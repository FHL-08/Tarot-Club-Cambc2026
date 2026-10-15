// Spending policy: the team's measured build order (clone_config.h,
// issue #84), consumable top-up, tribute bag-cap. Each body banks its
// equal share of an entry's team total before the queue advances, and the
// tail repeats open-ended. Entries are SHARDS — matching shards
// auto-combine into emblems, so progress counts
// shard_count + combine_count * emblem_count.
#include <cambc.h>
#include "econ.h"
#include "turn.h"
#include "clone_config.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>

namespace {

using cambc::consumable_type;
using cambc::item_type;

// Altar locks place within two tiles (GameRules §Consumables — no engine
// constant); one tile of slack covers the ring a clearer stands on while
// whittling a pack adjacent to its capture tiles.
constexpr std::int64_t lock_pass_range = 3;

// Shards banked toward an entry, counting emblems at their combine cost.
std::uint32_t entry_progress(item_type shard, const cambc::enum_map<item_type>& items)
{
    const auto result = shard.result();
    return items[shard] + shard.combine_count() * (result ? items[*result] : 0);
}

// Chebyshev tiles to the nearest altar, minus the tribute pickup radius —
// an optimistic lower bound for the forced-bank deadline.
std::uint32_t walk_to_altar(cambc::vec2 from)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto& altar : turn::altars())
        best = std::min(best, from.chebyshev_distance(altar));
    if (best == std::numeric_limits<std::int64_t>::max()) return 0;
    return static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, best - cambc::constants::submit_gems_range));
}

// Measured policy: a flat ~44-gem cap — the round ramp and scoreboard
// tighten are foundry's, not in the replay skeleton (issue #84).
std::uint32_t bag_cap(const Obs&)
{
    return clone::trib_cap;
}

// Chebyshev to the nearest tile of a team's spawn region — the same
// "enemy-side" test tactic/lock.cpp applies to placement candidates.
std::int64_t dist_to_spawn(cambc::vec2 p, cambc::team side)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto tile : turn::spawn_region(side).positions())
        best = std::min(best, p.chebyshev_distance(tile));
    return best;
}

// Consumable stocking is route-aware (docs/strategy.md §Champion-build
// queues: "carry altar lock + teleport crystal when routed past enemy-side
// altars"). tactic::lock is starved when the bodies that walk past enemy
// altars never shop — forward routes only see the shop on recalls, deaths
// and turn 1 — so a routed champion must take its lock on the FIRST
// shopping turn, ahead of shard gold.
// The flag is a pure function of the immutable plan blob, the fixed self
// slot and static map data, so it computes once and caches — dead-turn
// shopping (minimal Obs) hits the cache without re-querying the engine.
// A route "passes" an enemy-side altar when a CAMP stop's capture tiles
// reach inside lock range, or a spliced ALTAR stop is itself enemy-side
// (banking there re-locks it every visit — owner tributes are exempt).
bool route_stocks_consumables()
{
    static const bool stocked = [] {
        try
        {
            const Plan plan = Plan::decode(cambc::draft_memory);
            const auto allies = cambc::champions(cambc::team::ally);
            const auto& me = turn::self();
            std::size_t slot = allies.size();
            for (std::size_t i = 0; i < allies.size(); ++i)
                if (allies[i] == me) { slot = i; break; }
            if (slot >= plan.num_slots()) return false;
            const Route route = plan.route_at(static_cast<std::uint8_t>(slot));
            const auto& altars = turn::altars();
            const auto enemy_side = [&](cambc::vec2 altar) {
                return dist_to_spawn(altar, cambc::team::enemy)
                    < dist_to_spawn(altar, cambc::team::ally);
            };
            const auto& groups = turn::map_groups();
            for (std::uint8_t i = 0; i < route.size(); ++i)
            {
                const Stop stop = route[i];
                if (stop.kind == StopKind::ALTAR)
                {
                    if (stop.index < altars.size() && enemy_side(altars[stop.index]))
                        return true;
                }
                else if (stop.kind == StopKind::CAMP && stop.index < groups.size())
                {
                    cambc::bitboard tiles;
                    groups[stop.index].spawn_tiles_into(tiles);
                    for (const auto tile : tiles.positions())
                        for (const auto altar : altars)
                            if (enemy_side(altar)
                                && tile.chebyshev_distance(altar) <= lock_pass_range)
                                return true;
                }
            }
        }
        catch (const std::exception&) { }
        return false;
    }();
    return stocked;
}

} // namespace

