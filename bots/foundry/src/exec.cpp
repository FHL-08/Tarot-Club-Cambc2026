// exec — the runtime FSM replacing the starter decision list
// (docs/strategy.md §RUNTIME). Guard order inside tick:
//   channel-continuation → tactic::offer → capture-stay → pitstop →
//   bank → route step (contested/respawning filtered) → haul → idle.
// The stub planner is rebuilt from ledger::ready_round every turn — greedy
// nearest-ready over a deterministic slot partition — so FSM state is
// derived from (Obs, Plan, ledger), not stored. The statics below feed the
// transition log and the L1 instrumentation only; each is a pure function
// of the observation history.
#include <cambc.h>
#include "exec.h"
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

// Retreat floor: the share of max hp that ends a sortie — the `dying`
// pitstop trigger and the heal-to-need release both price off it. A
// release reserve below it would discharge a body the dying check
// re-traps the very next turn, and a sortie budgeted to a lower floor
// lands its last hit already past the bail line.
constexpr float retreat_floor = 0.40f;

// Shop trip: a full emblem (five 30g shards — Power counts as six shards'
// worth) plus a lock/crystal restock sends an unthreatened, light-bagged
// champion home to spend — routes otherwise
// never revisit spawn except on low HP, leaving hundreds banked (#34).
// Tavi shops in the field (GameRules: Tavi buys shards anywhere), so it
// never takes the trip.
constexpr std::uint32_t shop_trip_gold = 180;

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
        for (const auto& group : cambc::map::camp_groups())
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

int group_of(cambc::vec2 p)
{
    return group_at()[static_cast<std::size_t>(p.y) * cambc::map::width() + p.x];
}

int my_slot()
{
    if (mem.slot >= 0) return mem.slot;
    const auto allies = cambc::champions(cambc::team::ally);
    mem.allies = static_cast<int>(allies.size());
    const auto& me = cambc::self();
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
        const auto& me = cambc::self();
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

const CampObs* seen(const Obs& obs, int g)
{
    for (const auto& c : obs.camps)
        if (c.group == g) return &c;
    return nullptr;
}

bool unreachable(int g, std::uint32_t round)
{
    return g < static_cast<int>(mem.unreachable_until.size())
        && mem.unreachable_until[static_cast<std::size_t>(g)] > round;
}



// Identical legality mask to nav.cpp's blocked_tiles — goal seeds must be
// masked with it because an occupied goal tile would still seed the descent.
cambc::bitboard blocked_tiles()
{
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(cambc::team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

std::int64_t dist_to(const GroupGeo& g, cambc::vec2 p)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto tile : g.tile_list) best = std::min(best, p.chebyshev_distance(tile));
    return best;
}

// Grouped-opening stop order (issue #38): every body must derive the SAME
// chain — wire.h is full, so no shared-memory consensus can arbitrate a
// disagreement. All inputs are static map data (camp_groups, member base
// stats, a hard-terrain distance field from our spawn region) plus the
// fixed ally count and this body's own static kit, and ties break to the
// lowest group index, so the list is identical across instances without
// communication. Elks and monoliths are excluded: elks are haulable
// single units, not group fights, and monolith tiles never open up.
const std::vector<int>& opening_stops()
{
    static const std::vector<int> stops = [] {
        const auto& geos = groups();
        const auto raw = cambc::map::camp_groups();
        const auto hard = cambc::map::walls() | cambc::map::holes()
            | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
        // Whole-team clear estimate: the opening bodies fight together,
        // so one body's base_stats scaled by the roster size prices the
        // pack's health in team-damage rounds.
        const float team_dps = my_dps() * std::max(1, mem.allies);
        std::vector<char> used(geos.size());
        std::vector<int> out;
        cambc::bitboard origin = cambc::map::spawn_region(cambc::team::ally);
        for (int s = 0; s < 3; ++s)
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
            if (best < 0) break;
            used[static_cast<std::size_t>(best)] = 1;
            out.push_back(best);
            origin = geos[best].tiles;
        }
        return out;
    }();
    return stops;
}

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
std::vector<cambc::camp> visible_camps()
{
    std::vector<cambc::camp> out;
    for (const auto& tile : cambc::vision::camp_tiles().positions())
        try
        {
            const auto c = cambc::vision::camp_at(tile);
            if (c.is_visible()) out.push_back(c);
        }
        catch (const cambc::error&) { }
    return out;
}

