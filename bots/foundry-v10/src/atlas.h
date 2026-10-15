#pragma once
// Compile-time map knowledge — docs/strategy.md §DRAFT "atlas.cpp: map →
// CampTable {gems, gold, hp, period, tiles}, POI distance matrix (host-side
// distance_field)". Built once during the draft, when the full map is
// visible and field computes run host-side below the fuel meter.
//
// POI index space for the distance matrix:
//   [0 .. num_camps)              camp groups, map::camp_groups() order
//   [num_camps .. +num_altars)    altars, map::altars().positions() order
//   [last]                        ally spawn region
#include <cambc.h>
#include <cstdint>
#include <vector>

// One camp group's static geography and economy. `health`/`dps` are sums of
// member base stats — the clear-time and retaliation-pressure inputs to the
// planner's service estimate.
struct CampSite {
    std::uint8_t index = 0;
    cambc::vec2 anchor{};                  // lowest spawn tile — representative point
    std::vector<cambc::vec2> tiles;        // capture tiles (camp spawn positions)
    std::uint32_t gems = 0, gold = 0;      // capture rewards
    std::int32_t period = 0;               // max_respawn_timer
    float health = 0;                      // Σ member base health
    float dps = 0;                         // Σ member base strength per awake turn
    bool monolith = false;                 // 20k hp / 200 str — never routed solo
    bool passive = false;                  // no member can fight back (clusters)
};

struct Atlas {
    std::vector<CampSite> camps;
    std::vector<cambc::vec2> altars;                  // altar tile per altar index
    std::vector<std::vector<cambc::vec2>> altar_ring; // walkable tribute tiles per altar
    std::vector<cambc::vec2> spawn;                   // ally spawn tiles
    // dist[poi * pois + poi2]; unreachable when a walled pocket separates them.
    std::vector<std::uint16_t> dist;
    std::uint32_t pois = 0;

    static constexpr std::uint16_t unreachable = 0xFFFF;

    [[nodiscard]] std::uint32_t camp_poi(std::size_t g) const { return static_cast<std::uint32_t>(g); }
    [[nodiscard]] std::uint32_t altar_poi(std::size_t a) const
    {
        return static_cast<std::uint32_t>(camps.size() + a);
    }
    [[nodiscard]] std::uint32_t spawn_poi() const { return pois - 1; }
    // Walk distance between two POIs in tiles (≈ turns at move speed 1).
    [[nodiscard]] std::uint16_t between(std::uint32_t a, std::uint32_t b) const
    {
        return dist[static_cast<std::size_t>(a) * pois + b];
    }
    // Walk distance from a camp to an altar's nearest tribute tile.
    [[nodiscard]] std::uint16_t camp_to_altar(std::size_t g, std::size_t a) const
    {
        return between(camp_poi(g), altar_poi(a));
    }
};

namespace atlas {

// Compile the static map: camp table, altar stand rings, ally spawn, and the
// full POI distance matrix (one distance_field BFS per POI). Draft-only:
// relies on the draft-phase full-map view and 10x fuel turn.
[[nodiscard]] Atlas build();

} // namespace atlas
