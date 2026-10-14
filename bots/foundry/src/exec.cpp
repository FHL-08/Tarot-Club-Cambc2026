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

const char* state_name(State s)
{
    switch (s)
    {
    case State::boot: return "BOOT";
    case State::channel: return "CHANNEL";
    case State::tactic: return "TACTIC";
    case State::pitstop: return "PITSTOP";
    case State::bank: return "BANK";
    case State::travel: return "TRAVEL";
    case State::clear: return "CLEAR";
    case State::capture: return "CAPTURE";
    case State::haul: return "HAUL";
    case State::idle: return "IDLE";
    }
    return "?";
}

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

// Per-champion memory: the decision is re-derived each turn; these only
// power the transition log plus the contested/capture windows.
struct Mem {
    State state = State::boot;
    int slot = -1;
    int allies = 0;
    int contested_group = -1;
    std::int32_t contested_since = -1;
    int stand_group = -1;
    std::int32_t stand_since = -1;
    bool overrun_logged = false;
    int nav_pending = -1; // route/haul group the emitted Move steers to
    int nav_fail_group = -1;
    int nav_fail_count = 0;
    std::vector<std::uint32_t> unreachable_until; // per-group blacklist expiry
    int recall_attempts = 0;
    bool recall_pending = false; // recall issued, outcome not yet observed
};
Mem mem;

struct GroupGeo {
    cambc::bitboard tiles;
    std::vector<cambc::vec2> tile_list;
    bool monolith = false; // 20k hp at 200 dmg — a planner decision (#8), not a stub stop
};

