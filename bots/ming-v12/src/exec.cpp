// exec — the runtime FSM replacing the starter decision list
// (docs/strategy.md §RUNTIME). Guard order inside tick:
//   channel-continuation → capture-stay (unless already carrying a win) →
//   pitstop → winning deposit → tactic::offer → bank → route → haul → idle.
// The stub planner is rebuilt from ledger::ready_round every turn — greedy
// nearest-ready over a deterministic slot partition — so FSM state is
// derived from (Obs, Plan, ledger), not stored. The statics below feed the
// transition log and the L1 instrumentation only; each is a pure function
// of the observation history.
#include <cambc.h>
#include "exec.h"
#include "camp_scoring.h"
#include "progress.h"
#include "runtime_policy.h"
#include "econ.h"
#include "tactic/support.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>
#include "econ.h"
#include "ledger.h"
#include "nav.h"
#include "obs.h"
#include "turn.h"
#include "plan.h"
#include "tactic/tactic.h"

namespace {

enum class State : std::uint8_t {
    boot, channel, tactic, pitstop, bank, travel, clear, capture, haul, idle
};

// A route/haul move whose nav::step fails this many turns running means
// the goal is pocketed behind walls/holes or a permanently sealed ring —
// blacklist the group for a cooldown so the route falls through to the
// next-best stop instead of starving on an unreachable target.
constexpr int nav_fail_limit = 6;
constexpr std::uint32_t nav_cooldown = 40;

// Recalls cancelled this many times in a row mean an attacker keeps
// interrupting the channel while staying out of vision — 'exposed' can't
// see it, so the only safe exit left is walking home.
constexpr int max_recall_retries = 3;

// A pitstop entry that abandons a live-camp target is a failed sortie;
// strike_limit in a row on one group means contested churn the static
// fitness gate can't price (a pack camped inside the enemy zerg), so the
// group joins the unreachable blacklist for a long cooldown.
constexpr int strike_limit = 3;
constexpr std::uint32_t strike_cooldown = 200;

// Keep the existing recovery reserve for a return trip to a wounded camp.
// This is NOT the retreat trigger: runtime_policy uses 55% HP normally and
// 75% near a visible enemy. Returning home should still restore useful HP.
constexpr float return_sortie_reserve = 0.55f;
constexpr std::int64_t opportunity_range = 6;

// Per-champion memory: the decision is re-derived each turn; these only
// power the transition log plus the contested/capture windows.
struct Mem {
    State state = State::boot;
    int slot = -1;
    int allies = 0;
    int contested_group = -1;
    std::int32_t contested_since = -1;
    int nav_pending = -1; // route/haul group the emitted Move steers to
    int nav_fail_count = 0;
    std::vector<std::uint32_t> unreachable_until; // per-group blacklist expiry
    std::vector<std::uint8_t> strikes;            // consecutive failed sorties per group
    int fled_group = -1;                          // camp the current pitstop fled; -1 = champion-driven
    // Last live read on a group's remaining hp/dps — the heal-to-need
    // input; hp < 0 means never seen alive this match.
    struct CampLeft { float hp = -1.0f, dps = 0.0f; std::int32_t round = -1; };
    std::vector<CampLeft> camp_left;
    // Cast-fizzle watchdog: a use() that throws never starts the cooldown,
    // so `is_ready` stays true and the naive loop would re-emit the same
    // fizzling cast every turn. Three consecutive strikes retire the slot.
    int last_cast_slot = -1;
    std::uint32_t last_cast_round = 0;
    std::uint8_t cast_strikes[2] = { 0, 0 };
    bool cast_banned[2] = { false, false };
    int recall_attempts = 0;
    bool recall_pending = false; // recall issued, outcome not yet observed
    std::int32_t state_since = 0; // round the current FSM state began — stuck-state audit anchor
    // Compiled-plan binding, filled once from the draft blob. `route` is a
    // view into the Bot-held Plan — both stay valid for the whole match.
    bool plan_bound = false;
    bool have_route = false;
    Role role = Role::FLEX;
    std::uint32_t bag_cap = 0;             // the plan's divert threshold for this slot
    Route route{};                         // this slot's cyclic stop list
    std::array<std::uint8_t, 8> owned{};   // 64-group mask of the route's CAMP stops
    std::uint8_t cursor = 0;               // cyclic scan position in route
    int active_target = -1;                // in-flight route target — hysteresis anchor
    progress::Tracker journey; // Best path distance, not merely whether a move succeeded.
    int bank_target = -1;
    int bank_pending = -1;
    std::vector<std::uint32_t> bank_blocked_until;
    int farm_pending = -1;
    int haul_target = -1;
    std::uint32_t haul_since = 0;
    std::uint32_t idle_since = 0;
    std::uint32_t next_diagnostic = 0;
    int failed_actions = 0;
    std::uint32_t tactic_retry_round = 0; // Failed optional offers cannot starve farming forever.
};
Mem mem;

struct GroupGeo {
    cambc::bitboard tiles;
    std::vector<cambc::vec2> tile_list;
    bool monolith = false; // 20k hp at 200 dmg — a planner decision (#8), not a stub stop
    std::uint32_t gems = 0;  // 0 = pure gold camp: fallback work only, never displaces a real capture
    float health = 0;        // Σ member base health — clear-time input to the kit-fitness gate
    float dps = 0;           // Σ member base strength per awake turn — the gate's retaliation input
    bool passive = true;     // no member fights back (clusters): retaliation is zero, always fittable
};

// Camp layouts are immutable — the host layout query runs once.
const std::vector<GroupGeo>& groups()
{
    static const std::vector<GroupGeo> cache = [] {
        // base_stats() prices like any host call but is a pure function of
        // camp_type — one cached lookup per kind covers every member on the
        // map (same dodge as atlas.cpp's design cache).
        std::array<std::optional<cambc::camp_stats>, 16> designs;
        std::vector<GroupGeo> out;
        for (const auto& group : turn::map_groups())
        {
            GroupGeo geo;
            group.spawn_tiles_into(geo.tiles);
            geo.tile_list = geo.tiles.positions();
            for (const auto& member : group)
            {
                geo.monolith = geo.monolith || member.is_monolith();
                const auto type = static_cast<std::size_t>(member.kind());
                if (!designs[type]) designs[type] = member.base_stats();
                geo.health += designs[type]->base_health;
                geo.dps += designs[type]->base_strength;
                geo.passive = geo.passive && designs[type]->base_strength <= 0;
            }
            geo.gems = group.gem_value();
            out.push_back(std::move(geo));
        }
        return out;
    }();
    return cache;
}


// Spawn tile → group index, -1 elsewhere; built once since layouts never change.
const std::vector<std::int16_t>& group_at()
{
    static const std::vector<std::int16_t> table = [] {
        const std::int32_t w = cambc::map::width(), h = cambc::map::height();
        std::vector<std::int16_t> t(static_cast<std::size_t>(w) * h, -1);
        for (std::size_t g = 0; g < groups().size(); ++g)
            for (const auto tile : groups()[g].tile_list)
                t[static_cast<std::size_t>(tile.y) * w + tile.x] = static_cast<std::int16_t>(g);
        return t;
    }();
    return table;
}

// Resolve a known in-map tile to its static camp identity. Combat uses spawn
// identity even if an observed monster was displaced to a different tile.
int group_of(cambc::vec2 p)
{
    return group_at()[static_cast<std::size_t>(p.y) * cambc::map::width() + p.x];
}

// Bind this body to its draft slot once, so routes and fallback ownership stay
// consistent instead of changing when visibility or champion priorities change.
int my_slot()
{
    if (mem.slot >= 0) return mem.slot;
    const auto allies = cambc::champions(cambc::team::ally);
    mem.allies = static_cast<int>(allies.size());
    const auto& me = turn::self();
    for (std::size_t i = 0; i < allies.size(); ++i)
        if (allies[i] == me) { mem.slot = static_cast<int>(i); break; }
    if (mem.slot < 0) mem.slot = 0; // self unlisted: fall back to slot zero
    return mem.slot;
}

// Bind this champion's slot route once. A blob that doesn't cover the slot
// (fewer slots than bodies) leaves have_route false → stub partition.
void bind_plan(const Plan& plan)
{
    if (mem.plan_bound) return;
    mem.plan_bound = true;
    if (mem.slot < 0 || mem.slot >= plan.num_slots()) return;
    mem.role = plan.role_at(static_cast<std::uint8_t>(mem.slot));
    mem.bag_cap = plan.bag_cap(mem.role);
    mem.route = plan.route_at(static_cast<std::uint8_t>(mem.slot));
    for (std::uint8_t i = 0; i < mem.route.size(); ++i)
        if (const Stop stop = mem.route[i]; stop.kind == StopKind::CAMP)
            mem.owned[stop.index >> 3] |= static_cast<std::uint8_t>(1u << (stop.index & 7));
    mem.have_route = !mem.route.is_empty();
}

// Ownership is a union: the compiled route marks its stops, and the modulo
// partition under it still gives every group exactly one fallback owner —
// unrouted camps (monolith-adjacent, above the solo floor) would otherwise
// be owned by nobody and never ground down.
bool mine(int g)
{
    const int stride = std::max(1, std::min(mem.allies, static_cast<int>(groups().size())));
    if (g >= 0 && g % stride == mem.slot % stride) return true;
    if (mem.have_route)
        return g < 64 && ((mem.owned[g >> 3] >> (g & 7)) & 1);
    return false;
}

// Sustained camp damage per turn for this body — the same estimate the
// planner prices routes with (planner.cpp kit_for): basic output plus one
// camp-damage ability amortised over the 8-turn slot cooldown, a ranged
// kit's nuke worth ~2x focus, a melee payload one extra basic. The burst
// term needs a materialised damage slot — attack_reset/heal kits (Vesper
// Vale, Willow) have zero castable camp damage, and camp_cast never emits
// those kinds. Design stats are fixed per champion, so the host lookup
// runs once.
float my_dps()
{
    static const float dps = [] {
        const auto& me = turn::self();
        const auto stats = me.base_stats();
        const float basic = stats.base_strength * stats.camp_damage_multiplier;
        float burst = 0;
        for (const auto& ab : me.abilities())
            if (ab.is_damage() || ab.is_aoe_damage())
                burst = stats.base_attack_range > 1 ? 2.0f * stats.base_focus : basic;
        return basic + burst / 8.0f;
    }();
    return dps;
}

// The planner's per-kit solo check at runtime (planner.cpp:256-258 mirror):
// pack retaliation across one full clear is site.dps × turns-to-clear, and
// a camp that burns ≥2 health bars is a trap — the sortie loop leaves the
// body dead or commuting, never collecting (lanes: every elk is unrouted
// for exactly this, yet the modulo partition re-dealt one to each slot).
// Passive groups deal no damage and pass by definition.
bool unfit(const GroupGeo& g, float my_max_health)
{
    return !g.passive
        && g.dps * std::max(1.0f, g.health / my_dps()) >= my_max_health * 2.0f;
}

// Borrow this turn's observation of a camp, if any. Missing vision is not a
// freshly observed empty camp and must not manufacture a free capture.
const CampObs* seen(const Obs& obs, int g)
{
    return (g >= 0 && g < 64) ? obs.camp_at[static_cast<std::size_t>(g)] : nullptr;
}

// A temporary routing failure suspends a camp until its retry round, rather
// than committing every future turn to the same blocked economic objective.
bool unreachable(int g, std::uint32_t round)
{
    return g < static_cast<int>(mem.unreachable_until.size())
        && mem.unreachable_until[static_cast<std::size_t>(g)] > round;
}



// Identical legality mask to nav.cpp's blocked_tiles — goal seeds must be
// masked with it because an occupied goal tile would still seed the descent.
// Served from the per-turn cache in turn.h.
const cambc::bitboard& blocked_tiles()
{
    return turn::blocked_tiles();
}

// Cheap proximity to a camp region ranks runtime fallback jobs. It is a lower
// bound, not a promise that walls permit a straight walk to the reward.
std::int64_t dist_to(const GroupGeo& g, cambc::vec2 p)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto tile : g.tile_list) best = std::min(best, p.chebyshev_distance(tile));
    return best;
}

