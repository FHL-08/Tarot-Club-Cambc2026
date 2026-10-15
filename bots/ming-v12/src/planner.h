#pragma once
// The draft compiler — docs/strategy.md §DRAFT "planner.cpp: partition camps
// → per-champion cyclic routes". Pure function of (Atlas, Roster): every
// champion's decode of the plan is a pure function of the same blob, so the
// schedule is consensus without a protocol.
#include <cambc.h>
#include <vector>
#include "atlas.h"
#include "plan.h"

// The team as bodies in ally-handle order — the plan.h slot space. One draft
// pick expands to one body, cassi_and_issac to two. `team_index` is the
// absolute side (0/1): the planner spends it on the mirror-asymmetry break.
struct Roster {
    std::vector<cambc::champion_type> bodies;
    std::int32_t team_index = 0;
};

namespace planner {

// Partition the atlas into one cyclic route per body slot, splice ALTAR
// stops where a route's accumulated gems cross the role's bag cap, flag
// capture-overlap tiles, and pack the result for PlanSpec::encode.
// Draft-only: service estimates read champion design stats.
[[nodiscard]] PlanSpec compile(const Atlas& atlas, const Roster& roster);

} // namespace planner

