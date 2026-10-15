// atlas — the draft-time map compile (docs/strategy.md §DRAFT).
// All inputs are static map queries legal during the draft; every distance
// is a host-side distance_field BFS, so the whole matrix stays a pure,
// deterministic function of the map.
//
// Fuel note: host calls are cheap (~100 fuel) but bulk transfers price by
// size — a field's export_linear_buffer (w*h u16s) is the most expensive
// call in the compile. Bitboard linear buffers are BIT-packed, so masks
// stay opaque host-side objects and roots are set per tile; the only bulk
// moves are the field exports, one per camp. BFS on a uniform-cost grid is
// symmetric, so altar and spawn rows mirror camp columns for free. A
// chebyshev estimate underlies the whole matrix so a fuel bailout still
// yields routable — if optimistic — distances instead of a dead draft.
#include "atlas.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

// Locate a map tile in an exported distance grid, so draft routes can compare
// camp travel costs without asking the engine for every individual tile.
std::size_t idx(cambc::vec2 t, std::int32_t w) { return static_cast<std::size_t>(t.y) * w + t.x; }

// Tiles that can never be walked on at planning time. Camps are deliberately
// absent — their spawn tiles are where champions stand to capture, so route
// distances are measured to them; occupants are a runtime concern (nav.cpp
// handles them). Altars are impassable but enter POIs via their stand rings.
cambc::bitboard static_blocked()
{
    return cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(cambc::team::enemy);
}

// Walkable tiles a champion can tribute `altar` from.
std::vector<cambc::vec2> stand_ring(cambc::vec2 altar, const cambc::bitboard& blocked)
{
    cambc::bitboard ring;
    ring.draw_chebyshev(altar, static_cast<float>(cambc::constants::submit_gems_range));
    ring &= ~blocked;
    return ring.positions();
}

} // namespace

