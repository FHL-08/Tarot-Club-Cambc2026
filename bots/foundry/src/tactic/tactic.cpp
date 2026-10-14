// offer() dispatch — the seam exec.cpp's tick() calls before the route step
// (docs/strategy.md §RUNTIME).
#include "tactic.h"

std::optional<exec::Action> tactic::offer(const Obs& obs)
{
    // Channel saves > bag theft > lock > sabotage. The order is the strategy:
    // a cancelled tribute removes tributed gems outright, a kill steals a
    // carried bag, a lock delays their banking, and deny is gravy.
    if (const auto action = peel(obs)) return action;
    if (const auto action = gank(obs)) return action;
    if (const auto action = lock(obs)) return action;
    if (const auto action = deny(obs)) return action;
    return std::nullopt;
}
