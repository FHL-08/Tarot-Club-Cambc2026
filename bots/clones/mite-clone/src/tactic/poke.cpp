// poke — last refusal in the tactic chain: when no module interdicts,
// spend the basic attack a turn already allows rather than waste it.
#include "tactic.h"
#include "support.h"
#include "../clone_config.h"
#include <cstdint>
#include <limits>

namespace {

// This body's draft slot — the per-champ stagger on the focus cadence.
int self_slot()
{
    static int slot = -1;
    if (slot < 0)
    {
        try
        {
            const auto& me = turn::self();
            const auto allies = cambc::champions(cambc::team::ally);
            for (std::size_t i = 0; i < allies.size(); ++i)
                if (allies[i] == me) { slot = static_cast<int>(i); break; }
        }
        catch (const std::exception&) { }
        if (slot < 0) slot = 0;
    }
    return slot;
}

} // namespace

std::optional<exec::Action> tactic::poke(const Obs& obs)
{
    const auto& me = turn::self();
    if (!turn::self_state().contains(cambc::champion_state::can_attack)) return std::nullopt;
    // Below the pitstop line the retreat owns the turn — trading it for
    // poke is how a hurt champion gets chain-killed.
    if (obs.self_health < obs.self_max_health * 0.6f) return std::nullopt;
    const auto& attack_tiles = turn::attack_tiles();
    const EnemyObs* best = nullptr;
    std::int64_t best_d = std::numeric_limits<std::int64_t>::max();
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0 || e.max_health <= 0 || !attack_tiles.is_set(e.position)) continue;
        // Measured policy (issue #93): the nearest enemy in the ring takes
        // the hit — median attacked-target hp% ~0.5-0.6 across all four
        // teams, so lowest-hp% was a misread.
        if (const auto d = obs.self_pos.chebyshev_distance(e.position); d < best_d)
        { best_d = d; best = &e; }
    }
    if (!best) return std::nullopt;
    // Champ-focus share (issue #93): only every champ_focus_n-th in-range
    // turn takes the champion; the rest fall through to camp work.
    if ((obs.round + static_cast<std::uint32_t>(self_slot()))
        % static_cast<std::uint32_t>(clone::champ_focus_n) != 0)
        return std::nullopt;
    return exec::Attack{ best->position, exec::Attack::Kind::champion };
}
