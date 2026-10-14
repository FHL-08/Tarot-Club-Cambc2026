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
        if (e.health <= 0 || !attack_tiles.is_set(e.position)) continue;
        // A mid-tribute enemy is pinned at the altar: ordinary damage does
        // NOT cancel the channel (GameRules §Channels — only stun, death or
        // an altar lock does), but the free hit softens the bag carrier
        // for a gank. Otherwise lowest health = fastest kill.
        const bool better = !best || (e.submitting && !best->submitting)
            || (e.submitting == best->submitting && e.health < best->health);
        if (better) best = &e;
    }
    if (!best) return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}
