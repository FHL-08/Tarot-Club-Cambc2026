#pragma once
// The single movement seam for foundry: every step a champion takes goes
// through nav so descent, occupancy and retry policy live in exactly one
// place (docs/strategy.md §Architecture — "nav.h: distance_field descent +
// hidden-blocker false-retry").
//
// Both entry points are distance_field descents seeded from caller goals;
// bitboard and field ops are host imports, so the whole-map BFS costs no
// guest fuel. A false move means a hidden occupant: the blocker tile is
// revealed for the round and the move allowance is preserved, so the
// descent retries alternative steps. cambc::error from a known-illegal
// destination propagates — nav never swallows engine errors.
#include <cambc.h>

namespace nav {

// Move toward the nearest goal tile. Blocked goals (camps, enemies, terrain)
// stay seeds — descent only lands on free tiles, so they are approached
// rather than dropped. False means no improving step this turn: already on
// a goal, cannot move, no reachable goal, or retries exhausted on hidden
// occupants.
bool step(const cambc::bitboard& goals);

// Move toward a single tile; equivalent to step() on a one-tile board.
bool step_towards(cambc::vec2 target);

} // namespace nav
