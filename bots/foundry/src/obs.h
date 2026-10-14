#pragma once
// Per-turn observation snapshot consumed by the runtime modules (exec, the
// tactic/*.cpp interdiction modules, econ). Pure data — no engine handles —
// so a tactic can be swapped behind tick(Me, Obs) -> Action without SDK calls.
#include <cambc.h>
#include <cstdint>
#include <vector>
#include "wire.h"

// A visible enemy champion. gems/channel fields drive the bag-theft and
// channel-interrupt reads in docs/strategy.md §The mechanics.
struct EnemyObs {
    cambc::vec2 position{};
    cambc::champion_type type{};
    float health = 0, max_health = 0;
    // Live combat stats feed the kill-race estimate (issue #39).
    float strength = 0, focus = 0;
    std::int32_t attack_range = 1;
    std::uint32_t gems = 0;          // carried bag: killer inherits it +50g
    bool submitting = false;         // channelling tribute right now
    std::int32_t channel_left = 0;   // rounds left on that channel (0 when not submitting)
};

// A camp group with at least one spawn tile in vision, plus its lifecycle.
// group indexes map::camp_groups() — the same index space as Plan routes.
struct CampObs {
    std::uint8_t group = 0;
    cambc::map::camp_group_state state{};
};

struct Obs {
    std::uint32_t round = 0;
    // Self state this turn.
    cambc::vec2 self_pos{};
    cambc::champion_state self_state{};
    float self_health = 0, self_max_health = 0;
    std::uint32_t self_gems = 0, self_gold = 0;
    bool self_alive = false;
    std::vector<EnemyObs> enemies;
    std::vector<CampObs> camps;
    wire::Frame wire{}; // decoded shared memory; default fields if checksum failed
};

// Match perspective only — self() throws during the draft.
[[nodiscard]] inline Obs collect()
{
    Obs obs;
    obs.round = cambc::round_number();
    const auto& me = cambc::self();
    obs.self_pos = me.position();
    obs.self_state = me.state();
    obs.self_alive = me.is_alive();
    obs.self_health = me.health();
    obs.self_max_health = me.max_health();
    obs.self_gems = me.gem_count();
    obs.self_gold = me.gold_count();
    for (const auto& enemy : cambc::vision::champions(cambc::team::enemy))
    {
        EnemyObs seen;
        seen.position = enemy.position();
        seen.type = enemy.champion_name();
        seen.health = enemy.health();
        seen.max_health = enemy.max_health();
        seen.strength = enemy.strength();
        seen.focus = enemy.focus();
        seen.attack_range = enemy.attack_range();
        seen.gems = enemy.gem_count();
        seen.submitting = enemy.is_submitting_gems();
        // remaining_channel_time throws when no channel is active.
        seen.channel_left = seen.submitting ? enemy.remaining_channel_time() : 0;
        obs.enemies.push_back(seen);
    }
    const auto groups = cambc::map::camp_groups();
    for (const auto& group : cambc::vision::camp_groups())
    {
        CampObs seen;
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i] == group) seen.group = static_cast<std::uint8_t>(i);
        seen.state = group.state();
        obs.camps.push_back(seen);
    }
    if (const auto frame = wire::read()) obs.wire = *frame;
    return obs;
}
