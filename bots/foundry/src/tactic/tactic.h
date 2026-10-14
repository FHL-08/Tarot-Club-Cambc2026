#pragma once
// Interdiction hook — docs/strategy.md §RUNTIME "tactic/: evaluated before
// the route step". Each tactic module (deny/gank/lock/peel, #9-#12) gets
// first refusal on the turn: return an Action to take it, std::nullopt to
// pass control to the route step. The chain is assembled here once the
// modules land; until then the default offer is empty.
#include <optional>
#include "../exec.h"
#include "../obs.h"

namespace tactic {

[[nodiscard]] inline std::optional<exec::Action> offer(const Obs&)
{
    return std::nullopt;
}

} // namespace tactic
