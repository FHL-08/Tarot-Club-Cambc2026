#pragma once
// Interdiction hook — docs/strategy.md §RUNTIME "tactic/: evaluated before
// the route step". Each tactic module (deny/gank/lock/peel, #9-#12) gets
// first refusal on the turn: return an Action to take it, std::nullopt to
// pass control to the route step. The chain is assembled here once the
// modules land; until then the default offer is the basic attack a turn
// already allows: an enemy champion in range is hit, never ignored.
#include <optional>
#include "../exec.h"
#include "../obs.h"

namespace tactic {

[[nodiscard]] inline std::optional<exec::Action> offer(const Obs& obs)
{
    const auto& me = cambc::self();
    if (!me.can_attack()) return std::nullopt;
    // Below the pitstop line the retreat owns the turn — trading it for
    // poke is how a hurt champion gets chain-killed.
    if (obs.self_health < obs.self_max_health * 0.6f) return std::nullopt;
    const auto attack_tiles = me.attack_tiles();
    const EnemyObs* best = nullptr;
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0 || !attack_tiles.is_set(e.position)) continue;
        // A mid-tribute channel is the steal: the hit cancels it and the
        // gems stay in the bag. Otherwise lowest health = fastest kill.
        const bool better = !best || (e.submitting && !best->submitting)
            || (e.submitting == best->submitting && e.health < best->health);
        if (better) best = &e;
    }
    if (!best) return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}

} // namespace tactic
