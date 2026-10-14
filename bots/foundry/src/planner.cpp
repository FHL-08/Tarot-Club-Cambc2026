// planner — partition the atlas into one cyclic route per body slot
// (docs/strategy.md §DRAFT). Assignment is LPT-style over an earliest-
// deadline ordering: hardest clears are placed first into the cheapest
// tour insertion, so routes balance makespan and stay geographically
// local. Altar stops splice where a route's accumulated gems cross the
// role's bag cap; capture-overlap tiles are flagged for the runtime.
// Deterministic: every choice resolves ties by lowest index.
#include "planner.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace {

// Per-body clearing kit derived from design stats.
struct Kit {
    Role role = Role::FLEX;
    float health = 1;
    float dps = 1;           // sustained damage per turn against camps
    std::int32_t speed = 1;  // tiles per turn
};

// Role per champion body — maps onto the econ build queues:
// vendetta = camp cycler, ranged carries free-farm, issac tanks, the rest flex.
Role role_of(cambc::champion_type type)
{
    switch (type)
    {
    case cambc::champion_type::vendetta: return Role::FARMER;
    case cambc::champion_type::cassi:
    case cambc::champion_type::tavi:
    case cambc::champion_type::vesper_vale: return Role::CARRY;
    case cambc::champion_type::issac: return Role::ESCORT;
    default: return Role::FLEX;
    }
}

Kit kit_for(cambc::champion_type type)
{
    const auto stats = cambc::champion{ type }.base_stats();
    Kit kit;
    kit.role = role_of(type);
    kit.health = stats.base_health;
    kit.speed = std::max(1, stats.base_move_speed);
    const float basic = stats.base_strength * stats.camp_damage_multiplier;
    // Spell output amortised over the 8-turn slot cooldown: casters bring a
    // ~2x-focus nuke, melee kits an attack reset worth one extra basic.
    const float burst = stats.base_attack_range > 1 ? 2.0f * stats.base_focus : basic;
    kit.dps = basic + burst / 8.0f;
    return kit;
}

// Turns one group costs this kit: clear time, the 5-round capture, and a
// heal trip per sortie the pack's retaliation forces. Camps strike the
// nearest champion every awake turn — the elkring lesson priced in: a
// high-damage pack is many trips, not one fight.
float service(const Atlas& atlas, const CampSite& site, const Kit& kit)
{
    const float clear = std::max(1.0f, site.health / kit.dps);
    float sorties = 1.0f;
    if (!site.passive)
        sorties = std::max(1.0f, std::ceil(site.dps * clear / (kit.health * 0.6f)));
    const auto home = atlas.between(atlas.camp_poi(site.index), atlas.spawn_poi());
    const float sortie_cost = home == Atlas::unreachable
        ? 0.0f
        : 2.0f * static_cast<float>(home) / static_cast<float>(kit.speed) + 12.0f;
    return clear + static_cast<float>(cambc::constants::capture_time)
        + (sorties - 1.0f) * sortie_cost;
}

// 2-opt polish on the cyclic perimeter; deterministic first-improvement
// sweeps, bounded so a huge route can't burn the draft turn.
void polish(Atlas const& atlas, std::vector<std::uint8_t>& route)
{
    const auto k = route.size();
    if (k < 4) return;
    for (int pass = 0; pass < 4; ++pass)
    {
        bool improved = false;
        for (std::size_t i = 0; i + 2 < k && !improved; ++i)
            for (std::size_t j = i + 2; j < k && !(i == 0 && j == k - 1); ++j)
            {
                const auto pi = atlas.camp_poi(route[i]), pj = atlas.camp_poi(route[j]);
                const auto before_a = atlas.camp_poi(route[(i + k - 1) % k]);
                const auto after_b = atlas.camp_poi(route[(j + 1) % k]);
                const std::int64_t delta = atlas.between(before_a, pj) + atlas.between(pi, after_b)
                    - atlas.between(before_a, pi) - atlas.between(pj, after_b);
                if (delta < 0)
                {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                    break;
                }
            }
        if (!improved) break;
    }
}

