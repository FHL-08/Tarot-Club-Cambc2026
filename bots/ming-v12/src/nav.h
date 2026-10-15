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
// descent rebuilds the path after that reveal instead of repeatedly following
// a distance field which still routes through the hidden occupant. Other SDK
// queries are not wrapped by the movement-error handler.
#include <cambc.h>

namespace nav {

enum class StepStatus { unavailable, blocked, arrived, moved };

// The executor needs to distinguish arrival from failure: standing on an altar
// approach must not make a banking champion wander to a different approach.
struct StepResult {
    StepStatus status = StepStatus::unavailable;
    std::uint16_t distance_before = cambc::distance_field::unreachable;
    std::uint16_t distance_after = cambc::distance_field::unreachable;
    int retries = 0;
};

// Read the last movement attempt without another map search. These distances
// belong to that call's goal set, not to a globally fixed destination.
[[nodiscard]] const StepResult& last_step();

// Move toward the nearest goal tile. Blocked goals (camps, enemies, terrain)
// stay seeds — descent only lands on free tiles, so they are approached
// rather than dropped. Already being on a goal is arrival, not an invitation
// to leave it. False means no movement happened; last_step() explains whether
// we arrived, lacked an allowance/fuel, or could not find a legal route.
bool step(const cambc::bitboard& goals);

// Move toward a single tile; equivalent to step() on a one-tile board.
bool step_towards(cambc::vec2 target);

} // namespace nav

