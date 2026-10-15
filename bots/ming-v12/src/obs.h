#pragma once
// Per-turn observation snapshot consumed by the runtime modules (exec, the
// tactic/*.cpp interdiction modules, econ). Pure data — no engine handles —
// so a tactic can be swapped behind tick(Me, Obs) -> Action without SDK calls.
#include <cambc.h>
#include <array>
#include <cstdint>
#include <exception>
#include <vector>
#include "observation_health.h"
#include "turn.h"
#include "wire.h"

// A visible enemy champion. gems/channel fields drive the bag-theft and
// channel-interrupt reads in docs/strategy.md §The mechanics.
struct EnemyObs
{
    cambc::vec2 position{};
    cambc::champion_type type{};
    float health = 0, max_health = 0;
    std::uint32_t gems = 0;        // carried bag: killer inherits it +50g
    bool submitting = false;       // channelling tribute right now
    std::int32_t channel_left = 0; // rounds left on that channel (0 when not submitting)
};

// A camp group with at least one spawn tile in vision, plus its lifecycle.
// group indexes map::camp_groups() — the same index space as Plan routes.
struct CampObs
{
    std::uint8_t group = 0;
    cambc::map::camp_group_state state{};
};

struct Obs : observation::Health
{
    std::uint32_t round = 0;
    // Self state this turn.
    cambc::vec2 self_pos{};
    cambc::champion_state self_state{};
    float self_health = 0, self_max_health = 0;
    std::uint32_t self_gems = 0, self_gold = 0;
    bool self_alive = false;
    std::vector<EnemyObs> enemies;
    std::vector<CampObs> camps;
    wire::Frame wire{}; // decoded shared memory; default fields by checksum failure
    // camps indexed by group — the route scan's seen() lookups are the
    // hottest per-turn loop; a dense table turns each into O(1).
    std::array<const CampObs *, 64> camp_at{};
    // Give route decisions constant-time access to visible camps, avoiding repeated scans.
    // Rebuild after filling camps: pointers into the vector would move if it later grows.
    void index_camps()
    {
        camp_at.fill(nullptr);
        for (const auto &c : camps)
            if (c.group < camp_at.size())
                camp_at[c.group] = &c;
    }
};