// Grouped-opening stop order (issue #38): every body must derive the SAME
// chain — wire.h is full, so no shared-memory consensus can arbitrate a
// disagreement. Every input is static and shared across instances: map
// data (camp_groups, member base stats, a hard-terrain distance field
// from our spawn region) and the draft record for the clear estimate —
// summing per-body my_dps() would feed each body's own kit in and could
// split the chain. Ties break to the lowest group index. Elks and
// monoliths are excluded: elks are haulable single units, not group
// fights, and monolith tiles never open up.
// The chain extends lazily, one stop per index: each step costs a fresh
// distance field, and the round-1 fast path can only afford stop 0 inside
// the 2x budget — later stops get priced into normal-budget turns.
int opening_stop(int index)
{
    static std::vector<int> stops;
    static std::vector<char> used;
    static cambc::bitboard origin;
    static bool seeded = false, exhausted = false;
    const auto& geos = groups();
    if (!seeded)
    {
        used.assign(geos.size(), 0);
        origin = turn::spawn_region(cambc::team::ally);
        seeded = true;
    }
    // Whole-team clear estimate: the opening bodies fight together, so the
    // pack's health is priced in team-damage rounds. The sum runs over the
    // ally draft record — static and identical for every body — using only
    // each type's basic term (base_strength × camp_damage_multiplier,
    // matching my_dps()'s basic component; the per-body ability burst probe
    // stays out so kits can't skew the estimate). cassi_and_issac is one
    // pick but two bodies.
    static const float team_dps = [] {
        float total = 0;
        for (const auto pick : cambc::draft::current_draft(cambc::team::ally))
        {
            const auto add = [&](cambc::champion_type type) {
                const auto stats = cambc::champion(type).base_stats();
                total += stats.base_strength * stats.camp_damage_multiplier;
            };
            if (pick == cambc::champion_draft::cassi_and_issac)
            {
                add(cambc::champion_type::cassi);
                add(cambc::champion_type::issac);
            }
            else
            {
                const auto raw = static_cast<std::int32_t>(pick);
                add(static_cast<cambc::champion_type>(
                    raw + (raw > static_cast<std::int32_t>(cambc::champion_draft::cassi_and_issac) ? 1 : 0)));
            }
        }
        return std::max(total, 1.0f);
    }();
    static const auto& raw = turn::map_groups();
    static const auto hard = cambc::map::walls() | cambc::map::holes()
        | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
    while (static_cast<int>(stops.size()) <= index && !exhausted)
    {
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute(hard, origin, visited);
        int best = -1;
        float best_score = -1;
        for (std::size_t g = 0; g < geos.size(); ++g)
        {
            if (used[g] || geos[g].gems <= 0 || geos[g].monolith) continue;
            bool elk = false;
            for (const auto& member : raw[g])
                if (member.is_elk()) { elk = true; break; }
            if (elk) continue;
            std::uint16_t travel = cambc::distance_field::unreachable;
            for (const auto tile : geos[g].tile_list)
                travel = std::min(travel, field.get(tile));
            if (travel == cambc::distance_field::unreachable) continue;
            const float clear = geos[g].health / team_dps;
            const float score = static_cast<float>(geos[g].gems)
                / (static_cast<float>(travel) + clear);
            if (score > best_score) { best_score = score; best = static_cast<int>(g); }
        }
        if (best < 0) { exhausted = true; break; }
        used[static_cast<std::size_t>(best)] = 1;
        stops.push_back(best);
        origin = geos[best].tiles;
    }
    return static_cast<int>(stops.size()) > index ? stops[index] : -1;
}

// Only observed enemy positions establish a contested reward; this test never
// reveals hidden champions or claims that absence from vision proves safety.
bool enemy_on(int g, const Obs& obs)
{
    for (const auto& e : obs.enemies)
        if (group_of(e.position) == g) return true;
    return false;
}

// Lowest-health live camp in attack range belonging to this champion's
// partition — CLEAR's pick, and the capture-stand free hit (holding spawn
// tiles costs no actions, so an adjacent group gets whittled for free).
// harmless_only restricts that free hit to groups with no live attacker:
// damage wakes the whole group, so poking a wolf mid-capture buys 5
// rounds of retaliation while standing still.
// vision::camps() resolves every occupancy-mask tile through camp_at(),
// and the occupancy mask can still list a camp whose live state query
// fails (killed, knocked off its tile, or sitting past the vision edge
// where the mask reads stale). A camp that fails is_visible() throws
// error 97 on every stat read — skip it here so no caller has to.
// The tick calls this several times and each rebuild is a camp_tiles()
// mask plus a camp_at+is_visible pair per tile. Vision only changes
// between rounds or when this body itself moved (post-move apply), so
// memoise on (round, body, position). Stats ride along in the snapshot:
// within one body's turn only its own attack can change them, and no
// camp-stat read happens after an attack at the same position.
struct LiveCamp {
    cambc::camp handle;
    cambc::vec2 pos{};
    cambc::vec2 spawn{};
    float health = 0;
    float strength = 0;
    std::int64_t attack_range = 0;
};

const std::vector<LiveCamp>& visible_camps()
{
    static std::vector<LiveCamp> cache;
    static std::uint32_t cached_round = 0;
    static std::uint32_t cached_epoch = 0;
    const auto round = cambc::round_number();
    if (round == cached_round && turn::move_epoch() == cached_epoch)
        return cache;
    cached_round = round; cached_epoch = turn::move_epoch();
    cache.clear();
    // One sdk_camp record per visible camp replaces the is_visible +
    // position + health + strength chain of identical host fetches;
    // attack_range is design data, cached per kind. A hidden/missing
    // camp throws into the same catch as before.
    static const std::array<std::int64_t, 8> ranges = [] {
        std::array<std::int64_t, 8> out{};
        for (std::size_t k = 0; k < out.size(); ++k)
        {
            cambc::detail::camp_design design{};
            out[k] = cambc::detail::sdk_camp_design(static_cast<std::int32_t>(k), &design) == 0
                ? design.base_stats.base_attack_range : 1;
        }
        return out;
    }();
    for (const auto& tile : turn::camp_tiles().positions())
        try
        {
            const auto c = cambc::vision::camp_at(tile);
            cambc::detail::camp_state info{};
            if (cambc::detail::sdk_camp(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(c)),
                    &info) != 0)
                continue;
            const auto k = static_cast<std::size_t>(c.kind());
            cache.push_back({ c, { info.x, info.y }, c.spawn_position(), info.health,
                info.strength, k < ranges.size() ? ranges[k] : 1 });
        }
        catch (const cambc::error&) { }
    return cache;
}

// Same mask-vs-entity gap for champions: a teleporter or fresh corpse can
// vanish between champion_tiles() and champion_at(). Memoised on
// (round, body, position) for the same reason as visible_camps().
const std::vector<cambc::champion>& visible_enemies()
{
    static std::vector<cambc::champion> cache;
    static std::uint32_t cached_round = 0;
    static std::uint32_t cached_epoch = 0;
    const auto round = cambc::round_number();
    if (round == cached_round && turn::move_epoch() == cached_epoch)
        return cache;
    cached_round = round; cached_epoch = turn::move_epoch();
    cache.clear();
    for (const auto& tile : turn::champion_tiles(cambc::team::enemy).positions())
        try
        {
            const auto e = cambc::vision::champion_at(tile);
            if (e.is_visible()) cache.push_back(e);
        }
        catch (const cambc::error&) { }
    return cache;
}

struct FarmTargets {
    std::array<camp_scoring::Target, camp_scoring::max_targets> values{};
    std::size_t count = 0;

    // Only the populated prefix represents observed monsters. The unused
    // array tail must never become imaginary zero-coordinate farming targets.
    std::span<const camp_scoring::Target> view() const { return {values.data(), count}; }
};

// Reuse Foundry's pre-action visibility snapshot, then keep a bounded set of
// living members of the chosen group, putting legal centres before out-of-range
// splash victims. A distant prefix must not hide every legal shot on big packs.
// This function does not perform attacks;
// its cached HP stays valid until apply() commits the selected action.
FarmTargets farm_targets(int active_group, const cambc::bitboard& reach)
{
    FarmTargets out;
    const auto& visible = visible_camps();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& c : visible)
        {
            if (out.count == out.values.size() || !camp_scoring::can_score(cambc::fuel_remaining())) return out;
            if (c.health <= 0 || group_of(c.spawn) != active_group) continue;
            if (reach.is_set(c.pos) != (pass == 0)) continue;
            out.values[out.count++] = {c.pos.x, c.pos.y, c.health, c.strength};
        }
    }
    return out;
}

