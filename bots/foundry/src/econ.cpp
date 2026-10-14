// Spending policy: role build queues, consumable top-up, tribute bag-cap.
// docs/strategy.md §Champion-build queues mapped onto plan.h roles:
//   FARMER <- Vendetta line (camp-kill cycler)
//   CARRY  <- Tavi line (field-hauler; the sell/swap at ~r1600 is the
//             role-gated Tavi path, deferred until a Tavi role exists)
//   ESCORT <- Issac line (meat-shield on capture tiles)
//   FLEX   <- Varg line (fallback for slots beyond the plan's role table)
// Queues list SHARDS — five (six for Power) matching shards auto-combine
// into the emblem, so the queue entry is done when
// shard_count + combine_count * emblem_count >= combine_count.
#include <cambc.h>
#include "econ.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>

namespace {

using cambc::consumable_type;
using cambc::item_type;

constexpr item_type farmer_queue[]{
    item_type::monster_shard_of_strength,
    item_type::jungle_shard_of_strength,
    item_type::power_shard_of_strength,
};
constexpr item_type carry_queue[]{
    item_type::jungle_shard_of_strength,
    item_type::treasure_shard_of_strength,
    item_type::monster_shard_of_strength,
    item_type::power_shard_of_focus,
};
constexpr item_type escort_queue[]{
    item_type::jungle_shard_of_health,
    item_type::durability_shard_of_health,
    item_type::power_shard_of_health,
};
constexpr item_type flex_queue[]{
    item_type::jungle_shard_of_strength,
    item_type::monster_shard_of_strength,
    item_type::power_shard_of_strength,
};

// Altar locks place within two tiles (GameRules §Consumables — no engine
// constant); one tile of slack covers the ring a clearer stands on while
// whittling a pack adjacent to its capture tiles.
constexpr std::int64_t lock_pass_range = 3;

std::span<const item_type> queue_for(Role role)
{
    switch (role)
    {
    case Role::FARMER: return farmer_queue;
    case Role::CARRY: return carry_queue;
    case Role::ESCORT: return escort_queue;
    case Role::FLEX: return flex_queue;
    }
    return flex_queue;
}

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
    for (const auto& altar : cambc::map::altars().positions())
        best = std::min(best, from.chebyshev_distance(altar));
    if (best == std::numeric_limits<std::int64_t>::max()) return 0;
    return static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, best - cambc::constants::submit_gems_range));
}

// Small bags early: <=10 gems is a 1-tick, damage-proof channel
// (docs/GameRules.md — only stun/death/altar-lock interrupt tribute).
// The cap grows with the round, tightens when behind on the scoreboard,
// and stays under the audit ceiling no matter what.
std::uint32_t bag_cap(const Obs& obs)
{
    std::int64_t cap = 10 + static_cast<std::int64_t>(obs.round) / 60;
    const auto ours = cambc::global_submitted_gems(cambc::team::ally);
    const auto theirs = cambc::global_submitted_gems(cambc::team::enemy);
    if (theirs > ours)
        cap -= std::min<std::int64_t>(10, static_cast<std::int64_t>(theirs - ours) / 25);
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cap, 10, 40));
}

// Chebyshev to the nearest tile of a team's spawn region — the same
// "enemy-side" test tactic/lock.cpp applies to placement candidates.
std::int64_t dist_to_spawn(cambc::vec2 p, cambc::team side)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto tile : cambc::map::spawn_region(side).positions())
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
            const auto& me = cambc::self();
            std::size_t slot = allies.size();
            for (std::size_t i = 0; i < allies.size(); ++i)
                if (allies[i] == me) { slot = i; break; }
            if (slot >= plan.num_slots()) return false;
            const Route route = plan.route_at(static_cast<std::uint8_t>(slot));
            const auto altars = cambc::map::altars().positions();
            const auto enemy_side = [&](cambc::vec2 altar) {
                return dist_to_spawn(altar, cambc::team::enemy)
                    < dist_to_spawn(altar, cambc::team::ally);
            };
            const auto groups = cambc::map::camp_groups();
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

void econ::maybe_buy(Role role, const Obs& obs)
{
    auto& me = cambc::self();
    const auto queue = queue_for(role);
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

    std::size_t next = 0;
    while (next < queue.size() && entry_progress(queue[next], items) >= queue[next].combine_count())
        ++next;

    if (next < queue.size())
    {
        const item_type shard = queue[next];
        const std::uint32_t need = shard.combine_count() - entry_progress(shard, items);
        // Live gold, not obs.self_gold — routed bodies may have spent first.
        const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            need, me.gold_count() / shard.cost()));
        if (count && me.can_buy(shard, count)) me.buy(shard, count);
    }

    // Standing orders once the first emblem is done — the pre-routes proxy
    // kept for unrouted bodies (docs/strategy.md §Champion-build queues).
    if (!routed && next >= 1) stock_consumables();

    if (next == queue.size())
    {
        // Tail: the queue's last shard repeats open-ended (stats stack).
        const item_type tail = queue.back();
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
