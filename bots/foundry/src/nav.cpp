// Distance-field descent with hidden-occupant retries — the seam declared
// in nav.h. Ported from starter-cpp's move_towards_closest, generalized so
// every goal seeds the field and a false move retries alternative steps.
#include "nav.h"
#include "turn.h"
#include <cstdint>

namespace {

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
constexpr int max_retries = 8;

} // namespace

namespace nav {

bool step(const cambc::bitboard& goals)
{
    const auto& me = turn::self();
    // Callers mask goals with ~blocked, and blocked clears self — so our own
    // tile sneaks back into the goal set. Drop it: a "goal" that is our own
    // position must not veto every descent step toward the remaining goals.
    auto goals_clean = goals;
    goals_clean.set(me.position(), false);
    if (!me.can_move() || goals_clean.is_empty())
        return false;

    // The first mask reuses the turn cache — identical to a fresh build
    // this turn. Only the retry re-queries stay live: a revealed occupant
    // must enter the mask (comment below).
    const auto blocked = turn::blocked_tiles();

    // BFS out from the goals until our tile is reached; goal tiles are
    // lifted out of the walls so occupied ones still seed the descent.
    cambc::bitboard self_tile;
    self_tile.set(me.position());
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(blocked & ~goals_clean, goals_clean, self_tile, visited);

    const auto speed = static_cast<std::uint16_t>(me.base_stats().base_move_speed);
    auto closed = blocked;
    for (int retries = 0; retries <= max_retries; ++retries)
    {
        auto best = me.position();
        auto best_distance = field.get(best);
        for (const auto tile : closed.reachable_tiles(me.position(), speed).positions())
            if (field.get(tile) < best_distance)
            {
                best = tile;
                best_distance = field.get(tile);
            }
        if (best == me.position())
            return false;
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
                return true;
            }
        }
        catch (const cambc::error&) { }
        // The reveal may be a mid-path blocker, not the destination —
        // re-query vision so every newly seen occupant enters the mask.
        closed.set(best);
        closed |= blocked_tiles();
    }
    return false;
}

bool step_towards(cambc::vec2 target)
{
    cambc::bitboard goals;
    goals.set(target);
    return step(goals);
}

} // namespace nav