// Same mask-vs-entity gap for champions: a teleporter or fresh corpse can
// vanish between champion_tiles() and champion_at().
std::vector<cambc::champion> visible_enemies()
{
    std::vector<cambc::champion> out;
    for (const auto& tile : cambc::vision::champion_tiles(cambc::team::enemy).positions())
        try
        {
            const auto e = cambc::vision::champion_at(tile);
            if (e.is_visible()) out.push_back(e);
        }
        catch (const cambc::error&) { }
    return out;
}

std::optional<exec::Action> attack_in_range(const cambc::bitboard& attack_tiles,
    int only_group = -1, bool harmless_only = false)
{
    std::vector<char> dangerous;
    if (harmless_only)
    {
        dangerous.assign(groups().size(), 0);
        for (const auto& c : visible_camps())
            if (c.health() > 0 && c.strength() > 0)
            {
                const int g = group_of(c.spawn_position());
                if (g >= 0) dangerous[static_cast<std::size_t>(g)] = 1;
            }
    }
    bool found = false;
    cambc::vec2 best{};
    float best_health = 0;
    for (const auto& c : visible_camps())
    {
        if (c.health() <= 0 || !attack_tiles.is_set(c.position())) continue;
        const int g = group_of(c.spawn_position());
        if (g < 0 || (only_group >= 0 && g != only_group) || (only_group < 0 && !mine(g))
            || (!dangerous.empty() && dangerous[static_cast<std::size_t>(g)]))
            continue;
        const auto p = c.position();
        if (!found || c.health() < best_health
            || (c.health() == best_health && (p.x < best.x || (p.x == best.x && p.y < best.y))))
        {
            found = true;
            best = p;
            best_health = c.health();
        }
    }
    if (!found) return std::nullopt;
    return exec::Attack{ best, exec::Attack::Kind::camp };
}

// A ready damage/cc payload on the engaged group out-damages a basic
// swing, and stunned members skip their retaliation — the planner's kit
// dps already prices this in (burst amortized over the 8-round cooldown),
// so a runtime that never casts clears slower than the gate believes.
// Restricted to the active target: focused-fire, never wake a bystander.
std::optional<exec::Cast> camp_cast(int g, std::uint32_t round)
{
    const auto& me = cambc::self();
    if (!me.can_cast()) return std::nullopt;
    // abilities() returns its array by value — a reference bound to
    // abilities()[slot] dangles off a destroyed temporary and reads a
    // garbage owner handle (engine error 97) on the next query.
    const auto abilities = me.abilities();
    for (std::uint8_t slot = 0; slot < 2; ++slot)
    {
        if (mem.cast_banned[slot]) continue;
        const auto& ab = abilities[slot];
        if (!ab.is_ready() || (!ab.is_damage() && !ab.is_aoe_damage() && !ab.is_cc()))
            continue;
        const bool ground = ab.can_target_ground();
        if (!ab.can_target_camps() && !ground) continue;
        cambc::bitboard tiles{ cambc::uninitialized };
        ab.mask_into(tiles);
        for (const auto& c : visible_camps())
            if (c.health() > 0 && group_of(c.spawn_position()) == g && tiles.is_set(c.position()))
            {
                mem.last_cast_slot = slot;
                mem.last_cast_round = round;
                return exec::Cast{ slot, c.position(), ground };
            }
    }
    return std::nullopt;
}

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