// Waking a different pack can turn a profitable clear into a retreat. Reject
// blast centres touching another group's public spawn tiles, even if hidden.
// Also check live positions of visible displaced camps. This is conservative:
// it does not claim that unseen monsters are alive or reveal their positions.
bool farm_area_safe(int active_group, cambc::vec2 centre, float radius)
{
    if (radius < 0 || radius > camp_scoring::max_area_radius) return false;
    const int reach = static_cast<int>(radius);
    const int width = cambc::map::width(), height = cambc::map::height();
    for (int y = std::max(0, centre.y - reach); y <= std::min(height - 1, centre.y + reach); ++y)
        for (int x = std::max(0, centre.x - reach); x <= std::min(width - 1, centre.x + reach); ++x)
            if (camp_scoring::foreign_group(group_of({x, y}), active_group)) return false;
    for (const auto& c : visible_camps())
        if (c.health > 0 && camp_scoring::foreign_group(group_of(c.spawn), active_group)
            && c.pos.chebyshev_distance(centre) <= radius) return false;
    return true;
}

// Adapt Rank1gu1's kill/splash priorities only for ordinary active-group
// farming. Capture's harmless free hits and enemy-champion tactics keep their
// Foundry selection. Returning data here preserves apply() as the action seam.
std::optional<exec::Action> attack_in_range(const cambc::bitboard& attack_tiles,
    int only_group = -1, bool harmless_only = false)
{
    if (only_group >= 0 && !harmless_only && camp_scoring::can_score(cambc::fuel_remaining()))
    {
        const auto targets = farm_targets(only_group, attack_tiles);
        const auto& me = turn::self();
        const bool splash = me.champion_name() == cambc::champion_type::varg;
        // Basic damage is an estimate: the immutable champion multiplier is
        // known, but this is not an exact camp-specific mitigation simulator.
        static const float multiplier = me.base_stats().camp_damage_multiplier;
        const float damage = turn::self_strength() * multiplier;
        int best = -1;
        float best_score = -1;
        cambc::vec2 best_tile{};
        for (const auto& target : targets.view())
        {
            if (!camp_scoring::can_score(cambc::fuel_remaining())) break;
            const cambc::vec2 tile{target.x, target.y};
            if (!attack_tiles.is_set(tile)) continue;
            float score = camp_scoring::basic_value(target, damage);
            if (splash)
            {
                constexpr float radius = cambc::constants::passive_abilities::varg_splash_radius;
                if (!farm_area_safe(only_group, tile, radius)) continue;
                // The primary target was counted above; Varg hits it only once.
                score += camp_scoring::area_value(targets.view(), tile.x, tile.y, radius, damage, false)
                    - camp_scoring::useful_damage(target.health, damage);
            }
            if (camp_scoring::better(score, 0, tile.x, tile.y, best_score, best, best_tile.x, best_tile.y))
            { best = 0; best_score = score; best_tile = tile; }
        }
        if (best >= 0) return exec::Attack{best_tile, exec::Attack::Kind::camp};
        // Fuel may have run out before ranking any candidate. The cheap scan
        // below checks all legal targets and keeps the same splash safety guard.
    }
    const bool protect_splash = only_group >= 0 && !harmless_only
        && turn::self().champion_name() == cambc::champion_type::varg;
    std::vector<char> dangerous;
    if (harmless_only)
    {
        dangerous.assign(groups().size(), 0);
        for (const auto& c : visible_camps())
            if (c.health > 0 && c.strength > 0)
            {
                const int g = group_of(c.spawn);
                if (g >= 0) dangerous[static_cast<std::size_t>(g)] = 1;
            }
    }
    bool found = false;
    cambc::vec2 best{};
    float best_health = 0;
    for (const auto& c : visible_camps())
    {
        if (c.health <= 0 || !attack_tiles.is_set(c.pos)) continue;
        const int g = group_of(c.spawn);
        if (g < 0 || (only_group >= 0 && g != only_group) || (only_group < 0 && !mine(g))
            || (!dangerous.empty() && dangerous[static_cast<std::size_t>(g)]))
            continue;
        const auto p = c.pos;
        if (protect_splash && !farm_area_safe(only_group, p,
                cambc::constants::passive_abilities::varg_splash_radius)) continue;
        if (!found || c.health < best_health
            || (c.health == best_health && (p.x < best.x || (p.x == best.x && p.y < best.y))))
        {
            found = true;
            best = p;
            best_health = c.health;
        }
    }
    if (!found) return std::nullopt;
    return exec::Attack{ best, exec::Attack::Kind::camp };
}

// Choose the most useful legal camp spell instead of the first ready slot.
// Rank1gu1's capped-damage/AoE idea fits this read-only selector; its direct
// basic+cast sequence does not. No own action is issued before scoring, so
// Foundry's round/movement-keyed snapshots have not been made stale by us.
std::optional<exec::Cast> camp_cast(int g, std::uint32_t round)
{
    if (!turn::self_state().contains(cambc::champion_state::can_cast)
        || !camp_scoring::can_score(cambc::fuel_remaining())) return std::nullopt;
    const auto& abilities = turn::abilities();
    int best_slot = -1;
    float best_score = -1;
    cambc::vec2 best_tile{};
    bool best_ground = false;
    for (std::uint8_t slot = 0; slot < 2; ++slot)
    {
        if (!camp_scoring::can_score(cambc::fuel_remaining())) break;
        if (mem.cast_banned[slot]) continue;
        const auto& ab = abilities[slot];
        if (!turn::slot_ready(slot) || (!ab.is_damage() && !ab.is_aoe_damage() && !ab.is_cc()))
            continue;
        const bool ground = turn::slot_targets(slot).contains(cambc::ability_target_mask::ground);
        if (!turn::slot_targets(slot).contains(cambc::ability_target_mask::camps) && !ground)
            continue;
        float radius = 0;
        if (ground)
        {
            if (ab.is_aoe_damage()) radius = ab.get<cambc::ability_aoe_damage>().radius;
            // The SDK CC payload has no radius. GameRules' Ryth slot 0 is an
            // explicit radius-one area stun; do not guess for unknown kits.
            else if (ab.is_cc() && turn::self().champion_name() == cambc::champion_type::ryth) radius = 1;
            else continue;
        }
        // Includes caster items/passives per the SDK. Do not multiply item
        // bonuses again. HP clipping/one-tick CC credit remain estimates.
        const float damage = ab.damage_amount();
        const cambc::bitboard& tiles = turn::slot_reach(slot);
        const auto targets = farm_targets(g, tiles);
        for (const auto& target : targets.view())
        {
            if (!camp_scoring::can_score(cambc::fuel_remaining())) break;
            const cambc::vec2 tile{target.x, target.y};
            if (!tiles.is_set(tile) || (ground && !farm_area_safe(g, tile, radius))) continue;
            const float score = ground
                ? camp_scoring::area_value(targets.view(), tile.x, tile.y, radius, damage, ab.is_cc())
                : camp_scoring::spell_value(target, damage, ab.is_cc());
            if (score <= 0) continue;
            if (camp_scoring::better(score, slot, tile.x, tile.y,
                    best_score, best_slot, best_tile.x, best_tile.y))
            {
                best_slot = slot; best_score = score; best_tile = tile; best_ground = ground;
            }
        }
    }
    if (best_slot >= 0)
    {
        mem.last_cast_slot = best_slot;
        mem.last_cast_round = round;
        return exec::Cast{static_cast<std::uint8_t>(best_slot), best_tile, best_ground, g};
    }
    return std::nullopt;
}

// Keep a reachable banking destination while it stays useful. A lock can be
// placed after we chose the altar, so recheck that live public state before
// spending more gem-carrying turns walking toward it.
std::optional<exec::Action> banking_action(const Obs& obs, const cambc::bitboard& blocked,
                                         int preferred)
{
    const auto& altars = turn::altars();
    if (mem.bank_blocked_until.size() != altars.size())
        mem.bank_blocked_until.assign(altars.size(), 0);
    // Produce legal submit-range tiles, including our current tile when we
    // have already arrived. Altars themselves remain impassable.
    const auto approach = [&](std::size_t i) {
        cambc::bitboard ring;
        ring.draw_chebyshev(altars[i], cambc::constants::submit_gems_range);
        ring &= ~blocked;
        return ring;
    };
    const int previous = mem.bank_target;
    bool previous_approach = previous >= 0 && previous < static_cast<int>(altars.size());
    if (previous_approach && approach(previous).is_empty())
    {
        // An occupied ring may reopen next turn; do not spin on it while
        // another altar has a legal tile for this champion's bag.
        mem.bank_blocked_until[previous] = obs.round + 12;
        previous_approach = false;
        mem.bank_target = -1;
    }
    if (!previous_approach) mem.bank_target = -1;
    const bool hostile_target = mem.bank_target >= 0
        && turn::altar_locked(altars[mem.bank_target])
        && turn::altar_lock_is_enemy(altars[mem.bank_target]);
    const bool cooling_target = mem.bank_target >= 0
        && mem.bank_blocked_until[mem.bank_target] > obs.round;
    if (mem.bank_target < 0 || hostile_target || cooling_target)
    {
        // A low-fuel turn keeps a still-reachable target. Against its lock,
        // submit_gems becomes a lock attack rather than a lost channel.
        if (cambc::fuel_remaining() < 25000 && mem.bank_target < 0)
            return std::nullopt;
        if (cambc::fuel_remaining() >= 25000)
        {
            cambc::bitboard origin, visited;
            origin.set(obs.self_pos);
            cambc::distance_field field;
            field.compute(blocked, origin, visited);
            int candidate = -1;
            bool best_hostile = true;
            unsigned best_distance = cambc::distance_field::unreachable;
            unsigned previous_distance = cambc::distance_field::unreachable;
            for (std::size_t i = 0; i < altars.size(); ++i)
            {
                unsigned distance = cambc::distance_field::unreachable;
                for (const auto tile : approach(i).positions())
                    distance = std::min(distance, static_cast<unsigned>(field.get(tile)));
                if (static_cast<int>(i) == previous) previous_distance = distance;
                if (distance == cambc::distance_field::unreachable) continue;
                if (mem.bank_blocked_until[i] > obs.round) continue;
                const bool hostile = turn::altar_locked(altars[i]) && turn::altar_lock_is_enemy(altars[i]);
                if (candidate < 0 || runtime_policy::better_bank(hostile, distance,
                        static_cast<int>(i) == preferred, best_hostile, best_distance,
                        candidate == preferred))
                { candidate = static_cast<int>(i); best_hostile = hostile; best_distance = distance; }
            }
            if (mem.bank_target < 0)
            {
                // The watchdog may have cooled the only reachable altar.
                // Retry it instead of waiting forever with gems in the bag.
                mem.bank_target = candidate >= 0 ? candidate
                    : previous_approach && previous_distance != cambc::distance_field::unreachable
                        ? previous : -1;
            }
            else if (candidate >= 0 && candidate != mem.bank_target
                && ((hostile_target && !best_hostile
                        && runtime_policy::open_altar_saves_time(previous_distance, best_distance))
                    || (cooling_target && hostile_target == best_hostile)))
            {
                // A nearby open altar saves the three lock-clearing attacks.
                // A distant one does not: keep the bag on its shorter route.
                mem.bank_target = candidate;
            }
        }
    }
    if (mem.bank_target < 0) return std::nullopt;
    mem.bank_pending = mem.bank_target;
    const auto altar = altars[mem.bank_target];
    if (obs.self_pos.chebyshev_distance(altar) <= cambc::constants::submit_gems_range)
        return exec::Channel{exec::Channel::Kind::tribute, altar};
    return exec::Move{approach(mem.bank_target)};
}

