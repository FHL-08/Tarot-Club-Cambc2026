// poke — last refusal in the tactic chain: when no module interdicts,
// spend the basic attack a turn already allows rather than waste it.
#include "tactic.h"
#include "support.h"

std::optional<exec::Action> tactic::poke(const Obs& obs)
{
    const auto& me = turn::self();
    if (!turn::self_state().contains(cambc::champion_state::can_attack)) return std::nullopt;
    // Below the pitstop line the retreat owns the turn — trading it for
    // poke is how a hurt champion gets chain-killed.
    if (obs.self_health < obs.self_max_health * 0.6f) return std::nullopt;
    const auto& attack_tiles = turn::attack_tiles();
    const EnemyObs* best = nullptr;
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0 || e.max_health <= 0 || !attack_tiles.is_set(e.position)) continue;
        // Measured policy (issue #84): the lowest health fraction in the
        // ring takes the hit. Absolute health would let a fresh tank body
        // screen a wounded carry.
        const bool better = !best
            || e.health / e.max_health < best->health / best->max_health;
        if (better) best = &e;
    }
    if (!best) return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}