exec::Action exec::tick(const Obs& obs, const Plan& plan)
{
    try {
    using cs = cambc::champion_state;
    const auto st = obs.self_state;
    my_slot();
    bind_plan(plan);
    mem.nav_pending = -1;

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
                if (cambc::self().abilities()[s].is_ready())
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

    // Tactic modules make their own live reads (buffs, consumables, a
    // recaller query per enemy) — a stale read there must not fault the
    // turn, only skip the offer. std::exception, not just cambc::error:
    // a module's own bad_variant_access/out_of_range would otherwise
    // escape to the whole-tick catch and cost the turn, not the offer.
    try
    {
        if (const auto offer = tactic::offer(obs))
            return done(State::tactic, *offer, obs);
    }
    catch (const std::exception&) { }

    const auto& geos = groups();
    const cambc::vec2 pos = obs.self_pos;
    const int here = group_of(pos);
    const bool at_spawn = cambc::map::spawn_region(cambc::team::ally).is_set(pos);

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
        const int g = group_of(c.spawn_position());
        if (g < 0) continue;
        auto& left = mem.camp_left[static_cast<std::size_t>(g)];
        if (left.round != static_cast<std::int32_t>(obs.round))
        {
            left.hp = 0;
            left.dps = 0;
            left.round = static_cast<std::int32_t>(obs.round);
        }
        if (c.health() > 0)
        {
            left.hp += c.health();
            left.dps += c.strength();
        }
    }

    // Lethal-threat proxy: a visible enemy champion inside maximum attack
    // reach (3). Enemy camps aren't observable here, so plain low-HP stays
    // the floor and the threat case lifts it — recall channels for 5 turns
    // under fire, so the earlier exit is what survives the channel.
    bool threat = false;
    for (const auto& e : obs.enemies)
        if (pos.chebyshev_distance(e.position) <= 3) { threat = true; break; }
    const bool dying = obs.self_health < obs.self_max_health * retreat_floor;
    const bool unsafe = dying || (threat && obs.self_health < obs.self_max_health * 0.6f);

    // A recall we emitted that never shows in champion_state was cancelled
    // by damage/stun between turns; a completed one lands on spawn.
    if (mem.recall_pending)
    {
        mem.recall_pending = false;
        if (at_spawn) mem.recall_attempts = 0;
        else ++mem.recall_attempts;
    }
    if (!unsafe) mem.recall_attempts = 0; // threat gone — retry freely next time

    // Standing on own group's reward tiles: an uncontested stay completes
    // the capture (passive — free hit meanwhile), a contested one holds
    // only inside the capture-time grace window while it's safe to.
    if (here >= 0 && mine(here))
    {
        if (const CampObs* c = seen(obs, here); c && std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
        {
            const bool contested = enemy_on(here, obs);
            if (!contested)
            {
                // Low HP alone doesn't break the stand — the group's own
                // camps are dead and strangers sleep two rounds after we
                // stop poking — but an enemy champion in reach does.
                if (!(threat && obs.self_health < obs.self_max_health * 0.6f))
                {
                    const auto attack_tiles = cambc::self().attack_tiles();
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
                    const auto attack_tiles = cambc::self().attack_tiles();
                    if (const auto hit = attack_in_range(attack_tiles, -1, true))
                        return done(State::capture, *hit, obs);
                    return done(State::capture, Wait{}, obs);
                }
            }
            // Timed out or threatened — contested camp is excluded below ("else move on").
        }
    }
    if (mem.contested_group != here) { mem.contested_group = -1; mem.contested_since = -1; }

    // Pitstop: badly hurt, or hurt with an enemy in reach. Damage
    // interrupts recall (GameRules §Channels), so while a live attacker
    // can still reach us we walk home — out of range the channel is safe.
    // 'exposed' only sees visible attackers; a bush-hidden one still breaks
    // the channel every turn, so after max_recall_retries cancels we walk.
    // The retreat arm is sticky: a champion already fleeing stays on the
    // retreat below 75% — re-checking `unsafe` each turn flickers between
    // TRAVEL and PITSTOP when a threat pops in and out of vision.
    // Heal-to-need: a return sortie's spendable budget is capped by the
    // retreat floor, so healing past (expected retaliation + floor) buys
    // nothing — spawn heals to full at turn end (GameRules §Recalling),
    // so every surplus point waited on costs a whole turn of commute on a
    // camp the last sortie already chewed. Champion scares and non-sortie
    // exits heal full: the commute itself stays dangerous.
    float heal_target = obs.self_max_health * 0.9f;
    if (mem.fled_group >= 0 && mem.fled_group < static_cast<int>(geos.size()))
    {
        const auto& left = mem.camp_left[static_cast<std::size_t>(mem.fled_group)];
        const float hp_left = left.hp >= 0 ? left.hp : geos[mem.fled_group].health;
        const float dps_left = left.hp >= 0 ? left.dps : geos[mem.fled_group].dps;
        if (hp_left > 0)
            heal_target = std::min(heal_target, obs.self_max_health * retreat_floor
                + dps_left * std::max(1.0f, hp_left / my_dps()) * 1.25f);
    }
    // Pitstop release: mid-retreat stays sticky below 75% (re-checking
    // `unsafe` flickers when a threat pops in and out of vision); at spawn,
    // the heal target above.
    const bool recovered = mem.state != State::pitstop
        || (at_spawn ? obs.self_health >= heal_target
                     : obs.self_health >= obs.self_max_health * 0.75f);
    // Shop trips reuse the pitstop's recall-or-walk-home arm but are not
    // retreats: !threat keeps them out of the sortie watchdog, and they
    // leave fled_group unset so the heal target stays champion-priced.
    // A healthy shopper at spawn is already `recovered`, so the Wait never
    // holds it; recall lands at turn start and bot.cpp's econ::maybe_buy
    // spends the gold before exec runs — next turn gold < the threshold
    // and the trip ends on its own.
    const bool shop_trip = !threat && !at_spawn
        && obs.self_gold >= shop_trip_gold && obs.self_gems <= 10
        && cambc::self().champion_name() != cambc::champion_type::tavi;
    if (unsafe || !recovered || shop_trip)
    {
        // fled_group prices the heal target against the pack that dealt
        // the damage — the group we're standing in, not the route
        // destination. Off the tiles (ranged ring, mid-commute) the active
        // target stays the better guess.
        if (mem.state != State::pitstop && !shop_trip)
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
        bool exposed = threat;
        for (const auto& c : visible_camps())
            if (!exposed && c.health() > 0 && c.strength() > 0
                && pos.chebyshev_distance(c.position()) <= c.attack_range())
                exposed = true;
        if (!exposed && mem.recall_attempts < max_recall_retries)
        {
            mem.recall_pending = true;
            return done(State::pitstop, Channel{ Channel::Kind::recall }, obs);
        }
        return done(State::pitstop, Move{ cambc::map::spawn_region(cambc::team::ally) }, obs);
    }

    const auto blocked = blocked_tiles();

    // Bank trigger: with a compiled route the plan's role bag_cap adds an
    // early divert threshold on top of econ's; when it fires, the route's
    // next spliced ALTAR stop is the preferred tribute point.
    int preferred = -1; // route's next ALTAR stop, when the cap triggered it
    const bool over_cap = mem.have_route && mem.bag_cap && obs.self_gems >= mem.bag_cap;
    if (over_cap)
    {
        const auto altars = cambc::map::altars().positions();
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
        cambc::vec2 nearest{}, locked{};
        std::int64_t nearest_d = std::numeric_limits<std::int64_t>::max();
        std::int64_t locked_d = std::numeric_limits<std::int64_t>::max();
        cambc::bitboard rings;
        const auto altars = cambc::map::altars().positions();
        // A locked route altar falls back to the nearest unlocked one.
        if (preferred >= 0 && cambc::vision::has_altar_lock(altars[preferred])) preferred = -1;
        for (std::size_t a = 0; a < altars.size(); ++a)
        {
            const auto altar = altars[a];
            // The route's spliced ALTAR stop is the planned tribute point:
            // while it stands unlocked, other altars don't compete.
            if (preferred >= 0 && static_cast<std::int32_t>(a) != preferred) continue;
            // An enemy lock turns the tribute attempt into a lock attack
            // (GameRules §Tributing against a lock) — taking the nearest
            // locked altar still clears it, while our own locks never block
            // us and unlocked altars always win the pick.
            const bool enemy_locked = cambc::vision::has_altar_lock(altar)
                && cambc::vision::altar_lock_at(altar).is_enemy();
            rings.draw_chebyshev(altar, cambc::constants::submit_gems_range);
            if (const auto d = pos.chebyshev_distance(altar); !enemy_locked && d < nearest_d)
            {
                nearest_d = d;
                nearest = altar;
            }
            else if (enemy_locked && d < locked_d)
            {
                locked_d = d;
                locked = altar;
            }
        }
        if (nearest_d <= cambc::constants::submit_gems_range)
            return done(State::bank, Channel{ Channel::Kind::tribute, nearest }, obs);
        if (locked_d <= cambc::constants::submit_gems_range)
            return done(State::bank, Channel{ Channel::Kind::tribute, locked }, obs);
        rings &= ~blocked;
        if (!rings.is_empty()) // every approach tile occupied: bank later
            return done(State::bank, Move{ std::move(rings) }, obs);
    }

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
        else if (ledger::ready_round(g) > static_cast<std::int32_t>(obs.round))
            return;
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

    const auto scan = [&](bool relaxed) {
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
        // The modulo partition under the route: unrouted leftovers (packs
        // above the solo floor, monolith-adjacent) still have an owner to
        // grind them; without a route this IS the whole job.
        for (std::size_t g = 0; g < geos.size(); ++g)
            if (mine(static_cast<int>(g))) consider(static_cast<int>(g), -1, relaxed);
    };

    // Grouped opening (issue #38): while the chain has an unfinished stop
    // it overrides the route scan — the first gem packs are cleared
    // together, which is where the platform openings were being lost.
    // A stop is finished once the ledger marks it captured: a visible
    // respawning state, or a captured/dead ready_at; an unseen stop whose
    // ready_at still says "alive" is believed live and still owed a visit.
    if (obs.round <= 90)
    {
        const auto open_stop = [&](int after) -> int {
            const auto& chain = opening_stops();
            for (std::size_t i = static_cast<std::size_t>(after) + 1; i < chain.size(); ++i)
            {
                const int g = chain[i];
                if (const CampObs* c = seen(obs, g))
                {
                    if (std::holds_alternative<cambc::map::camp_group_respawning>(c->state))
                        continue;
                    return static_cast<int>(i);
                }
                const int ready = ledger::ready_round(g);
                if (ready == ledger::never || ready > static_cast<int>(obs.round))
                    continue;
                return static_cast<int>(i);
            }
            return -1;
        };
        for (int oi = open_stop(-1); oi >= 0; oi = open_stop(oi))
        {
            const int g = opening_stops()[oi];
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
                    const auto allies = cambc::champions(cambc::team::ally);
                    const auto& me = cambc::self();
                    for (std::size_t i = 0; i < allies.size() && stands; ++i)
                    {
                        if (allies[i] == me) continue;
                        const auto d = dist_free(allies[i].position());
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
    // Desperation pass: a body that owns zero fit gem camps (elkring,
    // rows — every pack above the solo floor) can't sit on 0-gem gold or
    // idle forever; the tribute race is paid in gems, so it grinds the
    // least-bad trap. Mixed-map bodies always own some fit supply — a
    // respawn lull never reads as desperation.
    desperate = fit_gem_supply == 0;
    if (desperate) { scanned = 0; scan(true); }

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
        if (const auto cast = camp_cast(target, obs.round))
            return done(State::clear, *cast, obs);
        const auto attack_tiles = cambc::self().attack_tiles();
        if (const auto hit = attack_in_range(attack_tiles, target))
            return done(State::clear, *hit, obs);
        cambc::bitboard goals;
        const auto range = static_cast<float>(cambc::self().attack_range());
        for (const auto& member : visible_camps())
            if (member.health() > 0 && group_of(member.spawn_position()) == target)
                goals.draw_chebyshev(member.position(), range);
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
        if (haul < 0 || ready < haul_ready || (ready == haul_ready && d < haul_dist))
        {
            haul = static_cast<int>(g);
            haul_ready = ready;
            haul_dist = d;
        }
    }
    if (haul >= 0)
    {
        cambc::bitboard ring;
        for (const auto tile : geos[haul].tile_list) ring.draw_chebyshev(tile, 1);
        ring &= ~geos[haul].tiles & ~blocked;
        if (!ring.is_set(pos) && !ring.is_empty())
        {
            mem.nav_pending = haul;
            return done(State::haul, Move{ std::move(ring) }, obs);
        }
        return done(State::haul, Wait{}, obs);
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

bool exec::apply(const Action& action)
{
    const auto& me = cambc::self();
    try {
    return std::visit(
        [&me](const auto& a) -> bool
        {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, Move>)
            {
                const bool moved = nav::step(a.goals);
                // A route/haul Move that can't descend at all means the
                // goal is unreachable from here — nav_fail_limit turns in
                // a row blacklists the group for nav_cooldown rounds so the
                // route falls through to the next-best stop.
                if (mem.nav_pending >= 0)
                {
                    // Consecutive failures accumulate across targets, not just
                    // per group — a route ping-ponging between two unreachable
                    // camps would otherwise reset the count every turn and
                    // orbit forever.
                    if (moved)
                        mem.nav_fail_count = 0;
                    else
                        ++mem.nav_fail_count;
                    if (mem.nav_fail_count >= nav_fail_limit)
                    {
                        if (mem.unreachable_until.size() != groups().size())
                            mem.unreachable_until.assign(groups().size(), 0);
                        mem.unreachable_until[static_cast<std::size_t>(mem.nav_pending)]
                            = cambc::round_number() + nav_cooldown;
                        mem.nav_fail_count = 0;
                    }
                }
                // The move only spent the move allowance — a melee body
                // still holds a basic attack (ranged lose it to the shared
                // allowance, which can_attack reports). Spend it like the
                // tactic offer would: a champion in range outranks a
                // monster, and only the nav target's group is safe to wake.
                if (moved && me.can_attack())
                {
                    const auto attack_tiles = me.attack_tiles();
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
                    {
                        try { me.attack(cambc::vision::champion_at(hit)); return true; }
                        catch (const cambc::error&) { /* tile emptied mid-turn */ }
                    }
                    if (mem.nav_pending >= 0)
                    {
                        for (const auto& c : visible_camps())
                        {
                            if (c.health() <= 0 || !attack_tiles.is_set(c.position())) continue;
                            if (group_of(c.spawn_position()) != mem.nav_pending) continue;
                            try { me.attack(c); return true; }
                            catch (const cambc::error&) { return moved; }
                        }
                    }
                }
                return moved;
            }
            else if constexpr (std::is_same_v<T, Attack>)
            {
                if (!me.can_attack() || !me.attack_tiles().is_set(a.target)) return false;
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
                    const auto enemy = cambc::vision::champion_at(a.target);
                    if (enemy.is_enemy()) { me.attack(enemy); return true; }
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
                if (a.ground)
                {
                    // A stale or out-of-range ground tile throws; treat it
                    // like any other fizzled action, not a faulted turn.
                    try { return me.abilities()[a.slot].use(a.target); }
                    catch (const cambc::error&) { return false; }
                }
                try
                {
                    return me.abilities()[a.slot].use(cambc::vision::champion_at(a.target));
                }
                catch (const cambc::error&)
                {
                    try { return me.abilities()[a.slot].use(cambc::vision::camp_at(a.target)); }
                    catch (const cambc::error&) { return false; }
                }
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