// Protect an escape without chasing. Before movement, only walking-compatible
// spells are allowed. If navigation fails, a stationary spell (including
// Stellar's damage) can use the otherwise lost turn against an immediate threat.
bool protect_escape(bool escape_failed = false)
{
    const auto& me = turn::self();
    if (!me.can_cast() || cambc::fuel_remaining() < 24000) return false;
    const auto& abilities = turn::abilities();
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.is_ready() || !ab.info()) continue;
        const bool exclusive = std::visit(
            [](const auto& payload) { return payload.exclusive_with_movement; }, *ab.info());
        if (!runtime_policy::escape_spell_allowed(exclusive, escape_failed))
            continue;
        try {
            const auto targets = ab.valid_targets();
            if ((ab.is_shield() || ab.is_heal()) && targets.contains(cambc::ability_target_mask::self))
            {
                if (ab.use()) { turn::note_action(); return true; }
            }
            if (!ab.is_cc() && !ab.is_damage() && !ab.is_aoe_damage()) continue;
            const auto reach = turn::slot_reach(slot);
            for (const auto& enemy : visible_enemies())
            {
                // Defense is optional work: leave fuel for navigation and the
                // normal turn epilogue instead of scanning every target at zero.
                if (cambc::fuel_remaining() < 24000) return false;
                if (!enemy.is_visible() || enemy.health() <= 0) continue;
                const auto p = enemy.position();
                if (!reach.is_set(p)) continue;
                const auto enemy_range = turn::enemy_attack_range(p);
                // Only retaliate against a visible champion able to threaten
                // us after one ordinary step. This does not create a chase job.
                if (!enemy_range || me.position().chebyshev_distance(p) > *enemy_range + 1) continue;
                const bool used = targets.contains(cambc::ability_target_mask::enemies)
                    ? ab.use(enemy) : targets.contains(cambc::ability_target_mask::ground) && ab.use(p);
                turn::note_action(); // A rejected ground cast may reveal a blocker too.
                if (used) return true;
            }
        } catch (const cambc::error&) { }
    }
    return false;
}

// A camp spell may leave a basic attack unused. Re-read live state after the
// spell, rescore survivors, and never switch to a different pack or a champion.
bool follow_camp_attack(int group)
{
    const auto& me = turn::self();
    if (group < 0 || cambc::fuel_remaining() < 35000 || !me.can_attack()) return false;
    if (const auto next = attack_in_range(turn::attack_tiles(), group))
        if (const auto* attack = std::get_if<exec::Attack>(&*next))
            try {
                const auto target = cambc::vision::camp_at(attack->target);
                if (target.is_visible() && target.health() > 0)
                { me.attack(target); turn::note_action(); return true; }
            } catch (const cambc::error&) { }
    return false;
}

// Spend a legal basic hit on a visible nearby attacker, without chasing it.
// Called before/after an attack reset so Varg's spell buys an EXTRA hit rather
// than replacing the free hit he already had. Copy a tile before invalidating
// caches: references into visible_enemies() must not survive a fresh query.
bool follow_enemy_attack()
{
    const auto& me = turn::self();
    if (cambc::fuel_remaining() < 18000 || !me.can_attack()) return false;
    const auto tiles = turn::attack_tiles();
    std::optional<cambc::vec2> target;
    float lowest_hp = std::numeric_limits<float>::max();
    for (const auto& enemy : visible_enemies())
        try {
            if (!enemy.is_visible()) continue;
            const float hp = enemy.health();
            const auto p = enemy.position();
            if (hp > 0 && hp < lowest_hp && tiles.is_set(p))
            { lowest_hp = hp; target = p; }
        } catch (const cambc::error&) { /* A stale enemy is not a valid hit. */ }
    if (!target) return false;
    try {
        const auto enemy = cambc::vision::champion_at(*target);
        if (!enemy.is_enemy() || !enemy.is_visible() || enemy.health() <= 0) return false;
        me.attack(enemy);
        turn::note_action();
        return true;
    } catch (const cambc::error&) { return false; }
}

// Record the champion's job transition while returning only an action proposal.
// The executor, not this bookkeeping, decides whether an engine action succeeds.
exec::Action done(State next, const exec::Action& action, const Obs& obs)
{
    if (next != mem.state)
    {
        mem.state = next;
        mem.state_since = static_cast<std::int32_t>(obs.round);
    }
    return action;
}

} // namespace