// Compile camp rewards and route distances once during draft. Expensive exact
// paths are fuel-limited; uncomputed pairs keep estimates rather than losing
// the whole team's opening plan to an unfinished draft turn.
Atlas atlas::build()
{
    Atlas atlas;
    const std::int32_t w = cambc::map::width();
    const std::int32_t h = cambc::map::height();

    // sdk_camp_design prices like any other host call, but a member's design
    // is a pure function of its camp_type — five cached lookups cover every
    // member on the map.
    std::array<std::optional<cambc::camp_stats>, 16> designs;

    for (const auto& group : cambc::map::camp_groups())
    {
        CampSite site;
        site.index = static_cast<std::uint8_t>(atlas.camps.size());
        site.gems = group.gem_value();
        site.gold = group.gold_value();
        site.period = group.max_respawn_timer();
        site.passive = true;
        for (const auto& member : group)
        {
            // spawn_position()/kind() are stored fields — no host call.
            site.tiles.push_back(member.spawn_position());
            const auto type = static_cast<std::size_t>(member.kind());
            if (!designs[type]) designs[type] = member.base_stats();
            site.health += designs[type]->base_health;
            site.dps += designs[type]->base_strength;
            site.monolith = site.monolith || member.is_monolith();
            site.passive = site.passive && designs[type]->base_strength <= 0;
        }
        site.anchor = site.tiles.empty() ? cambc::vec2{} : *std::min_element(site.tiles.begin(), site.tiles.end());
        atlas.camps.push_back(std::move(site));
    }

    const auto blocked = static_blocked();
    atlas.altars = cambc::map::altars().positions();
    for (const auto altar : atlas.altars) atlas.altar_ring.push_back(stand_ring(altar, blocked));
    atlas.spawn = cambc::map::spawn_region(cambc::team::ally).positions();

    // POI tile sets in matrix order: camps, altar stand rings, ally spawn.
    std::vector<const std::vector<cambc::vec2>*> poi_tiles;
    poi_tiles.reserve(atlas.camps.size() + atlas.altars.size() + 1);
    for (const auto& site : atlas.camps) poi_tiles.push_back(&site.tiles);
    for (const auto& ring : atlas.altar_ring) poi_tiles.push_back(&ring);
    poi_tiles.push_back(&atlas.spawn);

    atlas.pois = static_cast<std::uint32_t>(poi_tiles.size());
    atlas.dist.assign(static_cast<std::size_t>(atlas.pois) * atlas.pois, Atlas::unreachable);

    // Chebyshev lower bound under every pair — the fallback for fields that
    // never run (fuel bail). Anchors are precomputed once: re-resolving the
    // min tile inside the pair loop multiplies guest-op cost by tile count.
    std::vector<cambc::vec2> anchors(atlas.pois);
    for (std::uint32_t i = 0; i < atlas.pois; ++i)
    {
        const auto& tiles = *poi_tiles[i];
        anchors[i] = tiles.empty() ? cambc::vec2{} : *std::min_element(tiles.begin(), tiles.end());
    }
    for (std::uint32_t i = 0; i < atlas.pois; ++i)
    {
        atlas.dist[static_cast<std::size_t>(i) * atlas.pois + i] = 0;
        for (std::uint32_t j = i + 1; j < atlas.pois; ++j)
        {
            const auto d = static_cast<std::uint16_t>(
                std::max(std::abs(anchors[i].x - anchors[j].x), std::abs(anchors[i].y - anchors[j].y)));
            atlas.dist[static_cast<std::size_t>(i) * atlas.pois + j] = d;
            atlas.dist[static_cast<std::size_t>(j) * atlas.pois + i] = d;
        }
    }

    // Root order: the spawn field is non-negotiable (it is the reachability
    // oracle the planner's unreachable-filter trusts); the rest are
    // farthest-point samples over every POI so the landmark estimates below
    // stay tight for pairs whose own fields never ran.
    std::vector<std::uint32_t> order{ atlas.spawn_poi() };
    {
        // d[i] = chebyshev to the nearest already-picked root; updated once
        // per pick keeps the sampler O(pois²) — a naive rescan is O(pois³)
        // and eats the draft turn on the big maps.
        std::vector<std::uint32_t> d(atlas.pois);
        for (std::uint32_t i = 0; i < atlas.pois; ++i)
            d[i] = static_cast<std::uint32_t>(std::max(
                std::abs(anchors[i].x - anchors[atlas.spawn_poi()].x),
                std::abs(anchors[i].y - anchors[atlas.spawn_poi()].y)));
        while (order.size() < atlas.pois)
        {
            std::uint32_t best_i = atlas.pois; // sentinel: unset until first candidate
            std::uint32_t best_d = 0;
            for (std::uint32_t i = 0; i < atlas.pois; ++i)
                // Strict '>' plus index order: ties resolve to the lowest index.
                if (best_i == atlas.pois || d[i] > best_d) { best_d = d[i]; best_i = i; }
            order.push_back(best_i);
            for (std::uint32_t i = 0; i < atlas.pois; ++i)
                d[i] = std::min(d[i], static_cast<std::uint32_t>(std::max(
                    std::abs(anchors[i].x - anchors[best_i].x), std::abs(anchors[i].y - anchors[best_i].y))));
        }
    }

    // One host compute+export per rooted POI — fields are the compile's fuel
    // hog (~w*h export bytes each), so the loop bails once the margin can't
    // cover two more fields plus the planner/encode/pick that must still
    // fit inside the same draft turn.
    cambc::distance_field field;
    cambc::bitboard roots;
    cambc::bitboard visited{ cambc::uninitialized };
    std::vector<std::uint16_t> linear;
    // Reject invalid map coordinates before indexing the exported grid: a bad
    // route endpoint must not read outside our distance buffer.
    const auto in_bounds = [w, h](cambc::vec2 t) { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h; };
    std::vector<char> rooted(atlas.pois, 0);

    // reserve must cover everything after this loop: the landmark pass
    // (bails at its own floor), planner::compile (~200k on the biggest map),
    // encode+memory write+pick (~20k) and slack.
    std::int64_t reserve = 430000;
    for (const auto root_poi : order)
    {
        const auto before = cambc::fuel_remaining();
        if (before < reserve) break;

        roots.clear();
        for (const auto tile : *poi_tiles[root_poi])
            if (in_bounds(tile)) roots.set(tile);
        field.compute(blocked, roots, visited);
        linear = field.export_linear_buffer();

        for (std::uint32_t j = 0; j < atlas.pois; ++j)
        {
            std::uint16_t best = Atlas::unreachable;
            for (const auto tile : *poi_tiles[j])
                if (in_bounds(tile)) best = std::min(best, linear[idx(tile, w)]);
            // A field that ran is ground truth — unreachable stays
            // unreachable; only unrooted pairs keep estimates.
            atlas.dist[static_cast<std::size_t>(root_poi) * atlas.pois + j] = best;
            atlas.dist[static_cast<std::size_t>(j) * atlas.pois + root_poi] = best;
        }
        rooted[root_poi] = 1;
        const auto spent = static_cast<std::int64_t>(before - cambc::fuel_remaining());
        reserve = std::max<std::int64_t>(reserve, 2 * spent + 390000);
    }

    // Landmark estimate for pairs whose fields never ran: rooted POIs give
    // a lower bound max_r |d(r,i)−d(r,j)| and a detour upper bound
    // min_r d(r,i)+d(r,j); the estimate clamps the chebyshev fill between
    // them. Landmarks that reach neither side are skipped, so a pocketed
    // camp never reads as close through a wall. The pass is guest-fuelled
    // and priced per pair — it is capped to 16 landmarks and bails row by
    // row so a big map degrades to chebyshev instead of dying mid-sweep.
    std::vector<std::uint32_t> marks;
    for (std::uint32_t i = 0; i < atlas.pois && marks.size() < 16; ++i)
        if (rooted[i]) marks.push_back(i);
    for (std::uint32_t i = 0; i < atlas.pois; ++i)
    {
        if (rooted[i]) continue;
        // The floor leaves planner+write+pick the fuel they need.
        if (cambc::fuel_remaining() < 260000) break;
        for (std::uint32_t j = 0; j < atlas.pois; ++j)
        {
            if (i == j || rooted[j]) continue; // rooted pairs already hold real distances
            std::uint32_t lb = 0, ub = Atlas::unreachable;
            for (const auto r : marks)
            {
                const auto ri = atlas.dist[static_cast<std::size_t>(r) * atlas.pois + i];
                const auto rj = atlas.dist[static_cast<std::size_t>(r) * atlas.pois + j];
                if (ri == Atlas::unreachable || rj == Atlas::unreachable) continue;
                lb = std::max<std::uint32_t>(lb, ri > rj ? ri - rj : rj - ri);
                ub = std::min<std::uint32_t>(ub, static_cast<std::uint32_t>(ri) + rj);
            }
            const auto cell = static_cast<std::size_t>(i) * atlas.pois + j;
            const auto cur = atlas.dist[cell];
            const auto est = std::min<std::uint32_t>(ub, std::max<std::uint32_t>(lb, cur));
            if (est != Atlas::unreachable) atlas.dist[cell] = static_cast<std::uint16_t>(est);
            // else nothing bridges the pair — keep the chebyshev fill.
        }
    }
    return atlas;
}

