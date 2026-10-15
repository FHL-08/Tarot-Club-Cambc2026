// A small, optional vision layer. The champion completes its normal economy
// turn first; a spare ward then watches an altar approach without a detour.
#include "warding.h"
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>
#include "obs.h"
#include "turn.h"

namespace {

struct Candidate {
    cambc::vec2 tile{};
    int rough = 0;
};

// Compare two wards by the altar approaches they would actually see. Bush
// tiles count twice: a champion outside the bush cannot normally see through
// it, so a ward covering that approach gives more useful warning.
std::uint32_t coverage_score(const cambc::bitboard& sight,
    const cambc::bitboard& approach, const cambc::bitboard& bush_approach)
{
    return (sight & approach).count() + 2 * (sight & bush_approach).count();
}

} // namespace

// Put a ward near the nearest bank only when its vision adds useful coverage.
// The engine's hypothetical ward-vision API performs the real bush/wall
// occlusion calculation; no hidden enemy position is used to choose the tile.
void warding::maybe_place(const Obs& obs)
{
    static std::uint32_t next_check_round = 1;
    if (!obs.self_alive || obs.round < next_check_round || obs.self_gems == 0
        || cambc::fuel_remaining() < 40000 || cambc::constants::active_wards == 0)
        return;

    const auto& me = turn::self();
    try {
        // A channel is more valuable than a lookout: never risk interrupting
        // recall or a gem submission, and do not retry a charging trinket.
        if (me.is_recalling() || me.is_submitting_gems() || me.ward_cooldown() > 0)
            return;

        const auto& altars = turn::altars();
        int target = -1;
        std::int64_t nearest = 9;
        const auto position = me.position(); // post-action position, not the old Obs tile
        for (std::size_t i = 0; i < altars.size(); ++i) {
            const auto distance = position.chebyshev_distance(altars[i]);
            if (distance < nearest) { nearest = distance; target = static_cast<int>(i); }
        }
        // Placing a ward far from our next possible bank wastes its one active slot.
        if (target < 0) return;
        next_check_round = obs.round + 16;

        const auto passable = ~(cambc::map::walls() | cambc::map::holes()
            | cambc::map::altars());
        cambc::bitboard approach;
        approach.draw_chebyshev(altars[target], 6);
        // Only tiles a champion can cross are meaningful enemy approaches.
        approach &= passable;
        const auto bush_approach = cambc::map::bushes() & approach;
        const auto unseen_approach = approach & ~turn::visible_tiles();
        const auto legal = me.ward_placement_tiles() & passable;
        std::vector<Candidate> candidates;
        for (const auto tile : legal.positions()) {
            // Prefer a lookout pushed toward the bank or into an obscuring
            // bush. The exact vision score below decides between finalists.
            const int rough = 3 * static_cast<int>(bush_approach.is_set(tile))
                - static_cast<int>(tile.chebyshev_distance(altars[target]));
            candidates.push_back({tile, rough});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.rough != b.rough) return a.rough > b.rough;
            if (a.tile.y != b.tile.y) return a.tile.y < b.tile.y;
            return a.tile.x < b.tile.x;
        });

        // Replacing an existing ward loses its old vision. Require a clear
        // improvement, especially when it already watches this same altar.
        std::uint32_t old_score = 0;
        const auto existing = me.ward_positions();
        if (!existing.empty())
            old_score = coverage_score(cambc::ward::visible_tiles_from(existing.front()),
                approach, bush_approach);

        std::uint32_t best_score = 0, best_new = 0;
        cambc::vec2 best_tile{};
        bool found = false;
        const std::size_t limit = std::min<std::size_t>(5, candidates.size());
        for (std::size_t i = 0; i < limit && cambc::fuel_remaining() >= 24000; ++i) {
            const auto footprint = cambc::ward::visible_tiles_from(candidates[i].tile);
            const auto score = coverage_score(footprint, approach, bush_approach);
            const auto newly_seen = (footprint & unseen_approach).count();
            if (newly_seen >= 4 && score >= old_score + 8
                && (!found || score > best_score)) {
                found = true;
                best_score = score;
                best_new = newly_seen;
                best_tile = candidates[i].tile;
            }
        }
        if (!found || cambc::fuel_remaining() < 10000) return;

        // Placing a ward is an optional trinket action. A hidden obstruction
        // can still make this return false; normal champion actions are done.
        if (me.place_ward(best_tile)) {
            turn::note_action(); // next turn's observation must see the new ward
            if (cambc::fuel_remaining() >= 6000)
                std::cout << "RG7_WARD r=" << obs.round << " x=" << best_tile.x
                          << " y=" << best_tile.y << " altar=" << target
                          << " new=" << best_new << " score=" << best_score << '\n';
        }
    } catch (const cambc::error&) {
        // A placement race or newly revealed blocker must not undo farming.
    }
}