// Choose an economic job with explicit channel, capture and retreat priorities.
// Rank1gu5 retains Rank1gu4's draft, plan and action sequencing.
exec::Action exec::tick(const Obs& obs, const Plan& plan)
{
    try {
    using cs = cambc::champion_state;
    const auto st = obs.self_state;
    my_slot();
    mem.nav_pending = -1;
    mem.bank_pending = -1;
    mem.farm_pending = -1;

    // Round-1 fast path: a turn that overruns its fuel resumes on the next
    // round and only then commits the buffered action (GameRules §fuel) —
    // the full tick first-call (collect's vision scans plus every static
    // cache below) pushed first moves to round 2-3 while starters act on
    // round 1. Build just the opening chain here and travel straight at
    // stop 0's capture tiles; normal routing takes over from round 2.
    if (obs.round <= 1)
    {
        // The opening chain and the tactic geo caches are static map work
        // (~30-40k fuel each on the big maps) — building them inside the
        // 2x first-turn budget keeps the spike off normal turns. Round-2+
        // callers hit the same lazily-filled caches either way, so no
        // decision input changes.
        const int g0 = opening_stop(0);
        if (g0 >= 0)
        {
            const auto free_tiles = groups()[g0].tiles & ~blocked_tiles();
            if (!free_tiles.is_empty())
            {
                // Statics that would otherwise spike a mid-game turn —
                // each is pure map/draft data, so warming is invisible to
                // decisions. The heavy geo caches go through warm_statics
                // under a fuel check instead of all landing at once.
                opening_stop(1);
                opening_stop(2);
                group_at();
                my_dps();
                turn::abilities();
                turn::slot_targets(0);
                turn::slot_targets(1);
                turn::altars();
                mem.nav_pending = g0;
                return done(State::travel, Move{ std::move(free_tiles) }, obs);
            }
        }
    }

    bind_plan(plan);

    // Mid-channel turns can't act — tribute/recall/teleport continue alone.
    if (st.contains(cs::submitting_gems) || st.contains(cs::recalling) || st.contains(cs::teleporting)
        || (!st.contains(cs::can_move) && !st.contains(cs::can_attack)))
        return done(State::channel, Wait{}, obs);

    // Cast-fizzle verify: a successful cast spends the 8-turn cooldown, so
    // a slot still ready the very next round fizzled (thrown at apply).
    // Three in a row retires the slot — without this a fizzling cast is
    // re-emitted every turn forever.
    if (mem.last_cast_slot >= 0 && obs.round > mem.last_cast_round)
    {
        try
        {
            const auto s = static_cast<std::size_t>(mem.last_cast_slot);
            if (obs.round == mem.last_cast_round + 1)
            {
                if (turn::slot_ready(static_cast<std::uint32_t>(s)))
                {
                    if (++mem.cast_strikes[s] >= 3) mem.cast_banned[s] = true;
                }
                else
                    mem.cast_strikes[s] = 0;
            }
        }
        catch (const cambc::error&) { }
        mem.last_cast_slot = -1;
    }


    const auto& geos = groups();
    const cambc::vec2 pos = obs.self_pos;
    const int here = group_of(pos);
    const bool at_spawn = turn::spawn_region(cambc::team::ally).is_set(pos);

    if (mem.strikes.size() != geos.size()) mem.strikes.assign(geos.size(), 0);
    if (mem.camp_left.size() != geos.size()) mem.camp_left.assign(geos.size(), {});
    // A group seen dead clears its strike history: someone ground it down
    // this life, so prior failed sorties no longer evidence a trap.
    for (const auto& c : obs.camps)
        if (c.group < mem.strikes.size()
            && (std::holds_alternative<cambc::map::camp_group_respawning>(c.state)
                || std::holds_alternative<cambc::map::camp_group_reward_available>(c.state)))
            mem.strikes[c.group] = 0;
    // Snapshot live member hp/dps sums — rebuilt each round from the
    // members currently in vision, so an edge-of-vision sighting
    // overwrites a fuller read with the subset sum and heal_target
    // under-prices the real pack (a known under-estimate, tolerated: the
    // miss only releases a sortie early). Camps don't regen, so a full
    // read stays exact until someone else touches the pack.
    for (const auto& c : visible_camps())
    {
        const int g = group_of(c.spawn);
        if (g < 0) continue;
        auto& left = mem.camp_left[static_cast<std::size_t>(g)];
        if (left.round != static_cast<std::int32_t>(obs.round))
        {
            left.hp = 0;
            left.dps = 0;
            left.round = static_cast<std::int32_t>(obs.round);
        }
        if (c.health > 0)
        {
            left.hp += c.health;
            left.dps += c.strength;
        }
    }

    // Foundry's cheap threat proxy: a visible enemy within three tiles can
    // attack during an attempted recall, so start escaping sooner.
    bool threat = false;
    for (const auto& e : obs.enemies)
        if (pos.chebyshev_distance(e.position) <= 3) { threat = true; break; }
    // The scoreboard is public; the bag belongs to this champion. No ally or
    // hidden-enemy inventory is needed to know this deposit would finish.
    const bool finishing_bag = obs.self_gems > 0 && runtime_policy::winning_bag(
        turn::submitted_gems(cambc::team::ally), obs.self_gems, cambc::constants::winning_gems);
    // Preserve the farmer as well as its bag: retreat at 55% HP normally,
    // or at 75% near a visible enemy, before optional fights or camp farming.
    const bool unsafe = runtime_policy::should_retreat(
        obs.self_health, obs.self_max_health, threat);

    // A recall we emitted that never shows in champion_state was cancelled
    // by damage/stun between turns; a completed one lands on spawn.
    if (mem.recall_pending)
    {
        mem.recall_pending = false;
        if (at_spawn) mem.recall_attempts = 0;
        else ++mem.recall_attempts;
    }
    if (!unsafe && mem.state != State::pitstop)
        mem.recall_attempts = 0; // Reset between retreats, not during an unfinished escape.

    // Standing on own group's reward tiles: an uncontested stay completes
    // the capture (passive — free hit meanwhile), a contested one holds
    // only inside the capture-time grace window while it's safe to.
    // Do not wait for more gems with a winning bag or any critically hurt farmer.
    if (!finishing_bag && !unsafe && here >= 0 && mine(here))
    {
        if (const CampObs* c = seen(obs, here); c && std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
        {
            const bool contested = enemy_on(here, obs);
            if (!contested)
            {
                // Low HP alone doesn't break the stand — the group's own
                // camps are dead and strangers sleep two rounds after we
                // stop poking — but an enemy champion in reach does.
                if (obs.enemies_complete && !(threat && obs.self_health < obs.self_max_health * 0.75f))
                {
                    const auto& attack_tiles = turn::attack_tiles();
                    if (const auto hit = attack_in_range(attack_tiles, -1, true))
                        return done(State::capture, *hit, obs);
                    return done(State::capture, Wait{}, obs);
                }
            }
            else
            {
                if (mem.contested_group != here) { mem.contested_group = here; mem.contested_since = obs.round; }
                const bool grace = static_cast<std::int32_t>(obs.round) - mem.contested_since <= cambc::constants::capture_time + 1;
                if (grace && !unsafe)
                {
                    const auto& attack_tiles = turn::attack_tiles();
                    if (const auto hit = attack_in_range(attack_tiles, -1, true))
                        return done(State::capture, *hit, obs);
                    return done(State::capture, Wait{}, obs);
                }
            }
            // Timed out or threatened — contested camp is excluded below ("else move on").
        }
    }
    if (mem.contested_group != here) { mem.contested_group = -1; mem.contested_since = -1; }

    // Pitstop starts at the health threshold above, regardless of bag size.
    // Recall takes 5 owner turn ends (3 with Home), not an instant teleport;
    // damage interrupts it. Walk away from visible/incompletely scanned threats
    // first, then try recall. Hidden attackers can still interrupt it, so retry
    // at most max_recall_retries before continuing on foot. Defensive heals,
    // shields and legal retaliation can support the walk without a chase.
    // Keep an ongoing retreat until recovered, avoiding a return to combat
    // after a tiny heal. Then resume normal banking and gem collection.
    // The existing return-sortie healing budget stays independent of the new
    // entry threshold. At spawn, one turn end heals to full without a recall;
    // bot.cpp also runs the existing shop queue for shards/emblem progress.
    float heal_target = obs.self_max_health * 0.9f;
    if (mem.fled_group >= 0 && mem.fled_group < static_cast<int>(geos.size()))
    {
        const auto& left = mem.camp_left[static_cast<std::size_t>(mem.fled_group)];
        const float hp_left = left.hp >= 0 ? left.hp : geos[mem.fled_group].health;
        const float dps_left = left.hp >= 0 ? left.dps : geos[mem.fled_group].dps;
        if (hp_left > 0)
            heal_target = std::min(heal_target, obs.self_max_health * return_sortie_reserve
                + dps_left * std::max(1.0f, hp_left / my_dps()) * 1.25f);
    }
    // The 75% recovery target only completes an ALREADY-started retreat; it
    // cannot initiate one. All champions keep this recovery commitment.
    const bool recovered = mem.state != State::pitstop
        || (at_spawn ? obs.self_health >= heal_target
                     : obs.self_health >= obs.self_max_health * 0.75f);
    const bool needs_spawn_heal = at_spawn && obs.self_health < heal_target;
    if (needs_spawn_heal || unsafe || !recovered)
    {
        // fled_group prices the heal target against the pack that dealt
        // the damage — the group we're standing in, not the route
        // destination. Off the tiles (ranged ring, mid-commute) the active
        // target stays the better guess.
        if (mem.state != State::pitstop)
            mem.fled_group = (mem.state == State::travel || mem.state == State::clear)
                && !threat ? (here >= 0 ? here : mem.active_target) : -1;
        // Sortie watchdog: contested exits only — an attrition bail is
        // paid-for progress (the pack keeps its wounds), while a bail
        // under enemy reach buys nothing. Pure-grind maps would otherwise
        // blacklist their only work. A camp that died mid-sortie (reward
        // or respawn already visible) is a success either way.
        if ((mem.state == State::travel || mem.state == State::clear)
            && mem.active_target >= 0 && threat)
        {
            const CampObs* c = seen(obs, mem.active_target);
            const bool died = c
                && (std::holds_alternative<cambc::map::camp_group_respawning>(c->state)
                    || std::holds_alternative<cambc::map::camp_group_reward_available>(c->state));
            if (!died)
            {
                const auto g = static_cast<std::size_t>(mem.active_target);
                if (++mem.strikes[g] >= strike_limit)
                {
                    if (mem.unreachable_until.size() != geos.size())
                        mem.unreachable_until.assign(geos.size(), 0);
                    mem.unreachable_until[g] = obs.round + strike_cooldown;
                    mem.strikes[g] = 0;
                }
            }
        }
        if (at_spawn) return done(State::pitstop, Wait{}, obs); // turn end heals to full
        bool exposed = threat || !obs.enemies_complete || !obs.camps_complete;
        for (const auto& c : visible_camps())
            if (!exposed && c.health > 0 && c.strength > 0
                && pos.chebyshev_distance(c.pos) <= c.attack_range)
                exposed = true;
        // An enemy at reach+2 walks in and breaks the 5-round channel:
        // walking home instead keeps us moving out of its attack line.
        if (!exposed)
            for (const auto& e : obs.enemies)
                if (pos.chebyshev_distance(e.position) <= 5) { exposed = true; break; }
        if (!exposed && mem.recall_attempts < max_recall_retries)
        {
            mem.recall_pending = true;
            return done(State::pitstop, Channel{ Channel::Kind::recall }, obs);
        }
        return done(State::pitstop, Move{ turn::spawn_region(cambc::team::ally), true }, obs);
    }

    // Necessary survival and active channels still come first. Once safe, a
    // winning bag bypasses bag thresholds, extra captures and optional tactics.
    // Reuse the same reachable-altar selection and route watchdog as banking.
    if (finishing_bag)
    {
        if (const auto action = banking_action(obs, blocked_tiles(), -1))
            return done(State::bank, *action, obs);
        return done(State::bank, Wait{}, obs); // Retry the route; do not resume farming.
    }

    // Optional robbery/poking cannot override either the emergency test or a
    // sticky retreat. Missing enemy observations also cannot justify aggression.
    if (obs.enemies_complete && obs.round >= mem.tactic_retry_round)
        try {
            if (const auto offer = tactic::offer(obs)) return done(State::tactic, *offer, obs);
        } catch (const std::exception&) { }

    const auto blocked = blocked_tiles();

    // Bank trigger: with a compiled route the plan's role bag_cap adds an
    // early divert threshold on top of econ's; when it fires, the route's
    // next spliced ALTAR stop is the preferred tribute point.
    int preferred = -1; // route's next ALTAR stop, when the cap triggered it
    const bool over_cap = mem.have_route && mem.bag_cap && obs.self_gems >= mem.bag_cap;
    if (over_cap)
    {
        const auto& altars = turn::altars();
        for (std::uint8_t i = 0; i < mem.route.size() && preferred < 0; ++i)
            if (const Stop stop = mem.route.at(mem.cursor + i);
                stop.kind == StopKind::ALTAR && stop.index < altars.size())
                preferred = stop.index;
    }
    // econ::should_tribute covers the piñata ceiling, the deadline walk and
    // the scoreboard-aware dynamic cap; the plan's per-role bag_cap only
    // tightens it (a role carrying past its splice point banks early).
    const bool bank_now = over_cap || econ::should_tribute(obs);
    if (bank_now)
    {
        if (const auto action = banking_action(obs, blocked, preferred))
            return done(State::bank, *action, obs);
        // If all banking approaches are sealed, the temporary cooldown expires
        // and retries them. Do not wander with a large bag chasing more camps.
        return done(State::bank, Wait{}, obs);
    }
    mem.bank_target = -1;

    // Route step. With a compiled route: cyclic scan from the cursor — the
    // first actionable CAMP stop wins, ALTAR/PITSTOP stops are splice markers
    // the bank and pitstop guards consume. Without one: the stub's greedy
    // nearest-ready over the round-robin partition, a confirmed reward
    // outranking a believed-live camp at any distance.
    int target = -1;
    bool target_reward = false;
    std::int64_t target_cost = 0;
    int fit_gem_supply = 0; // owned/route gem camps this body could ever solo
    // Gold-only camps are filler: the race is 1000 tributed gems, so the
    // best gem-bearing target always wins and a 0-gem camp is picked only
    // when no gem target is actionable at all.
    int gold_target = -1;
    bool gold_reward = false;
    std::int64_t gold_cost = 0;
    // In-flight target hysteresis: re-deriving greedy-nearest every turn
    // dithers on the boundary between two equidistant camps (step toward A
    // makes B nearer, step toward B makes A nearer — one champion froze an
    // entire match). While the active target stays actionable, a rival must
    // win by a real margin or outrank it on reward-vs-alive to take over.
    int active_state = 0; // 0 not actionable, 1 alive-belief, 2 reward
    std::int64_t active_cost = 0;
    // Unified candidate pass: the route's camp stops plus every group the
    // partition owns, deduped and evaluated once each. A gem pack two tiles
    // away must outrank a route-listed gold cluster forty tiles away — the
    // two-tier scan (route only, then grind on exhaustion) made Vendetta
    // march past an owned pack to the map's far edge on rows.
    std::uint64_t scanned = 0; // group ids share mem.owned's <64 space
    const auto consider = [&](int g, std::int8_t cursor_idx, bool relaxed) {
        if (g < 0 || g >= static_cast<int>(geos.size()) || ((scanned >> g) & 1)) return;
        scanned |= 1ull << g;
        // The unfit gate must sit ahead of the active-target mark below:
        // a camp this body can't solo has to drop out of hysteresis, not
        // ride the +8 margin into another doomed sortie. But it sits BEHIND
        // the lifecycle read: a pending reward has no fight left to be
        // unfit for — crossfire kills leave free five-round captures the
        // owner never fought, and gating those leaks the reward entirely.
        if (geos[g].monolith) return;
        const bool is_unfit = unfit(geos[g], obs.self_max_health);
        if (geos[g].gems && !is_unfit) ++fit_gem_supply;
        if (unreachable(g, obs.round)) return;
        bool reward = false;
        if (const CampObs* c = seen(obs, g))
        {
            if (std::holds_alternative<cambc::map::camp_group_respawning>(c->state))
                return;
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
            {
                if (enemy_on(g, obs)) return; // contested, we hold nothing: move on
                if ((geos[g].tiles & ~blocked).is_empty()) return; // ally already covers it
                reward = true;
            }
        }
        else
        {
            const int ready = ledger::ready_round(g);
            // 'Reward pending' describes a past sighting, not permanent truth.
            // Revisit aged unseen rewards as UNKNOWN work. Do not award the
            // free-capture priority or bypass solo fitness until actually seen.
            if (ready > static_cast<std::int32_t>(obs.round)
                && !runtime_policy::stale_reward(ready, ledger::last_seen_round(g), obs.round))
                return;
        }
        if (is_unfit && !reward && !relaxed) return;
        // Cost ≈ commute + expected sortie churn: each sortie spends a
        // recall, a heal and a return walk (~80t at speed 1), so a passive
        // cluster two screens away beats a three-sortie wolf next door
        // for a body that can't tank it. Fit kits price ~0 extra — the
        // gate already guarantees they clear inside one bar.
        const float sorties = geos[g].passive ? 0.0f
            : geos[g].dps * (geos[g].health / my_dps()) / (obs.self_max_health * 0.65f);
        const auto cost = dist_to(geos[g], pos)
            + static_cast<std::int64_t>(sorties * 60.0f);
        if (g == mem.active_target) { active_state = reward ? 2 : 1; active_cost = cost; }
        int& bt = geos[g].gems ? target : gold_target;
        bool& br = geos[g].gems ? target_reward : gold_reward;
        std::int64_t& bc = geos[g].gems ? target_cost : gold_cost;
        if (bt < 0 || (reward && !br) || (reward == br && cost < bc))
        {
            bt = g;
            br = reward;
            bc = cost;
            if (&bt == &target && cursor_idx >= 0) mem.cursor = static_cast<std::uint8_t>(cursor_idx);
        }
    };

    const auto scan = [&](bool relaxed, bool remote = false) {
        if (mem.have_route)
        {
            // Cyclic scan from the cursor — strict cycle order would force
            // cross-map legs between respawns; the winning pattern is
            // re-harvesting the same few camps each period.
            const auto n = mem.route.size();
            for (std::uint8_t i = 0; i < n; ++i)
            {
                const auto idx = static_cast<std::uint8_t>((mem.cursor + i) % n);
                if (const Stop stop = mem.route[idx]; stop.kind == StopKind::CAMP)
                    consider(stop.index, static_cast<std::int8_t>(idx), relaxed);
            }
        }
        // Opportunistic reach: a gem camp a few tiles outside the
        // partition is closer than the assigned share — claim it unless a
        // readable ally is already on its tiles (clearing or capturing).
        // Ally position reads are vision-gated; a throw can't prove
        // coverage, so the ally doesn't count as covering.
        // Readable ally positions are fetched lazily once per scan and
        // shared across candidate groups — roster reads are vision-gated
        // host calls, so per-group re-reads would burn fuel for nothing.
        std::vector<cambc::vec2> readable_allies;
        bool allies_loaded = false;
        const auto open_nearby = [&](std::size_t g) {
            if (!geos[g].gems || (!remote && dist_to(geos[g], pos) > opportunity_range)) return false;
            // A nearby ally may be idle, not clearing this pack. Only during
            // the remote fallback, a personally observed LIVE pack is still
            // useful work. The normal fitness/blacklist gates remain below;
            // unseen camps and rewards keep the original ownership checks.
            if (remote)
                if (const CampObs* c = seen(obs, static_cast<int>(g));
                    c && std::holds_alternative<cambc::map::camp_group_alive>(c->state))
                    return true;
            if (!allies_loaded)
            {
                allies_loaded = true;
                try
                {
                    const auto& allies = turn::roster(cambc::team::ally);
                    const auto& me = turn::self();
                    for (std::size_t i = 0; i < allies.size(); ++i)
                    {
                        if (allies[i] == me) continue;
                        readable_allies.push_back(turn::roster_position(cambc::team::ally, i));
                    }
                }
                catch (const cambc::error&) { }
            }
            for (const auto& ap : readable_allies)
                if (dist_to(geos[g], ap) <= 2) return false;
            return true;
        };
        // The modulo partition under the route: unrouted leftovers (packs
        // above the solo floor, monolith-adjacent) still have an owner to
        // grind them; without a route this IS the whole job.
        for (std::size_t g = 0; g < geos.size(); ++g)
            if (mine(static_cast<int>(g)) || open_nearby(g))
                consider(static_cast<int>(g), -1, relaxed);
    };

    // Grouped opening (issue #38): while the chain has an unfinished stop
    // it overrides the route scan — the first gem packs are cleared
    // together, which is where the platform openings were being lost.
    // A stop is finished once the ledger marks it captured: a visible
    // respawning state, or a captured/dead ready_at; an unseen stop whose
    // ready_at still says "alive" is believed live and still owed a visit.
    if (obs.round <= 90)
    {
        // Chain stops are priced lazily — one distance field each (~30k on
        // the big maps) — so pulling the whole vector at once spikes a
        // normal-budget turn past 100k. opening_stop(i) computes only the
        // stops the scan actually reaches.
        // Chain stops are priced lazily — one distance field each (~30k on
        // the big maps) — so pulling the whole vector at once spikes a
        // normal-budget turn past 100k. opening_stop(i) computes only the
        // stops the scan actually reaches; the chain still caps at three.
        const auto open_stop = [&](int after) -> int {
            for (int i = after + 1; i < 3; ++i)
            {
                const int g = opening_stop(i);
                if (g < 0) return -1;
                if (unreachable(g, obs.round)) continue;
                if (const CampObs* c = seen(obs, g))
                {
                    if (std::holds_alternative<cambc::map::camp_group_respawning>(c->state))
                        continue;
                    return i;
                }
                const int ready = ledger::ready_round(g);
                if (ready == ledger::never || ready > static_cast<int>(obs.round))
                    continue;
                return i;
            }
            return -1;
        };
        for (int oi = open_stop(-1); oi >= 0; oi = open_stop(oi))
        {
            const int g = opening_stop(oi);
            const CampObs* c = seen(obs, g);
            if (!c || !std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
            {
                target = g;
                break;
            }
            // One body stands on a pending reward; the rest walk to the
            // next stop. Nearest to a free capture tile wins (ties: the
            // lower ally slot). Ally position reads are vision-gated —
            // any throw means the roster can't be arbitrated, so this
            // body captures itself.
            const auto free_tiles = geos[g].tiles & ~blocked;
            const auto dist_free = [&](cambc::vec2 p) {
                std::int64_t best = std::numeric_limits<std::int64_t>::max();
                for (const auto t : free_tiles.positions())
                    best = std::min(best, p.chebyshev_distance(t));
                return best;
            };
            bool stands = !free_tiles.is_empty();
            if (stands && !free_tiles.is_set(pos))
            {
                const auto mine_d = dist_free(pos);
                try
                {
                    const auto& allies = turn::roster(cambc::team::ally);
                    const auto& me = turn::self();
                    for (std::size_t i = 0; i < allies.size() && stands; ++i)
                    {
                        if (allies[i] == me) continue;
                        const auto d = dist_free(turn::roster_position(cambc::team::ally, i));
                        if (d < mine_d || (d == mine_d && static_cast<int>(i) < mem.slot))
                            stands = false;
                    }
                }
                catch (const cambc::error&) { }
            }
            if (!stands) continue; // next open stop, or fall to solo logic
            mem.nav_pending = g;
            return done(State::travel, Move{ free_tiles }, obs);
        }
    }

    bool desperate = false;
    if (target < 0)
    {
        scan(false);
        // Desperation: a body that owns zero fit gem camps (elkring, rows —
        // every pack above the solo floor) can't sit on 0-gem gold or idle
        // forever; the tribute race is paid in gems, so it grinds the
        // respawn lull never reads as desperation.
        desperate = fit_gem_supply == 0;
        if (desperate) { scanned = 0; scan(true); }

        // If assigned gem work is unavailable, compare safe remote gem work
        // before parking or farming gold. Ownership still wins the first pass;
        // reward capture ownership is preserved; a nearby idle ally does not
        // exclude personally observed live work in the remote-only pass.
        if (target < 0)
        {
            scanned = 0;
            scan(false, true);
            if (target >= 0)
            {
                // A nearby respawn due shortly can beat a cross-map errand.
                // Compare estimated travel/wait plus clear time; this is a
                // cheap timing estimate, not a hidden-state camp query.
                const float remote_time = static_cast<float>(dist_to(geos[target], pos))
                    + geos[target].health / my_dps();
                for (std::size_t g = 0; g < geos.size(); ++g)
                {
                    if (!mine(static_cast<int>(g)) || !geos[g].gems || geos[g].monolith
                        || unfit(geos[g], obs.self_max_health) || unreachable(static_cast<int>(g), obs.round)) continue;
                    const auto ready = ledger::ready_round(static_cast<int>(g));
                    const int travel = static_cast<int>(dist_to(geos[g], pos));
                    if (!runtime_policy::worth_waiting(ready, obs.round, travel)) continue;
                    const float local_time = static_cast<float>(std::max(travel, ready - static_cast<int>(obs.round)))
                        + geos[g].health / my_dps();
                    if (local_time < remote_time) { target = -1; break; }
                }
            }
        }

        if (target < 0) { target = gold_target; target_reward = gold_reward; }
        // An in-flight gold errand is preemptible the moment real work exists.
        if (active_state && mem.active_target >= 0 && geos[mem.active_target].gems == 0 && target >= 0)
            active_state = 0;

        if (active_state)
        {
            // Commit until done unless a gem rival clearly outranks: a pending
            // capture beats a live-camp errand, and a respawned camp eight
            // tiles closer beats the walk. 1-3 tile margins still dither on
            // Voronoi boundaries — the threshold must exceed the flip band.
            const bool rival = target >= 0 && geos[target].gems > 0
                && ((target_reward && active_state == 1)
                    || (target_reward == (active_state == 2) && target_cost + 8 < active_cost));
            if (target < 0 || !rival)
            {
                target = mem.active_target;
                target_reward = active_state == 2;
            }
        }
    }
    mem.active_target = target;

    if (target >= 0)
    {
        mem.idle_since = 0;
        mem.haul_target = -1;
        const CampObs* c = seen(obs, target);
        if (c && std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
        {
            auto free_tiles = geos[target].tiles & ~blocked;
            mem.nav_pending = target;
            return done(State::travel, Move{ std::move(free_tiles) }, obs);
        }
        // Alive (or believed): hit the target already in range — focused
        // fire, so a passerby group never gets woken on the way in —
        // else descend to a free tile inside our own attack ring.
        mem.farm_pending = target;
        if (const auto cast = camp_cast(target, obs.round))
            return done(State::clear, *cast, obs);
        const auto& attack_tiles = turn::attack_tiles();
        if (const auto hit = attack_in_range(attack_tiles, target))
            return done(State::clear, *hit, obs);
        cambc::bitboard goals;
        const auto range = static_cast<float>(turn::self().attack_range());
        for (const auto& member : visible_camps())
            if (member.health > 0 && group_of(member.spawn) == target)
                goals.draw_chebyshev(member.pos, range);
        goals &= ~blocked;
        if (goals.is_empty()) goals = geos[target].tiles & ~blocked; // rings sealed off: walk onto the tiles
        if (goals.is_empty()) goals = geos[target].tiles;
        mem.nav_pending = target;
        return done(State::travel, Move{ std::move(goals) }, obs);
    }

    // Haul: pre-position at the soonest-ready partition group, parked on
    // the ring — an occupied spawn tile would delay the respawn itself.
    int haul = -1;
    std::int32_t haul_ready = ledger::never;
    std::int64_t haul_dist = 0;
    for (std::size_t g = 0; g < geos.size(); ++g)
    {
        // unfit here too: pre-parking on a trap's ring means the respawned
        // pack wakes onto a body that can't fight it — pitstop churn with
        // no active_target, which the strike watchdog can never see.
        if (!mine(static_cast<int>(g)) || geos[g].monolith
            || unreachable(static_cast<int>(g), obs.round)
            || (unfit(geos[g], obs.self_max_health) && !desperate))
            continue;
        const std::int32_t ready = ledger::ready_round(static_cast<int>(g));
        if (ready == ledger::never) continue;
        const auto d = dist_to(geos[g], pos);
        // Overdue observations need inspection, not an endless wait on a ring.
        // Long respawns lose to remote work and are reconsidered on later turns.
        if (!runtime_policy::worth_waiting(ready, obs.round, static_cast<int>(d))) continue;
        if (haul < 0 || ready < haul_ready || (ready == haul_ready && d < haul_dist))
        {
            haul = static_cast<int>(g);
            haul_ready = ready;
            haul_dist = d;
        }
    }
    if (haul >= 0)
    {
        if (mem.haul_target != haul) { mem.haul_target = haul; mem.haul_since = obs.round; }
        cambc::bitboard ring;
        for (const auto tile : geos[haul].tile_list) ring.draw_chebyshev(tile, 1);
        ring &= ~geos[haul].tiles & ~blocked;
        if (!ring.is_set(pos) && !ring.is_empty())
        {
            mem.haul_since = obs.round; // Travel time is not stationary waiting.
            mem.nav_pending = haul;
            return done(State::haul, Move{ std::move(ring) }, obs);
        }
        if (ring.is_set(pos) && obs.round - mem.haul_since < 12)
            return done(State::haul, Wait{}, obs);
        // A blocked ring or repeatedly delayed respawn loses its reservation.
        if (mem.unreachable_until.size() != geos.size()) mem.unreachable_until.assign(geos.size(), 0);
        mem.unreachable_until[static_cast<std::size_t>(haul)] = obs.round + nav_cooldown;
        mem.haul_target = -1;
    }

    // Idle: never park on a group's tiles — occupancy delays respawns and
    // joins captures the partition gave to someone else.
    if (here >= 0)
    {
        cambc::bitboard off;
        for (const auto tile : geos[here].tile_list) off.draw_chebyshev(tile, 1);
        off &= ~geos[here].tiles & ~blocked;
        if (!off.is_empty()) return done(State::haul, Move{ std::move(off) }, obs);
    }

    // L1: an idle turn while an owned live uncontested camp exists is a
    // machine violation — flag it rather than hide it. Uncontested can only
    // be proven by vision: an unseen group may be enemy-held, so a ledger
    // belief alone never flags. Unreachable and unsoloable groups are
    // legitimately idle.
    return done(State::idle, Wait{}, obs);
    } catch (const std::exception& err)
    {
        // A stale live read mid-decision costs this turn only — same as
        // the engine-side fizzle policy apply() already follows. A
        // deterministic throw would otherwise surface as a bare per-turn
        // Wait — an idle body with zero diagnostics — so echo the fault
        // the way play_turn's catch would. Rate-limited and fuel-floored:
        // a starved turn can't afford the write.
        static std::uint32_t next_log = 0;
        if (obs.round >= next_log && !cambc::is_draft()
            && cambc::fuel_remaining() >= 4000)
        {
            next_log = obs.round + 40;
            std::cout << "bot error: exec::tick r" << obs.round
                      << ": " << err.what() << '\n';
        }
        return exec::Wait{};
    }
}

// Commit the job, then only legal, explicitly scoped followups. Refresh live
// caches between actions so killed targets and spent allowances are not reused.
bool exec::apply(const Action& action)
{
    const auto& me = turn::self();
    try {
    // Dispatch the proposal's concrete action type without issuing an unrelated
    // movement, bank or recall from a second bot controller in this turn.
    return std::visit(
        [&me](const auto& a) -> bool
        {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, Move>)
            {
                bool protected_walk = false;
                // Optional defense must never prevent the required escape walk
                // when a target or ability read becomes unavailable.
                if (a.escaping) try { protected_walk = protect_escape(); }
                catch (const std::exception&) { }
                const bool moved = nav::step(a.goals);
                if (a.escaping)
                {
                    bool defended = false;
                    if (!moved)
                        try {
                            // Failed movement can reveal blockers. Refresh before
                            // using a stationary spell, and never let a spell
                            // replace an escape step that could have succeeded.
                            turn::note_action();
                            defended = protect_escape(true);
                        } catch (const std::exception&) { }
                    bool attacked = false;
                    try {
                        // Melee can hit after walking; ranged normally cannot.
                        // The live allowance decides. If walking failed, an
                        // in-range hit is also useful. Never chase or hit a camp.
                        attacked = follow_enemy_attack();
                    } catch (const std::exception&) { }
                    return moved || protected_walk || defended || attacked;
                }
                // The move only spent the move allowance — a melee body
                // still holds a basic attack (ranged lose it to the shared
                // allowance, which can_attack reports). Spend it like the
                // tactic offer would: a champion in range outranks a
                // monster, and only the nav target's group is safe to wake.
                if (moved && me.can_attack())
                {
                    const auto& attack_tiles = turn::attack_tiles();
                    float best_health = 0;
                    bool found = false;
                    cambc::vec2 hit{};
                    for (const auto& e : visible_enemies())
                    {
                        if (!e.is_visible() || e.health() <= 0
                            || !attack_tiles.is_set(e.position())) continue;
                        if (!found || e.health() < best_health)
                        { hit = e.position(); best_health = e.health(); found = true; }
                    }
                    if (found)
                        if (const auto* enemy = turn::enemy_at(hit))
                        {
                            try { me.attack(*enemy); return true; }
                            catch (const cambc::error&) { /* tile emptied mid-turn */ }
                        }
                    if (mem.nav_pending >= 0) follow_camp_attack(mem.nav_pending);
                }
                return moved;
            }
            else if constexpr (std::is_same_v<T, Attack>)
            {
                if (!me.can_attack() || !turn::attack_tiles().is_set(a.target)) return false;
                if (a.kind == Attack::Kind::camp)
                {
                    try
                    {
                        const auto camp = cambc::vision::camp_at(a.target);
                        if (camp.is_visible() && camp.health() > 0) { me.attack(camp); return true; }
                    }
                    catch (const cambc::error&) { /* stale target: fall through to champion */ }
                }
                try
                {
                    const auto* enemy = turn::enemy_at(a.target);
                    if (enemy && enemy->is_enemy()) { me.attack(*enemy); return true; }
                }
                catch (const cambc::error&) { /* tile emptied between tick and apply */ }
                if (a.kind == Attack::Kind::champion)
                {
                    try
                    {
                        const auto camp = cambc::vision::camp_at(a.target);
                        if (camp.is_visible() && camp.health() > 0) { me.attack(camp); return true; }
                    }
                    catch (const cambc::error&) { }
                }
                return false;
            }
            else if constexpr (std::is_same_v<T, Cast>)
            {
                if (!me.can_cast() || a.slot >= 2) return false;
                const auto& abilities = turn::abilities();
                // Farming has an explicit target-group tag. Handle both unit
                // and ground casts here, then refresh every live cache before
                // considering a second action. A fizzled cast can still fall
                // back to a basic hit if the engine leaves that allowance.
                if (a.farm_group >= 0)
                {
                    bool cast = false;
                    try { cast = a.ground ? abilities[a.slot].use(a.target)
                        : abilities[a.slot].use(cambc::vision::camp_at(a.target)); }
                    catch (const cambc::error&) { }
                    turn::note_action(); // A failed attempt may also reveal a hidden obstruction.
                    const bool attacked = follow_camp_attack(a.farm_group);
                    return cast || attacked;
                }
                // Self-target shields/heals/resets are not enemies or camps.
                if (!a.ground && a.target == me.position()
                    && abilities[a.slot].valid_targets().contains(cambc::ability_target_mask::self))
                {
                    const bool reset = abilities[a.slot].is_attack_reset();
                    const bool first_hit = reset && follow_enemy_attack();
                    bool cast = false;
                    try { cast = abilities[a.slot].use(); }
                    catch (const cambc::error& error)
                    {
                        // Preserve the first hit if the OPTIONAL reset fails.
                        // A sparse error code distinguishes a rejected action
                        // from the engine simply returning false in a replay.
                        static std::uint32_t next_error_round = 0;
                        const auto round = cambc::round_number();
                        if (round >= next_error_round && cambc::fuel_remaining() >= 6000)
                        {
                            next_error_round = round + 32;
                            std::cout << "RG4_CAST r=" << round << " slot=" << mem.slot
                                      << " ability=" << static_cast<unsigned>(a.slot)
                                      << " error=" << cambc::to_string(error.code()) << '\n';
                        }
                    }
                    turn::note_action(); // Even failed casts can reveal blockers.
                    const bool second_hit = cast && reset && follow_enemy_attack();
                    return cast || first_hit || second_hit;
                }
                if (a.ground)
                {
                    // A stale or out-of-range ground tile throws; treat it
                    // like any other fizzled action, not a faulted turn.
                    try { return abilities[a.slot].use(a.target); }
                    catch (const cambc::error&) { return false; }
                }
                try
                {
                    const auto* enemy = turn::enemy_at(a.target);
                    if (!enemy)
                        throw cambc::error{ cambc::error_code::api_not_in_vision,
                            "tile emptied between tick and apply" };
                    if (!abilities[a.slot].use(*enemy))
                        return false;
                }
                catch (const cambc::error&)
                {
                    try { return abilities[a.slot].use(cambc::vision::camp_at(a.target)); }
                    catch (const cambc::error&) { return false; }
                }
                // An attack-reset cast restores the attack allowance it
                // didn't consume (GameRules §Combat) — spend it on the
                // lowest-health enemy still in range.
                if (abilities[a.slot].is_attack_reset() && me.can_attack())
                {
                    const auto& attack_tiles = turn::attack_tiles();
                    cambc::vec2 hit{};
                    float hit_hp = std::numeric_limits<float>::max();
                    for (const auto& e : visible_enemies())
                        try
                        {
                            if (!e.is_visible() || !attack_tiles.is_set(e.position())) continue;
                            const float hp = e.health();
                            if (hp > 0 && hp < hit_hp) { hit_hp = hp; hit = e.position(); }
                        }
                        catch (const cambc::error&) { continue; } // stale entity — skip this turn
                    if (hit_hp < std::numeric_limits<float>::max())
                        if (const auto* enemy = turn::enemy_at(hit))
                        {
                            try { me.attack(*enemy); }
                            catch (const cambc::error&) { }
                        }
                }
                return true;
            }
            else if constexpr (std::is_same_v<T, Channel>)
            {
                if (a.kind == Channel::Kind::recall)
                {
                    // Stunned or dead between tick and apply: a thrown
                    // error would fault the turn, a false just skips it.
                    try { me.recall(); return true; }
                    catch (const cambc::error&) { return false; }
                }
                // An enemy-locked altar converts the tribute into a lock
                // attack (GameRules §Tributing against a lock); a stale or
                // illegal altar still throws — fizzle, don't fault.
                try { me.submit_gems(a.altar); return true; }
                catch (const cambc::error&) { return false; }
            }
            else if constexpr (std::is_same_v<T, UseItem>)
            {
                // Consumables ride their own allowance; a hidden obstruction
                // or a lock that landed in between returns false cleanly.
                try { return me.consumables()[a.item].use(a.target); }
                catch (const cambc::error&) { return false; }
            }
            else if constexpr (std::is_same_v<T, Buy>)
            {
                if (a.count == 0 || !me.can_buy(a.item, a.count)) return false;
                try { me.buy(a.item, a.count); return true; }
                catch (const cambc::error&) { return false; }
            }
            else
            {
                return false; // Wait — yield is always legal.
            }
        },
        action);
    } catch (const std::exception&) { return false; }
}

// Observe the actual outcome, not just an issued command. Repeated successful
// A-B moves do not improve the best path distance; a real attack/capture does.
// Recovery is per champion and never changes the shared-memory wire format.
void exec::record_outcome(const Obs& obs, const Action& action, bool applied)
{
    const auto round = cambc::round_number();
    bool replanned = false;
    const int objective = mem.bank_pending >= 0 ? 1000 + mem.bank_pending : mem.nav_pending;
    if (std::holds_alternative<Move>(action) && objective >= 0)
    {
        const auto& step = nav::last_step();
        if (step.status == nav::StepStatus::unavailable)
            mem.journey.pause(round); // Stun/no movement allowance is not a broken route.
        else
        {
            mem.journey.observe(objective, round, step.distance_after);
            if (step.status == nav::StepStatus::moved) mem.nav_fail_count = 0;
            else ++mem.nav_fail_count;
            replanned = mem.journey.expired(round, 10) || mem.nav_fail_count >= nav_fail_limit;
        }
    }
    else if (applied || mem.state == State::capture || mem.state == State::channel
             || mem.state == State::pitstop)
    {
        // A defensive cast, a recall or a short retreat does not make the
        // same carried bag any closer to its altar. Pause that bank journey
        // rather than forgiving a repeated bank -> retreat -> bank loop.
        if (mem.bank_target >= 0 && obs.self_gems > 0)
            mem.journey.pause(round);
        else
            mem.journey.reset();
        mem.nav_fail_count = 0;
    }
    else mem.journey.pause(round);

    // A rejected attack/cast is not productive merely because it was selected.
    // In particular, repeated stale targets must not monopolise all later turns.
    if (!applied && (std::holds_alternative<Attack>(action) || std::holds_alternative<Cast>(action)))
        ++mem.failed_actions;
    else mem.failed_actions = 0;
    if (mem.failed_actions >= 6 && mem.farm_pending >= 0) replanned = true;
    if (mem.failed_actions >= 2 && mem.state == State::tactic)
    {
        // Two rejected optional actions are evidence to yield to the economic
        // job briefly. Escape defense is separate and is never disabled here.
        mem.tactic_retry_round = round + 8;
        mem.failed_actions = 0;
    }
    if (replanned)
    {
        if (mem.bank_pending >= 0)
        {
            mem.bank_blocked_until[mem.bank_pending] = round + nav_cooldown;
            // Let banking_action choose another reachable altar next turn.
            // Keep this one as the fallback if no alternative exists.
        }
        else
        {
            const int group = mem.nav_pending >= 0 ? mem.nav_pending : mem.farm_pending;
            if (group >= 0)
            {
                if (mem.unreachable_until.size() != groups().size())
                    mem.unreachable_until.assign(groups().size(), 0);
                mem.unreachable_until[group] = round + nav_cooldown;
                mem.active_target = -1;
            }
        }
        mem.journey.reset(); mem.nav_fail_count = 0; mem.failed_actions = 0;
    }

    // Sparse, searchable replay diagnostics: state/goal/action/result/fuel.
    // Legitimate waiting stays visible but cannot flood the fuel budget.
    if ((round >= mem.next_diagnostic || replanned) && cambc::fuel_remaining() >= 6000)
    {
        mem.next_diagnostic = round + 32;
        static constexpr const char* states[] = {
            "boot", "channel", "tactic", "pitstop", "bank", "travel", "clear", "capture", "haul", "idle"
        };
        std::cout << "RG4 r=" << round << " slot=" << mem.slot
                  << " state=" << states[static_cast<unsigned>(mem.state)]
                  << " target=" << mem.active_target << " nav=" << mem.nav_pending
                  << " bank=" << mem.bank_pending << " action=" << action.index()
                  << " ok=" << applied << " replan=" << replanned
                  << " hp=" << obs.self_health << " gems=" << obs.self_gems
                  << " fuel=" << cambc::fuel_remaining() << '\n';
    }
}

// One-time statics that are pure functions of the map and the draft blob.
// Each costs ~30-45k fuel on the big maps; building one per light turn
// (and only with clear headroom) keeps the spike off any single turn
// without changing a single decision input.
void exec::warm_statics()
{
    // ~55k covers the largest single cache (tactic geography on the big
    // maps); a warm item only runs when the turn's real work already fit
    // inside 45k, so the total can never reach the 100k line.
    constexpr std::uint64_t headroom = 75000;
    static int next_stop = 1;
    while (cambc::fuel_remaining() >= headroom)
    {
        bool did = false;
        if (next_stop <= 2 && opening_stop(next_stop) >= 0) { ++next_stop; did = true; }
        else if (next_stop <= 2) { ++next_stop; }
        if (!did) break;
    }
    if (cambc::fuel_remaining() >= headroom) tactic::groups();
    if (cambc::fuel_remaining() >= headroom) tactic::altars();
    if (cambc::fuel_remaining() >= headroom) turn::altars();
    if (cambc::fuel_remaining() >= headroom) econ::warm();
}

