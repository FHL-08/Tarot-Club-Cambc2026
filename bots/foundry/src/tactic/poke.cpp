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
        // for a gank. Then a stunned body outranks lower health — hitting
        // the target a teammate already disabled is what makes focus fire
        // emerge (issue #103); otherwise lowest health = fastest kill.
        const bool stunned = turn::enemy_stunned(e.position).value_or(false);
        const bool best_stunned = best
            && turn::enemy_stunned(best->position).value_or(false);
        const bool better = !best || (e.submitting && !best->submitting)
            || (e.submitting == best->submitting
                && ((stunned && !best_stunned)
                    || (stunned == best_stunned && e.health < best->health)));
        if (better) best = &e;
    }
    if (!best) return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}