std::int32_t nearest_altar(const Atlas& atlas, std::uint8_t g)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    std::int32_t altar = -1;
    for (std::size_t a = 0; a < atlas.altars.size(); ++a)
    {
        const auto d = atlas.camp_to_altar(g, a);
        if (d != Atlas::unreachable && d < best && a < Plan::max_groups)
        {
            best = d;
            altar = static_cast<std::int32_t>(a);
        }
    }
    return altar;
}

// Altar stops splice in wherever the running bag crosses the role cap; a
// route that never reaches the cap still gets one bank point at the end so
// the over-cap divert always has somewhere to go.
void splice_altars(const Atlas& atlas, const std::vector<std::uint8_t>& camps, std::uint32_t cap,
                   std::vector<Stop>& out, const std::vector<std::uint8_t>& altar_by_group)
{
    std::uint32_t bag = 0;
    bool spliced = false;
    for (const auto g : camps)
    {
        const auto& site = atlas.camps[g];
        std::uint8_t flags = 0;
        const auto altar = altar_by_group[g];
        if (altar != Plan::no_index)
            for (const auto tile : site.tiles)
                if (tile.chebyshev_distance(atlas.altars[altar]) <= cambc::constants::submit_gems_range)
                {
                    flags |= stop_flag_capture_overlap;
                    break;
                }
        out.push_back(Stop{ StopKind::CAMP, g, flags });
        bag += site.gems;
        if (cap && bag >= cap)
        {
            const auto near = nearest_altar(atlas, g);
            if (near >= 0)
            {
                out.push_back(Stop{ StopKind::ALTAR, static_cast<std::uint8_t>(near), 0 });
                spliced = true;
            }
            bag = 0;
        }
    }
    if (!spliced && !camps.empty())
    {
        std::uint32_t gems = 0;
        for (const auto g : camps) gems += atlas.camps[g].gems;
        if (gems)
            if (const auto near = nearest_altar(atlas, camps.back()); near >= 0)
                out.push_back(Stop{ StopKind::ALTAR, static_cast<std::uint8_t>(near), 0 });
    }
}

} // namespace

