// poke — last refusal in the tactic chain: when no module interdicts,
// spend the basic attack a turn already allows rather than waste it.
#include "tactic.h"
#include "support.h"

std::optional<exec::Action> tactic::poke(const Obs& obs)
{
    const auto& me = cambc::self();
    if (!me.can_attack()) return std::nullopt;
    const auto attack_tiles = me.attack_tiles();
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
    // Poke picks a fight, so it answers to the same kill race the
    // fight-or-flight arm runs (issue #39): only fire when the target's
    // TTK clearly beats our TTD — losing the race hands the killer the
    // carried bag plus 50g (GameRules §Death).
    if (time_to_kill(*best) > time_to_die(obs) * 0.9f) return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}