// Camp layouts are immutable — the host layout query runs once.
const std::vector<GroupGeo>& groups()
{
    static const std::vector<GroupGeo> cache = [] {
        std::vector<GroupGeo> out;
        for (const auto& group : cambc::map::camp_groups())
        {
            GroupGeo geo;
            group.spawn_tiles_into(geo.tiles);
            geo.tile_list = geo.tiles.positions();
            for (const auto& member : group)
                geo.monolith = geo.monolith || member.is_monolith();
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

// Stub partition (the planner owns the real assignment, #8): camp indices
// are dealt round-robin across champion body slots, g % stride == slot %
// stride with stride = min(allies, groups). Every group has exactly one
// owner while allies ≤ groups; extra bodies share the smallest indices.
bool mine(int g)
{
    const int stride = std::max(1, std::min(mem.allies, static_cast<int>(groups().size())));
    return g % stride == mem.slot % stride;
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
std::optional<exec::Action> attack_in_range(const cambc::bitboard& attack_tiles,
    int only_group = -1, bool harmless_only = false)
{
    std::vector<char> dangerous;
    if (harmless_only)
    {
        dangerous.assign(groups().size(), 0);
        for (const auto& c : cambc::vision::camps())
            if (c.health() > 0 && c.strength() > 0)
            {
                const int g = group_of(c.spawn_position());
                if (g >= 0) dangerous[static_cast<std::size_t>(g)] = 1;
            }
    }
    bool found = false;
    cambc::vec2 best{};
    float best_health = 0;
    for (const auto& c : cambc::vision::camps())
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

exec::Action done(State next, const exec::Action& action, const Obs& obs)
{
    if (next != mem.state)
    {
        std::cout << "FSM " << mem.slot << ' ' << state_name(mem.state) << "->" << state_name(next)
                  << " r" << obs.round << '\n';
        mem.state = next;
    }
    return action;
}

} // namespace

exec::Action exec::tick(const Obs& obs, const Plan& plan)
{
    (void)plan; // stub route comes from the ledger, not the compiled stops (#8)
    using cs = cambc::champion_state;
    const auto st = obs.self_state;

    my_slot(); // before any done() — the transition log prints the real slot
    mem.nav_pending = -1;

    // Mid-channel turns can't act — tribute/recall/teleport continue alone.
    if (st.contains(cs::submitting_gems) || st.contains(cs::recalling) || st.contains(cs::teleporting)
        || (!st.contains(cs::can_move) && !st.contains(cs::can_attack)))
        return done(State::channel, Wait{}, obs);

    if (const auto offer = tactic::offer(obs))
        return done(State::tactic, *offer, obs);

    const auto& geos = groups();
    const cambc::vec2 pos = obs.self_pos;
    const int here = group_of(pos);
    const bool at_spawn = cambc::map::spawn_region(cambc::team::ally).is_set(pos);

    // Lethal-threat proxy: a visible enemy champion inside maximum attack
    // reach (3). Enemy camps aren't observable here, so plain low-HP stays
    // the floor and the threat case lifts it — recall channels for 5 turns
    // under fire, so the earlier exit is what survives the channel.
    bool threat = false;
    for (const auto& e : obs.enemies)
        if (pos.chebyshev_distance(e.position) <= 3) { threat = true; break; }
    const bool dying = obs.self_health < obs.self_max_health * 0.45f;
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
                if (mem.stand_group != here) { mem.stand_group = here; mem.stand_since = obs.round; mem.overrun_logged = false; }
                // L1: an uncontested capture resolves in capture_time+1; a
                // longer stand means progress reset we couldn't see.
                if (!mem.overrun_logged
                    && static_cast<std::int32_t>(obs.round) - mem.stand_since > cambc::constants::capture_time + 1)
                {
                    mem.overrun_logged = true;
                    std::cout << "FSM_L1 capture-overrun g" << here << " r" << obs.round << '\n';
                }
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
    if (mem.stand_group != here) { mem.stand_group = -1; mem.stand_since = -1; }
    if (mem.contested_group != here) { mem.contested_group = -1; mem.contested_since = -1; }

    // Pitstop: badly hurt, or hurt with an enemy in reach. Damage
    // interrupts recall (GameRules §Channels), so while a live attacker
    // can still reach us we walk home — out of range the channel is safe.
    // 'exposed' only sees visible attackers; a bush-hidden one still breaks
    // the channel every turn, so after max_recall_retries cancels we walk.
    if (unsafe || (mem.state == State::pitstop && at_spawn && obs.self_health < obs.self_max_health * 0.9f))
    {
        if (at_spawn) return done(State::pitstop, Wait{}, obs); // turn end heals to full
        bool exposed = threat;
        for (const auto& c : cambc::vision::camps())
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

    // Bank once the bag crosses the econ cap or the deadline forces it.
    if (econ::should_tribute(obs))
    {
        cambc::vec2 nearest{}, locked{};
        std::int64_t nearest_d = std::numeric_limits<std::int64_t>::max();
        std::int64_t locked_d = std::numeric_limits<std::int64_t>::max();
        cambc::bitboard rings;
        for (const auto altar : cambc::map::altars().positions())
        {
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

    // Route step: greedy nearest-ready over the partition; a confirmed
    // reward outranks a believed-live camp at any distance.
    int target = -1;
    bool target_reward = false;
    std::int64_t target_cost = 0;
    for (std::size_t g = 0; g < geos.size(); ++g)
    {
        if (!mine(static_cast<int>(g)) || geos[g].monolith
            || unreachable(static_cast<int>(g), obs.round))
            continue;
        const CampObs* c = seen(obs, static_cast<int>(g));
        bool reward = false;
        std::int64_t cost;
        if (c)
        {
            if (std::holds_alternative<cambc::map::camp_group_respawning>(c->state)) continue;
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
            {
                if (enemy_on(static_cast<int>(g), obs)) continue; // contested, we hold nothing: move on
                auto free_tiles = geos[g].tiles & ~blocked;
                if (free_tiles.is_empty()) continue;              // ally already covers it
                reward = true;
            }
            cost = dist_to(geos[g], pos);
        }
        else
        {
            if (ledger::ready_round(static_cast<int>(g)) > static_cast<int>(obs.round)) continue;
            cost = dist_to(geos[g], pos);
        }
        const bool better = target < 0 || (reward && !target_reward)
            || (reward == target_reward && cost < target_cost);
        if (better) { target = static_cast<int>(g); target_reward = reward; target_cost = cost; }
    }

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
        const auto attack_tiles = cambc::self().attack_tiles();
        if (const auto hit = attack_in_range(attack_tiles, target))
            return done(State::clear, *hit, obs);
        cambc::bitboard goals;
        const auto range = static_cast<float>(cambc::self().attack_range());
        for (const auto& member : cambc::vision::camps())
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
        if (!mine(static_cast<int>(g)) || geos[g].monolith
            || unreachable(static_cast<int>(g), obs.round))
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
    // belief alone never flags. Unreachable groups are legitimately idle.
    for (std::size_t g = 0; g < geos.size(); ++g)
    {
        if (!mine(static_cast<int>(g)) || geos[g].monolith
            || unreachable(static_cast<int>(g), obs.round))
            continue;
        const CampObs* c = seen(obs, static_cast<int>(g));
        if (c && std::holds_alternative<cambc::map::camp_group_alive>(c->state)
            && !enemy_on(static_cast<int>(g), obs))
        {
            std::cout << "FSM_L1 idle-work g" << g << " r" << obs.round << '\n';
            break;
        }
    }
    return done(State::idle, Wait{}, obs);
}

bool exec::apply(const Action& action)
{
    const auto& me = cambc::self();
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
                    if (moved)
                    {
                        mem.nav_fail_group = -1;
                        mem.nav_fail_count = 0;
                    }
                    else if (mem.nav_fail_group == mem.nav_pending)
                    {
                        ++mem.nav_fail_count;
                    }
                    else
                    {
                        mem.nav_fail_group = mem.nav_pending;
                        mem.nav_fail_count = 1;
                    }
                    if (mem.nav_fail_count >= nav_fail_limit)
                    {
                        if (mem.unreachable_until.size() != groups().size())
                            mem.unreachable_until.assign(groups().size(), 0);
                        mem.unreachable_until[static_cast<std::size_t>(mem.nav_pending)]
                            = cambc::round_number() + nav_cooldown;
                        mem.nav_fail_group = -1;
                        mem.nav_fail_count = 0;
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
                me.buy(a.item, a.count);
                return true;
            }
            else
            {
                return false; // Wait — yield is always legal.
            }
        },
        action);
}