// Build this champion's legal observation snapshot for farming and defensive decisions.
// Enemy details are collected only through current vision, never from replay omniscience.
// Match perspective only — self() throws during the draft.
[[nodiscard]] inline Obs collect()
{
    Obs obs;
    obs.round = cambc::round_number();
    // Commit the mandatory self snapshot before touching optional vision data.
    // A missing enemy/camp record must not turn a healthy champion into a fake
    // zero-health body with no movement or attack flags.
    try
    {
        const auto &me = turn::self();
        obs.self_alive = me.is_alive();
        obs.self_gems = me.gem_count();
        obs.self_gold = me.gold_count();
        // One sdk_champion record stands in for the position/state/health
        // triple of per-field host calls — identical values, ~1/4 the fuel.
        if (const auto *info = turn::self_info())
        {
            obs.self_pos = {info->x, info->y};
            obs.self_state = cambc::champion_state::from_bits_retain(info->state);
            obs.self_health = info->health;
            obs.self_max_health = info->max_health;
        }
        else if (obs.self_alive)
        {
            obs.self_pos = me.position();
            obs.self_state = me.state();
            obs.self_health = me.health();
            obs.self_max_health = me.max_health();
        }
        // A dead champion needs its gold for shopping, but not an invented map
        // position. Only live champions enter the executor in bot.cpp.
        obs.self_valid = true;
    }
    catch (const std::exception &)
    {
        obs.failed(observation::Section::self);
        return obs;
    }

    obs.enemies_complete = true;
    try
    {
        // Resolve each occupied tile separately: vision::champions() resolves
        // its entire vector before returning, so one stale tile could otherwise
        // discard every readable enemy before our per-record guards run.
        for (const auto tile : turn::champion_tiles(cambc::team::enemy).positions())
        {
            try
            {
                const auto enemy = cambc::vision::champion_at(tile);
                EnemyObs seen;
                // One sdk_champion record covers position/type/health/submitting —
                // the per-field getters each re-issue the same host fetch. A
                // vanished enemy is marked unknown without discarding the whole turn.
                cambc::detail::champion_info raw{};
                bool have_raw = false;
                try
                {
                    const cambc::detail::champion_info *info =
                        cambc::detail::sdk_champion(
                            static_cast<std::uint32_t>(
                                static_cast<cambc::handle_champion_t>(enemy)),
                            &raw) == 0
                            ? &raw
                            : nullptr;
                    if (info)
                    {
                        have_raw = true;
                        seen.position = {info->x, info->y};
                        seen.type = static_cast<cambc::champion_type>(info->type);
                        seen.health = info->health;
                        seen.max_health = info->max_health;
                        seen.submitting = cambc::champion_state::from_bits_retain(info->state)
                                              .contains(cambc::champion_state::submitting_gems);
                    }
                    else
                    {
                        seen.position = enemy.position();
                        seen.type = enemy.champion_name();
                        seen.health = enemy.health();
                        seen.max_health = enemy.max_health();
                        seen.submitting = enemy.is_submitting_gems();
                    }
                }
                catch (const std::exception &)
                {
                    obs.failed(observation::Section::enemies);
                    continue;
                }
                // Keep a known enemy position/HP even if its optional bag or channel
                // metadata is unavailable: it can still threaten our farming champion.
                try
                {
                    seen.gems = enemy.gem_count();
                }
                catch (const std::exception &)
                {
                    obs.failed(observation::Section::enemies);
                }
                // remaining_channel_time throws when no channel is active.
                try
                {
                    seen.channel_left = seen.submitting ? enemy.remaining_channel_time() : 0;
                }
                catch (const std::exception &)
                {
                    obs.failed(observation::Section::enemies);
                }
                obs.enemies.push_back(seen);
                // The tactic modules re-resolve the same tiles through
                // champion_at() — hand them the handle and the record we already
                // have, so their per-enemy reads come from this snapshot.
                turn::note_enemy(seen.position, enemy);
                if (have_raw)
                    turn::seed_enemy(seen.position, raw);
            }
            catch (const std::exception &)
            {
                obs.failed(observation::Section::enemies);
            }
        }
    }
    catch (const std::exception &)
    {
        obs.failed(observation::Section::enemies);
    }
    // The camp layout is immutable, so the group handles are cached for
    // the match — map::camp_groups() is a ~27k-fuel host query on the
    // 42-group map, and vision::camp_groups() re-runs it internally.
    // camp_group::state() also rebuilds the whole vision board per call,
    // so filter the cached handles against one visible_tiles() snapshot
    // and decode lifecycle through the raw imports for in-vision groups.
    obs.camps_complete = true;
    try
    {
        const auto &groups = turn::map_groups();
        const auto &visible = turn::visible_tiles();
        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            // The shared plan and dense lookup have 64 slots. A map beyond that
            // supported capacity is incomplete knowledge, never an out-of-bounds write.
            if (i >= obs.camp_at.size())
            {
                obs.failed(observation::Section::camps);
                break;
            }
            try
            {
                const auto &group = groups[i];
                bool in_vision = false;
                for (const auto &member : group)
                    if (visible.is_set(member.spawn_position()))
                    {
                        in_vision = true;
                        break;
                    }
                if (!in_vision)
                    continue;
                CampObs seen;
                seen.group = static_cast<std::uint8_t>(i);
                std::uint32_t raw = 0;
                cambc::detail::check(cambc::detail::sdk_camp_group_state(
                    static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group)),
                    &raw));
                switch (raw)
                {
                case 1:
                    seen.state = cambc::map::camp_group_alive{};
                    break;
                case 2:
                    seen.state = cambc::map::camp_group_reward_available{};
                    break;
                case 3:
                {
                    std::int32_t timer = 0;
                    cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(
                        static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group)),
                        &timer));
                    seen.state = cambc::map::camp_group_respawning{timer};
                    break;
                }
                default:
                    throw cambc::error{cambc::error_code::api_not_in_vision,
                                       "camp group state is unknown"};
                }
                obs.camps.push_back(seen);
            }
            catch (const std::exception &)
            {
                // Keep earlier camp records and try the remaining groups. Not seeing
                // this group's lifecycle is NOT evidence its monsters were cleared.
                obs.failed(observation::Section::camps);
            }
        }
    }
    catch (const std::exception &)
    {
        obs.failed(observation::Section::camps);
    }
    obs.index_camps();
    try
    {
        if (const auto frame = wire::read())
        {
            obs.wire = *frame;
            obs.wire_valid = true;
        }
        // No valid frame is normal before the first shared-memory publication;
        // retain the established empty-frame fallback, not a fabricated assignment.
    }
    catch (const std::exception &)
    {
        obs.failed(observation::Section::wire);
    }
    return obs;
}
