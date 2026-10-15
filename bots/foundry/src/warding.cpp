// A small, optional vision layer. The champion completes its normal economy
// turn first; a spare ward then watches an objective approach without a detour.
#include "warding.h"
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <variant>
#include <vector>
#include "obs.h"
#include "turn.h"

namespace {

struct Candidate {
    cambc::vec2 tile{};
    int rough = 0;
};

// Compare two wards by the objective approaches they would actually see. Bush
// tiles count twice: a champion outside the bush cannot normally see through
// it, so a ward covering that approach gives more useful warning.
std::uint32_t coverage_score(const cambc::bitboard& sight,
    const cambc::bitboard& approach, const cambc::bitboard& bush_approach)
{
    return (sight & approach).count() + 2 * (sight & bush_approach).count();
}

// The point enemies walk toward to reach a live camp: the centroid of its
// member spawn tiles. Only groups visible this turn have a known state.
bool live_camp_anchor(const CampObs& seen, cambc::vec2& anchor)
{
    if (!std::holds_alternative<cambc::map::camp_group_alive>(seen.state)
        && !std::holds_alternative<cambc::map::camp_group_reward_available>(seen.state))
        return false;
    const auto& group = turn::map_groups()[seen.group];
    std::int64_t x = 0, y = 0, n = 0;
    for (const auto& member : group) {
        const auto p = member.spawn_position();
        x += p.x; y += p.y; ++n;
    }
    if (!n) return false;
    anchor = { static_cast<std::int32_t>(x / n), static_cast<std::int32_t>(y / n) };
    return true;
}

} // namespace

// Put a ward near the nearest live objective only when its vision adds useful
// coverage. The engine's hypothetical ward-vision API performs the real
// bush/wall occlusion calculation; no hidden enemy position is used.
void warding::maybe_place(const Obs& obs)
{
    static std::uint32_t next_check_round = 1;
    if (!obs.self_alive || obs.round < next_check_round
        || cambc::fuel_remaining() < 40000 || cambc::constants::active_wards == 0)
        return;

    const auto& me = turn::self();
    try {
        // A channel is more valuable than a lookout: never risk interrupting
        // recall, a gem submission or a teleport crystal.
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()
            || me.ward_cooldown() > 0)
            return;

        // Any real objective qualifies: a bank on our tribute route or a live
        // camp being worked. Far from every objective the trinket stays pocketed.
        const auto position = me.position(); // post-action position, not the old Obs tile
        cambc::vec2 target{};
        std::int64_t nearest = 9;
        for (const auto& altar : turn::altars()) {
            const auto distance = position.chebyshev_distance(altar);
            if (distance < nearest) { nearest = distance; target = altar; }
        }
        for (const auto& camp : obs.camps) {
            cambc::vec2 anchor{};
            if (!live_camp_anchor(camp, anchor)) continue;
            const auto distance = position.chebyshev_distance(anchor);
            if (distance < nearest) { nearest = distance; target = anchor; }
        }
        if (nearest == 9) return;
        next_check_round = obs.round + 16;

        const auto passable = ~(cambc::map::walls() | cambc::map::holes()
            | cambc::map::altars());
        cambc::bitboard approach;
        approach.draw_chebyshev(target, 6);
        // Only tiles a champion can cross are meaningful enemy approaches.
        approach &= passable;
        const auto bush_approach = cambc::map::bushes() & approach;
        const auto unseen_approach = approach & ~turn::visible_tiles();
        // blocked_tiles additionally masks occupied tiles, so a placement
        // that would fail on a visible body never reaches the engine.
        const auto legal = me.ward_placement_tiles() & ~turn::blocked_tiles();
        std::vector<Candidate> candidates;
        for (const auto tile : legal.positions()) {
            // Prefer a lookout pushed toward the objective or into an
            // obscuring bush. The exact vision score below decides.
            const int rough = 3 * static_cast<int>(bush_approach.is_set(tile))
                - static_cast<int>(tile.chebyshev_distance(target));
            candidates.push_back({tile, rough});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.rough != b.rough) return a.rough > b.rough;
            if (a.tile.y != b.tile.y) return a.tile.y < b.tile.y;
            return a.tile.x < b.tile.x;
        });

        // Replacing an existing ward loses its old vision. Never downgrade
        // coverage of the current approach for the swap.
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
            if (newly_seen >= 4 && score >= old_score
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
            turn::note_moved(); // the new ward is a fresh vision source — invalidate the turn caches
            if (cambc::fuel_remaining() >= 6000)
                std::cout << "RG7_WARD r=" << obs.round << " x=" << best_tile.x
                          << " y=" << best_tile.y << " new=" << best_new
                          << " score=" << best_score << '\n';
        }
    } catch (const cambc::error&) {
        // A placement race or newly revealed blocker must not undo farming.
    }
}
