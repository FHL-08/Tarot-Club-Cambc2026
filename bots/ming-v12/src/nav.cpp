// Distance-field descent with hidden-occupant retries — the seam declared
// in nav.h. A failed attempt can reveal a new wall of units, so its retry
// needs a fresh distance field rather than merely a different destination.
#include "nav.h"
#include "turn.h"
#include <cstdint>

namespace {

nav::StepResult last_result;

// The legality mask the engine's own move_towards builds: terrain plus all
// visible occupants, self cleared so it cannot wall itself in.
cambc::bitboard blocked_tiles()
{
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(cambc::team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(turn::self().position(), false);
    return blocked;
}

// A false move reveals the hidden occupant for the rest of the round and
// preserves the move allowance, so each retry is legal — bounded because
// the fog could hide more occupants than a turn can route around.
constexpr int max_retries = 3;
// Keep time for the executor's follow-up action and shared-memory publication;
// choosing another route is optional once this turn's budget runs short.
constexpr std::uint64_t retry_fuel_reserve = 15000;

} // namespace

namespace nav {

// Let the farming/banking watchdog inspect path progress without doing a
// second breadth-first search or treating a successful sideways step as work.
const StepResult& last_step() { return last_result; }

// Spend at most this turn's movement on progress toward a farming, banking,
// or recovery goal. A distance field handles walls; bounded retries handle
// newly revealed blockers. False means no move succeeded, not task completion.
bool step(const cambc::bitboard& goals)
{
    last_result = {};
    const auto& me = turn::self();
    const auto origin = me.position();
    // Callers define the acceptable destination area. In particular, an altar
    // ring is a set of equally useful banking positions, not a patrol route.
    if (goals.is_set(origin))
    {
        last_result = {StepStatus::arrived, 0, 0, 0};
        return false;
    }
    if (goals.is_empty())
    {
        last_result.status = StepStatus::blocked;
        return false;
    }
    if (!me.can_move() || cambc::fuel_remaining() < retry_fuel_reserve)
        return false;

    // Reuse ordinary turn reads initially; only a failed move requires live
    // visibility queries because it may have revealed a previously hidden unit.
    auto closed = turn::blocked_tiles();

    // BFS out from the goals until our tile is reached; goal tiles are
    // lifted out of the walls so occupied ones still seed the descent.
    cambc::bitboard self_tile;
    self_tile.set(origin);
    cambc::distance_field field;
    cambc::bitboard visited;

    const auto speed = static_cast<std::uint16_t>(me.base_stats().base_move_speed);
    for (int retries = 0; retries <= max_retries; ++retries)
    {
        last_result.retries = retries;
        if (cambc::fuel_remaining() < retry_fuel_reserve || !me.can_move())
        {
            last_result.status = StepStatus::unavailable;
            return false;
        }
        // Recomputing is essential: simply blacklisting a blocked tile while
        // keeping the old field can reject every valid detour around that tile.
        field.compute_to(closed & ~goals, goals, self_tile, visited);
        auto best = origin;
        auto best_distance = field.get(best);
        if (retries == 0) last_result.distance_before = best_distance;
        last_result.distance_after = best_distance;
        for (const auto tile : closed.reachable_tiles(origin, speed).positions())
            if (field.get(tile) < best_distance)
            {
                best = tile;
                best_distance = field.get(tile);
            }
        if (best == origin)
        {
            last_result.status = StepStatus::blocked;
            return false;
        }
        // A false return means a hidden occupant — legal, retryable. A
        // thrown error means the destination was known-illegal all along:
        // the hand-built mask misses an occupant class the engine counts
        // (seen as error 22 in lanes). Either way mark the tile and retry
        // alternatives — the alternative is faulting the whole turn.
        try
        {
            if (me.move_absolute(best))
            {
                turn::note_moved();
                last_result.status = StepStatus::moved;
                last_result.distance_after = best_distance;
                return true;
            }
        }
        catch (const cambc::error&) { }
        // The reveal may be a mid-path blocker, not the destination —
        // re-query vision so every newly seen occupant enters the mask.
        closed.set(best);
        closed |= blocked_tiles();
    }
    last_result.status = StepStatus::blocked;
    return false;
}

// Adapt a single destination (for example a teammate's tile) to the same
// multi-goal movement policy, keeping obstacle handling consistent for callers.
bool step_towards(cambc::vec2 target)
{
    cambc::bitboard goals;
    goals.set(target);
    return step(goals);
}

} // namespace nav