// The route probe is pure plan+map data (~30-40k on the big maps): let
// exec::warm_statics spend a slack turn on it instead of the first
// gold-carrying field turn.
void econ::warm()
{
    (void)route_stocks_consumables();
}

void econ::maybe_buy(Role role, const Obs& obs)
{
    auto& me = turn::self();
    // Every purchase below is gated on can_buy(), which needs gold — with an
    // empty purse the whole body is a no-op, so skip its one-time statics
    // (route probe, queue reads) instead of spending ~40k on a field turn.
    // obs.self_gold was read this same turn; gold cannot move until a buy.
    if (obs.self_gold == 0) return;
    const auto items = me.item_count();

    // Consumables first on routed bodies: forward routes only see the shop
    // on recalls, deaths and turn 1, so the lock cannot queue behind shard
    // gold — 10g is trivial against a 150g emblem. Unrouted bodies keep the
    // opportunistic post-emblem restock below (enemy carriers bank at OUR
    // altars too — a carried lock is live denial wherever its holder walks).
    const auto stock_consumables = [&me, &obs] {
        if (me.consumables()[consumable_type::altar_lock].count() == 0
            && me.can_buy(consumable_type::altar_lock))
        {
            me.buy(consumable_type::altar_lock);
            std::cout << "ECON lock: stocked @r" << obs.round << '\n';
        }
        if (me.consumables()[consumable_type::tp_crystal].count() == 0
            && me.can_buy(consumable_type::tp_crystal))
            me.buy(consumable_type::tp_crystal);
    };
    const bool routed = route_stocks_consumables();
    if (routed) stock_consumables();

    // Measured team buy-order (issue #84): an entry is done once this body
    // banks its equal share of the team total; the tail repeats open-ended.
    static const std::size_t bodies = [] {
        try { return std::max<std::size_t>(1, cambc::champions(cambc::team::ally).size()); }
        catch (const std::exception&) { return std::size_t{4}; }
    }();
    const std::span<const clone::EconItem> queue{ clone::econ_queue };
    std::size_t next = 0;
    while (next < queue.size()
        && static_cast<std::uint64_t>(entry_progress(queue[next].item, items)) * bodies
            >= queue[next].team_total)
        ++next;

    if (next < queue.size())
    {
        const auto& entry = queue[next];
        const std::uint32_t target = static_cast<std::uint32_t>(
            (entry.team_total + bodies - 1) / bodies);
        const std::uint32_t have = entry_progress(entry.item, items);
        const std::uint32_t need = have < target ? target - have : 0;
        // Live gold, not obs.self_gold — routed bodies may have spent first.
        const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            need, me.gold_count() / entry.item.cost()));
        if (count && me.can_buy(entry.item, count)) me.buy(entry.item, count);
    }

    // Standing orders once the first emblem is done — the pre-routes proxy
    // kept for unrouted bodies (docs/strategy.md §Champion-build queues).
    if (!routed && next >= 1) stock_consumables();

    if (next == queue.size())
    {
        // Tail: the queue's last shard repeats open-ended (stats stack).
        const item_type tail = queue.back().item;
        const auto count = static_cast<std::uint32_t>(me.gold_count() / tail.cost());
        if (count && me.can_buy(tail, count)) me.buy(tail, count);
    }
}

bool econ::should_tribute(const Obs& obs)
{
    if (obs.self_gems == 0) return false;
    // Piñata ceiling: deaths hand the killer the whole bag +50g.
    if (obs.self_gems >= 45) return true;
    // Forced bank: channel ticks + walk must fit inside the rounds left.
    const std::uint32_t ticks = (obs.self_gems + 9) / 10; // ceil(bag/10) owner turn ends
    if (obs.round + ticks + walk_to_altar(obs.self_pos) >= cambc::constants::max_rounds)
        return true;
    return obs.self_gems >= bag_cap(obs);
}
