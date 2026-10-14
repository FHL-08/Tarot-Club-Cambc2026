// tactic/lock — altar-lock discipline (factory issue #10). A lock costs 10g
// and carries one at a time; econ stocks it on spawn visits, this module
// only places. Two placements pay:
//   * insurance — locking the altar we're about to tribute at can't block
//     our own channel (the owner's team is exempt) but occupies the slot
//     against an enemy lock or channel landing mid-tribute;
//   * denial — dropping it on an enemy-side altar a route passes within
//     ~2 tiles makes their tributes eat three basic attacks.
// Cancelling a live enemy channel by locking its altar is peel's variant of
// the same play — peel runs first in dispatch, so on ties it's already gone.
#include "tactic.h"
#include "support.h"
#include "../econ.h"
#include <cambc.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

namespace {

// "within two tiles" — GameRules §Consumables; no engine constant for it.
constexpr std::int64_t place_range = 2;

} // namespace

std::optional<exec::Action> tactic::lock(const Obs& obs)
{
    const auto& me = cambc::self();
    if (me.consumables()[cambc::consumable_type::altar_lock].count() == 0)
        return std::nullopt;

    // Placement reads are vision-gated; only visible altars are candidates.
    const cambc::bitboard visible = cambc::vision::visible_tiles();
    const auto usable = [&](cambc::vec2 altar) {
        return visible.is_set(altar) && !cambc::vision::has_altar_lock(altar);
    };
    const auto place = [&](cambc::vec2 altar) -> exec::Action {
        alarm_altar(obs, altar_index(altar));
        std::cout << "TACTIC lock: placed @r" << obs.round
                  << " (" << altar.x << "," << altar.y << ")\n";
        return exec::UseItem{ cambc::consumable_type::altar_lock, altar };
    };

    // Insurance: the altar this bag is about to channel on.
    if (obs.self_gems > 0 && econ::should_tribute(obs))
    {
        cambc::vec2 best{};
        std::int64_t best_d = std::numeric_limits<std::int64_t>::max();
        for (const auto altar : altars())
        {
            const auto d = obs.self_pos.chebyshev_distance(altar);
            if (d <= cambc::constants::submit_gems_range && d < best_d && usable(altar))
            {
                best = altar;
                best_d = d;
            }
        }
        if (best_d <= cambc::constants::submit_gems_range) return place(best);
    }

    // Denial: nearest enemy-side altar within placement range.
    cambc::vec2 best{};
    std::int64_t best_d = std::numeric_limits<std::int64_t>::max();
    for (const auto altar : altars())
    {
        const auto d = obs.self_pos.chebyshev_distance(altar);
        if (d > place_range || d >= best_d || !usable(altar)) continue;
        if (dist_to_spawn(altar, cambc::team::enemy) >= dist_to_spawn(altar, cambc::team::ally))
            continue;
        best = altar;
        best_d = d;
    }
    if (best_d <= place_range) return place(best);
    return std::nullopt;
}
