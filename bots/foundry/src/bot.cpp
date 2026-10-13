// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "econ.h"
#include "obs.h"
#include "plan.h"
#include "random.h"
#include <cstdint>
#include <span>
#include <variant>

using cambc::vec2;
using cambc::team;

int distance(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

cambc::bitboard tiles_around(vec2 position, int radius) {
    cambc::bitboard tiles;
    tiles.clear();
    tiles.draw_chebyshev(position, radius);
    return tiles;
}

// Combine terrain and visible occupants using native bitboard operations.
cambc::bitboard blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

bool move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    if (!me.can_move() || goals.is_set(me.position()))
        return false;

    // Only free goals or occupied camp tiles can seed the field.
    const auto blocked = blocked_tiles();
    goals &= ~blocked | cambc::vision::camp_tiles();
    if (goals.is_empty())
        return false;
    if (goals.count() == 1 && !blocked.is_set(goals.positions()[0]))
        return me.move_towards(goals.positions()[0]);

    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);

    // Choose a reachable step with a smaller distance to the goal.
    auto step = me.position();
    for (auto tile : blocked.reachable_tiles(step, me.base_stats().base_move_speed).positions())
        if (field.get(tile) < field.get(step)) step = tile;

    return step != me.position() && me.move_absolute(step);
}

Role Bot::role() {
    if (role_) return *role_;
    Role resolved = Role::FLEX;
    try {
        const Plan plan = Plan::decode(cambc::draft_memory);
        const auto allies = cambc::champions(team::ally);
        const auto& me = cambc::self();
        for (std::uint8_t slot = 0; slot < allies.size(); ++slot)
            if (allies[slot] == me) {
                resolved = slot < plan.num_slots() ? plan.role_at(slot) : Role::FLEX;
                break;
            }
    } catch (const std::exception&) {
        // No decodable plan: FLEX is the safe default queue.
    }
    role_ = resolved;
    return resolved;
}

// Make one basic attack if the target is already in attack range.
template<class Target>
bool attack_if_in_range(const Target& target) {
    auto& me = cambc::self();
    if (!me.can_attack() || !target.is_visible() || target.health() <= 0
        || !me.attack_tiles().is_set(target.position())) {
        return false;
    }
    me.attack(target);
    return true;
}

// Attack a camp already in range, otherwise move toward the nearest camp tiles.
void farm_camps() {
    for (const auto& camp : cambc::vision::camps())
        if (attack_if_in_range(camp))
            return;

    // Union every group's spawn tiles except visibly respawning groups.
    cambc::bitboard goals;
    goals.clear();
    const auto visible = cambc::vision::visible_tiles();
    for (const auto& group : cambc::map::camp_groups()) {
        const bool group_visible = !(group.spawn_tiles() & visible).is_empty();
        if (group_visible && std::holds_alternative<cambc::map::camp_group_respawning>(group.state()))
            continue;
        goals |= group.spawn_tiles();
    }

    // Fresh vision after movement may reveal a camp already in range.
    if (move_towards_closest(goals))
        for (const auto& camp : cambc::vision::camps())
            if (attack_if_in_range(camp))
                return;
}

void Bot::play_turn() {
    // Initialise this champion's build and read the draft message once.
    if (chosen_stat < 0) {
        chosen_stat = random_number(3);
        std::cout << "draft: " << std::string(reinterpret_cast<const char*>(cambc::draft_memory.data()), 11) << '\n';
    }

    // Read before writing: little-endian u32 at shared-memory byte offset zero.
    std::uint32_t round = 0;
    cambc::shared_memory.read(0, std::span{&round, 1});
    std::cout << "shared round: " << round << '\n';
    round = cambc::round_number();
    cambc::shared_memory.write(0, std::span{&round, 1});

    auto& me = cambc::self();

    // Shopping is legal on spawn tiles and while dead — econ runs before the
    // alive check. Vision queries can fail on dead turns, so keep a minimal
    // snapshot for the shop path then.
    Obs obs;
    try {
        obs = collect();
    } catch (const std::exception&) {
        obs.round = cambc::round_number();
        obs.self_gold = me.gold_count();
    }
    econ::maybe_buy(role(), obs);

    // Leave channels running and skip turns where we cannot act.
    if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()
        || (!me.can_move() && !me.can_attack()))
        return;

    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());

    // Recall immediately when badly hurt away from spawn.
    if (!at_spawn && me.health() < me.max_health() * 0.35f) {
        me.recall();
        return;
    }

    // Attack the first enemy champion already in range.
    for (const auto& enemy : cambc::vision::champions(team::enemy))
        if (attack_if_in_range(enemy))
            return;

    // Bank once the bag crosses the econ cap (or the deadline forces it).
    if (econ::should_tribute(obs)) {
        // Union all altar approach tiles: the field chooses the nearest reachable altar.
        cambc::bitboard goals;
        goals.clear();
        for (auto altar : cambc::map::altars().positions()) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range
                && !cambc::vision::has_altar_lock(altar)) {
                    std::cout << "econ: tribute r" << obs.round << " bag=" << me.gem_count() << '\n';
                    me.submit_gems(altar);
                    return;
                }
            if (!cambc::vision::has_altar_lock(altar)) goals |= tiles_around(altar, cambc::constants::submit_gems_range);
        }
        move_towards_closest(goals);
        return;
    }

    farm_camps();

    me.set_priority(1 + random_number(100));
}