PlanSpec planner::compile(const Atlas& atlas, const Roster& roster)
{
    PlanSpec spec;
    const std::size_t groups = std::min<std::size_t>(atlas.camps.size(), Plan::max_groups);
    const std::size_t slots = std::min<std::size_t>(roster.bodies.size(), Plan::max_slots);
    spec.flags = roster.team_index == 1 ? 1 : 0; // bit0: team-B breakpoint asymmetry
    spec.bag_cap = { 40, 48, 40, 32 }; // FARMER, CARRY, ESCORT, FLEX

    std::vector<Kit> kits(slots);
    for (std::size_t s = 0; s < slots; ++s)
    {
        kits[s] = kit_for(roster.bodies[s]);
        spec.slot_roles.push_back(kits[s].role);
    }

    // Every group names its nearest reachable altar — routed or not, the
    // runtime banks there when the plan is consulted.
    spec.altar_by_group.assign(groups, Plan::no_index);
    for (std::size_t g = 0; g < groups; ++g)
        if (const auto a = nearest_altar(atlas, static_cast<std::uint8_t>(g)); a >= 0)
            spec.altar_by_group[g] = static_cast<std::uint8_t>(a);

    // Candidates: solo-routable groups only — monoliths and camps walled off
    // from spawn are planner-known unworkable and left to future tactics.
    // ...and a pack whose retaliation burns more than ~3 sorties of HP per
    // clear for EVERY kit is a trap, not a stop: the body spends the match
    // dead or travelling for one camp's reward (lanes Vendetta/Tavi churn).
    std::vector<std::uint8_t> candidates;
    for (std::size_t g = 0; g < groups; ++g)
    {
        const auto& site = atlas.camps[g];
        if (site.monolith
            || atlas.between(atlas.spawn_poi(), atlas.camp_poi(g)) == Atlas::unreachable)
            continue;
        bool soloable = site.passive;
        for (const auto& kit : kits)
        {
            const float clear = std::max(1.0f, site.health / kit.dps);
            if (site.dps * clear < kit.health * 2.0f) { soloable = true; break; }
        }
        if (soloable) candidates.push_back(static_cast<std::uint8_t>(g));
    }

    // Earliest-deadline ordering: the slowest clears gate a route's first
    // capture, so they are scheduled first, ahead of cheap camps that slot
    // into whatever space remains. Keys are hoisted — the comparator must
    // not re-run service estimates per comparison at draft fuel prices.
    // Clear-time estimates per camp×kit, computed once — recomputing them in
    // the deadline sort, assignment scoring and load tracking triples the
    // guest-fuel bill on camp-dense maps.
    std::array<std::array<float, 8>, Plan::max_groups> svc{};
    std::array<float, Plan::max_groups> deadline{};
    for (const auto g : candidates)
        for (std::size_t s = 0; s < slots; ++s)
        {
            svc[g][s] = service(atlas, atlas.camps[g], kits[s]);
            deadline[g] += svc[g][s];
        }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](std::uint8_t a, std::uint8_t b) { return deadline[a] > deadline[b]; });
    // Gems are the win condition, gold is fuel: run the assignment pass
    // over gem camps first so every slot's route anchors on tribute
    // income, then let gold camps fill the gaps. Clustering by geography
    // alone parked two bodies on all-gold routes on rows (1366 gold,
    // 0 gems collected all match).
    std::stable_sort(candidates.begin(), candidates.end(), [&](std::uint8_t a, std::uint8_t b) {
        return (atlas.camps[a].gems > 0) > (atlas.camps[b].gems > 0);
    });

    // Assignment = nearest-centroid clustering in EDD order, then each slot's
    // route is ordered by a nearest-neighbour chain off the real matrix.
    // Insertion-per-candidate was O(camps×slots×route) of matrix lookups —
    // on the biggest maps that alone overruns a draft turn.
    std::vector<std::vector<std::uint8_t>> assigned(slots);
    std::vector<cambc::vec2> centroid(slots, cambc::vec2{});
    std::vector<float> load(slots, 0.0f);
    const auto spawn_anchor = atlas.spawn.empty() ? cambc::vec2{} : atlas.spawn.front();
    for (const auto g : candidates)
    {
        // -1 sentinel, not 0: a camp no slot can solo must be dropped, not
        // silently dumped on slot 0's route (that's exactly how Vendetta
        // ended up owning an all-elk route on lanes).
        std::size_t best_slot = slots;
        float best_cost = std::numeric_limits<float>::max();
        for (std::size_t s = 0; s < slots; ++s)
        {
            // Per-kit feasibility, not just camp-level: every candidate is
            // soloable by SOME kit, but handing it to a body that can't
            // survive the clear parks that slot in churn (lanes: Vendetta
            // routed onto packs only a tank could farm — zero output).
            const float clr = std::max(1.0f, atlas.camps[g].health / kits[s].dps);
            if (!atlas.camps[g].passive
                && atlas.camps[g].dps * clr >= kits[s].health * 2.0f)
                continue;
            const auto hub = assigned[s].empty() ? spawn_anchor : centroid[s];
            const auto& a = atlas.camps[g].anchor;
            const float leg = static_cast<float>(std::max(std::abs(a.x - hub.x), std::abs(a.y - hub.y)))
                / kits[s].speed;
            // Minimum resulting load (LPT makespan): the service term both
            // balances totals and steers hard clears to fast kits — without
            // it one fast kit snowballs into a mega-route while strong
            // clearers idle on a single camp.
            const float cost = leg + load[s] + svc[g][s];
            if (cost < best_cost)
            {
                best_cost = cost;
                best_slot = s;
            }
        }
        if (best_slot == slots) continue; // no feasible body — leave it unrouted
        assigned[best_slot].push_back(g);
        auto& c = centroid[best_slot];
        const auto n = static_cast<std::int32_t>(assigned[best_slot].size());
        c.x = (c.x * (n - 1) + atlas.camps[g].anchor.x) / n;
        c.y = (c.y * (n - 1) + atlas.camps[g].anchor.y) / n;
        load[best_slot] += svc[g][best_slot];
    }

    // Co-farm the linked pair on one partition: camps retaliate only against
    // the nearest champion, so sharing a route puts the range-1 body between
    // the pack and the range-3 carry — and with linked deaths (either twin
    // dying kills both, GameRules §Cassi & Issac) a solo carry route is a
    // double-bag loss waiting to happen.
    int pair_carry = -1, pair_tank = -1;
    for (std::size_t s = 0; s < slots; ++s)
    {
        if (roster.bodies[s] == cambc::champion_type::cassi) pair_carry = static_cast<int>(s);
        else if (roster.bodies[s] == cambc::champion_type::issac) pair_tank = static_cast<int>(s);
    }
    if (pair_carry >= 0 && pair_tank >= 0)
    {
        auto merged = assigned[static_cast<std::size_t>(pair_carry)];
        merged.insert(merged.end(), assigned[static_cast<std::size_t>(pair_tank)].begin(),
                      assigned[static_cast<std::size_t>(pair_tank)].end());
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        assigned[static_cast<std::size_t>(pair_carry)] = merged;
        assigned[static_cast<std::size_t>(pair_tank)] = merged;
    }

    for (auto& route : assigned)
    {
        // Nearest-neighbour chain anchored at the camp closest to spawn.
        if (route.size() > 1)
        {
            std::vector<std::uint8_t> chain;
            chain.push_back(*std::min_element(route.begin(), route.end(), [&](std::uint8_t a, std::uint8_t b) {
                return atlas.between(atlas.spawn_poi(), atlas.camp_poi(a))
                    < atlas.between(atlas.spawn_poi(), atlas.camp_poi(b));
            }));
            std::vector<char> used(atlas.camps.size(), 0);
            used[chain.back()] = 1;
            while (chain.size() < route.size())
            {
                const auto from = atlas.camp_poi(chain.back());
                std::uint8_t next = 0;
                std::uint16_t best = Atlas::unreachable;
                for (const auto g : route)
                {
                    if (used[g]) continue;
                    if (const auto d = atlas.between(from, atlas.camp_poi(g)); d < best)
                    {
                        best = d;
                        next = g;
                    }
                }
                used[next] = 1;
                chain.push_back(next);
            }
            route = std::move(chain);
        }
    }
    for (std::size_t s = 0; s < slots; ++s)
    {
        auto& route = assigned[s];
        polish(atlas, route);
        // Enter the cycle at the camp nearest spawn — the first leg and each
        // post-pitstop re-entry start there.
        if (!route.empty())
        {
            const auto first = std::min_element(route.begin(), route.end(), [&](std::uint8_t a, std::uint8_t b) {
                return atlas.between(atlas.spawn_poi(), atlas.camp_poi(a))
                    < atlas.between(atlas.spawn_poi(), atlas.camp_poi(b));
            });
            std::rotate(route.begin(), first, route.end());
            // Mirror break: Team B's routes cycle one stop further so a
            // mirrored plan doesn't produce identical timings (full ties
            // gift Team B, so the asymmetry is ours to spend either way).
            if (roster.team_index == 1 && route.size() > 1)
                std::rotate(route.begin(), route.begin() + 1, route.end());
        }
        std::vector<Stop> stops;
        splice_altars(atlas, route, spec.bag_cap[static_cast<std::size_t>(kits[s].role)], stops,
                      spec.altar_by_group);
        spec.routes.push_back(std::move(stops));
    }
    // Budget fit: drop the tail stop of the longest route until encode()
    // is under draft memory (never triggers on the published maps).
    auto planned_bytes = [&] {
        std::size_t stops = 0;
        for (const auto& r : spec.routes) stops += r.size();
        return 10 + spec.slot_roles.size() + spec.altar_by_group.size()
            + 3 * spec.slot_roles.size() + 2 * stops;
    };
    while (planned_bytes() > Plan::max_bytes)
    {
        const auto longest = std::max_element(spec.routes.begin(), spec.routes.end(),
                                              [](const auto& a, const auto& b) { return a.size() < b.size(); });
        if (longest->empty()) break;
        longest->pop_back();
    }
    return spec;
}
