#pragma once
// Interdiction hook — docs/strategy.md §RUNTIME "tactic/: evaluated before
// the route step". Each module gets first refusal on the turn in dispatch
// order — peel (channel saves) > gank (bag theft) > lock (altar denial) >
// burst (ability spend on champions) > deny (sabotage) > poke (spend the
// free attack) — matching the strategy
// doc's priority ("throughput first, denial second"). Returning an Action
// takes the turn; std::nullopt passes control down the chain and finally
// to the route step.
//
// Modules are pure policy: same Obs → same decision. Like exec::tick they
// may do engine *reads* (ability cooldowns, consumables, live channel flags
// that Obs doesn't snapshot); the returned Action carries plain data and
// exec::apply remains the single write seam — aside from wire.h alarm
// merges, which are the designed side-band for converge calls.
#include <optional>
#include "../exec.h"
#include "../obs.h"

namespace tactic {

// Interrupt a visible enemy's tribute or recall when our current kit can do so.
[[nodiscard]] std::optional<exec::Action> peel(const Obs& obs);
// Consider stealing a valuable carried bag only when the local damage estimate supports it.
[[nodiscard]] std::optional<exec::Action> gank(const Obs& obs);
// Spend an altar lock for banking insurance or nearby enemy-side denial.
[[nodiscard]] std::optional<exec::Action> lock(const Obs& obs);
// Offer a damaging or protective ability during a visible champion encounter.
[[nodiscard]] std::optional<exec::Action> burst(const Obs& obs);
// Briefly contest enemy capture/respawn progress without unlimited detours from income.
[[nodiscard]] std::optional<exec::Action> deny(const Obs& obs);
// Use a basic attack against a nearby enemy when higher-priority tactics offered nothing.
[[nodiscard]] std::optional<exec::Action> poke(const Obs& obs);

// First non-empty offer in dispatch order wins the turn.
[[nodiscard]] std::optional<exec::Action> offer(const Obs& obs);

} // namespace tactic

