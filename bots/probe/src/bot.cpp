// Champion decisions. main.cpp dispatches here after the draft.
// Telemetry probe: every turn, query each enemy roster handle (visible or not)
// and log which inspection calls return a value versus throw cambc::error.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <cstdio>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

using cambc::vec2;
using cambc::team;

namespace {

constexpr std::size_t probe_count = 8;
const std::array<const char*, probe_count> probe_names{
    "gem_count", "item_count", "consumable_tp_crystal", "consumable_altar_lock",
    "is_alive", "position", "health", "champion_name"};

struct probe_agg {
    std::uint32_t n = 0, ok = 0;
    std::string last_value = "-";
    std::string last_code = "-";

    const char* status() const {
        if (n == 0) return "none";
        if (ok == n) return "ok";
        if (ok == 0) return "error";
        return "mixed";
    }
};

// Champion stdout shares one pipe across instances; emit each line (newline
// included) in a single fwrite so concurrent bots cannot interleave mid-line.
void emit_line(std::string line) {
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
}

template<class F>
void run_probe(std::size_t index, const cambc::champion& enemy, F&& call, bool visible,
               std::uint32_t self_handle, std::uint32_t handle, std::uint32_t round, bool in_roster,
               std::array<probe_agg, probe_count>& hidden_aggs,
               std::array<probe_agg, probe_count>& visible_aggs) {
    probe_agg& agg = visible ? visible_aggs[index] : hidden_aggs[index];
    std::string value;
    std::string code;
    bool ok = true;
    try {
        value = call(enemy);
    } catch (const cambc::error& e) {
        ok = false;
        code = std::to_string(static_cast<std::int32_t>(e.code()));
    } catch (const std::exception& e) {
        ok = false;
        code = std::string("stdexc:") + e.what();
    }
    agg.n++;
    if (ok) {
        agg.ok++;
        agg.last_value = value;
    } else {
        agg.last_code = code;
    }
    std::string line = "PROBE_SAMPLE round=" + std::to_string(round) + " self=" +
        std::to_string(self_handle) + " handle=" + std::to_string(handle) +
        " vis=" + (visible ? "1" : "0") + " roster=" + (in_roster ? "1" : "0") +
        " api=" + probe_names[index] + " result=" + (ok ? "ok" : "error");
    if (ok)
        line += " value=" + value;
    else
        line += " code=" + code;
    emit_line(line);
}

std::uint32_t handle_id(const cambc::champion& c) {
    return static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c));
}

void probe_enemies() {
    const auto round = cambc::round_number();
    const auto self_handle = handle_id(cambc::self());

    // Vision-roster membership cross-checks is_visible() against a vision query result.
    std::unordered_set<std::uint32_t> visible_roster;
    for (const auto& e : cambc::vision::champions(team::enemy))
        visible_roster.insert(handle_id(e));

    std::array<probe_agg, probe_count> hidden_aggs, visible_aggs;

    for (const auto& enemy : cambc::champions(team::enemy)) {
        const auto h = handle_id(enemy);
        bool visible = false;
        try {
            visible = enemy.is_visible();
        } catch (const std::exception&) {
            // A failed visibility check still counts as "not in any vision result".
        }
        const bool in_roster = visible_roster.count(h) != 0;

        std::size_t i = 0;
        run_probe(i++, enemy, [](const cambc::champion& e) { return std::to_string(e.gem_count()); },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy,
                  [](const cambc::champion& e) {
                      std::uint64_t total = 0;
                      for (auto count : e.item_count()) total += count;
                      return std::to_string(total);
                  },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy,
                  [](const cambc::champion& e) {
                      return std::to_string(e.consumables()[cambc::consumable_type::tp_crystal].count());
                  },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy,
                  [](const cambc::champion& e) {
                      return std::to_string(e.consumables()[cambc::consumable_type::altar_lock].count());
                  },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy, [](const cambc::champion& e) { return e.is_alive() ? "1" : "0"; },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy, [](const cambc::champion& e) { return cambc::to_string(e.position()); },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy, [](const cambc::champion& e) { return std::to_string(e.health()); },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
        run_probe(i++, enemy, [](const cambc::champion& e) { return cambc::to_string(e.champion_name()); },
                  visible, self_handle, h, round, in_roster, hidden_aggs, visible_aggs);
    }

    for (std::size_t i = 0; i < probe_count; ++i) {
        const auto& hid = hidden_aggs[i];
        const auto& vis = visible_aggs[i];
        emit_line("PROBE " + std::string(probe_names[i]) + " hidden=" + hid.status() +
                  " visible=" + vis.status() + " round=" + std::to_string(round) +
                  " hidden_ok=" + std::to_string(hid.ok) + "/" + std::to_string(hid.n) +
                  " hidden_val=" + hid.last_value + " hidden_code=" + hid.last_code +
                  " visible_ok=" + std::to_string(vis.ok) + "/" + std::to_string(vis.n) +
                  " visible_val=" + vis.last_value + " visible_code=" + vis.last_code);
    }
}

} // namespace

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

void Bot::buy_items() {
    auto& me = cambc::self();
    const std::array items{cambc::item_type::power_shard_of_strength,
        cambc::item_type::power_shard_of_focus, cambc::item_type::power_shard_of_health};
    const auto item = items[chosen_stat];
    const unsigned count = me.gold_count() / item.cost();
    if (count && me.can_buy(item, count)) me.buy(item, count);
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
    // Initialise this champion's build once.
    if (chosen_stat < 0)
        chosen_stat = random_number(3);

    // Probe enemy handles before acting so even idle turns yield samples.
    probe_enemies();

    auto& me = cambc::self();

    // Leave channels running and skip turns where we cannot act.
    if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()
        || (!me.can_move() && !me.can_attack()))
        return;

    // Shop before leaving spawn.
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (at_spawn)
        buy_items();

    // Recall immediately when badly hurt away from spawn.
    if (!at_spawn && me.health() < me.max_health() * 0.35f) {
        me.recall();
        return;
    }

    // Attack the first enemy champion already in range.
    for (const auto& enemy : cambc::vision::champions(team::enemy))
        if (attack_if_in_range(enemy))
            return;

    // Bank any carried gems before farming.
    if (me.gem_count() > 0) {
        // Union all altar approach tiles: the field chooses the nearest reachable altar.
        cambc::bitboard goals;
        goals.clear();
        for (auto altar : cambc::map::altars().positions()) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range
                && !cambc::vision::has_altar_lock(altar)) {
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
