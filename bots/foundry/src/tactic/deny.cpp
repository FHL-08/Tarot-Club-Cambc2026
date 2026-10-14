// tactic/deny — production sabotage (factory issue #12), the lowest-priority
// tactic. Two plays, both bounded to a single step of route slack:
//   * contest — an enemy standing on a defeated group's tiles is running the
//     five-round capture; a tile of ours in the region resets it, so step on
//     (or keep standing on) a free tile while they're there;
//   * park — an occupied spawn tile delays the whole group's respawn
//     (GameRules §Camps), so hold a tile of an enemy-claimed respawning
//     group through the end of its countdown.
// Never while hurt, and never a Move off a live capture of our own — the
// region left empty would reset our own timer too.
#include "tactic.h"
#include "support.h"
#include <cambc.h>
#include <cstdint>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

namespace {

// Per-champion claim memory: a reward_available group with an enemy on its
// tiles is being captured by them — marked enemy-owned until the group is
// seen alive again. Pure function of the observation history.
std::vector<char>& enemy_owned()
{
    static std::vector<char> owned;
    if (owned.size() != tactic::groups().size())
        owned.assign(tactic::groups().size(), 0);
    return owned;
}

// Contests and parks trade our turns for theirs one-for-one — the hold is
// capped so a permanent standoff can't starve the route.
constexpr int hold_cap = 8;
int& hold_streak()
{
    static int streak = 0;
    return streak;
}

bool enemy_on(const Obs& obs, int g)
{
    for (const auto& e : obs.enemies)
        if (tactic::group_of(e.position) == g) return true;
    return false;
}

bool reward_pending(const CampObs& c)
{
    return std::holds_alternative<cambc::map::camp_group_reward_available>(c.state);
}

} // namespace

std::optional<exec::Action> tactic::deny(const Obs& obs)
{
    if (obs.self_health < obs.self_max_health * 0.45f) return std::nullopt;
    const int here = tactic::group_of(obs.self_pos);

    // Refresh claim marks from this turn's snapshot.
    for (const auto& c : obs.camps)
    {
        if (std::holds_alternative<cambc::map::camp_group_alive>(c.state))
            enemy_owned()[c.group] = 0;
        else if (reward_pending(c) && enemy_on(obs, c.group))
            enemy_owned()[c.group] = 1;
    }

    // Stepping off a reward tile forfeits the capture standing on it —
    // deny Moves are only issued from neutral ground.
    bool on_reward = false;
    if (const CampObs* c = tactic::seen_camp(obs, here); c && reward_pending(*c))
        on_reward = true;

    const cambc::bitboard ally_tiles = cambc::vision::champion_tiles(cambc::team::ally);
    for (const auto& c : obs.camps)
    {
        if (!reward_pending(c)) continue;
        const int g = c.group;
        if (!enemy_on(obs, g)) continue; // no enemy on it: no capture to reset
        const auto& region = tactic::groups()[g].tiles;
        const auto allies_on = (ally_tiles & region).count();
        if (here == g)
        {
            // Already holding the contest — stay only while no other ally
            // covers it and the streak cap lasts.
            if (allies_on <= 1 && hold_streak() < hold_cap)
            {
                ++hold_streak();
                tactic::alarm_contested(obs, g);
                std::cout << "TACTIC deny: contest @r" << obs.round << '\n';
                return exec::Wait{};
            }
            continue;
        }
        if (on_reward || allies_on > 0 || hold_streak() >= hold_cap) continue;
        cambc::bitboard step_in = region & ~tactic::blocked_tiles();
        bool adjacent = false;
        for (const auto tile : step_in.positions())
            if (obs.self_pos.chebyshev_distance(tile) == 1) { adjacent = true; break; }
        if (!adjacent) continue;
        ++hold_streak();
        tactic::alarm_contested(obs, g);
        std::cout << "TACTIC deny: contest @r" << obs.round << '\n';
        return exec::Move{ std::move(step_in) };
    }

    // Park on an enemy-claimed group's tiles through the last rounds of its
    // countdown — occupation at the deadline pushes the respawn out.
    if (here >= 0 && enemy_owned()[static_cast<std::size_t>(here)]
        && hold_streak() < hold_cap)
        if (const CampObs* c = tactic::seen_camp(obs, here); c)
            if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&c->state);
                r && r->timer <= 2)
            {
                ++hold_streak();
                std::cout << "TACTIC deny: park @r" << obs.round << '\n';
                return exec::Wait{};
            }

    // Every denying path returns above — reaching here releases the streak.
    hold_streak() = 0;
    return std::nullopt;
}
