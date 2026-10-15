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
// seen alive again or recomputed enemy-free (our own capture clearing the
// mark keeps us off our own respawn). Pure function of observation history.
std::vector<char>& enemy_owned()
{
    static std::vector<char> owned;
    if (owned.size() != tactic::groups().size())
        owned.assign(tactic::groups().size(), 0);
    return owned;
}

// Per-group session budget: each hold spends one charge, refilled only when
// the group is seen alive again — a standoff can never spend more than the
// budget's worth of route slack on a single enemy claim.
constexpr int hold_budget = 8;
// Remember remaining contest turns so sabotaging one camp cannot consume the whole farming route.
std::vector<int>& budget()
{
    static std::vector<int> b;
    if (b.size() != tactic::groups().size())
        b.assign(tactic::groups().size(), hold_budget);
    return b;
}

// Only visible enemy occupants justify contesting this camp; unseen occupancy is not assumed.
bool enemy_on(const Obs& obs, int g)
{
    for (const auto& e : obs.enemies)
        if (tactic::group_of(e.position) == g) return true;
    return false;
}

// A defeated camp awaiting capture can be contested; a live monster pack cannot be captured yet.
bool reward_pending(const CampObs& c)
{
    return std::holds_alternative<cambc::map::camp_group_reward_available>(c.state);
}

} // namespace

// Spend a bounded amount of nearby route time delaying enemy rewards, while preserving our capture.
std::optional<exec::Action> tactic::deny(const Obs& obs)
{
    // Aligns with exec's threatened-pitstop rule: deny is pure gravy, so a
    // hurt champion with an enemy in reach keeps fleeing instead of holding.
    bool threatened = false;
    for (const auto& e : obs.enemies)
        if (obs.self_pos.chebyshev_distance(e.position) <= 3) { threatened = true; break; }
    if (obs.self_health < obs.self_max_health * 0.45f
        || (threatened && obs.self_health < obs.self_max_health * 0.6f))
        return std::nullopt;
    const int here = tactic::group_of(obs.self_pos);

    // Refresh claim marks and budgets from this turn's snapshot.
    for (const auto& c : obs.camps)
    {
        if (std::holds_alternative<cambc::map::camp_group_alive>(c.state))
        {
            enemy_owned()[c.group] = 0;
            budget()[c.group] = hold_budget;
        }
        else if (reward_pending(c))
            // Recompute every sighting: an enemy on the region claims it;
            // no enemy (our capture in progress, or theirs already done)
            // un-marks so the park branch never holds our own respawn.
            enemy_owned()[c.group] = enemy_on(obs, c.group) ? 1 : 0;
    }

    // Stepping off a reward tile forfeits the capture standing on it —
    // deny Moves are only issued from neutral ground.
    bool on_reward = false;
    if (const CampObs* c = tactic::seen_camp(obs, here); c && reward_pending(*c))
        on_reward = true;

    const cambc::bitboard& ally_tiles = turn::champion_tiles(cambc::team::ally);
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
            // covers it and the group's budget still has charge.
            if (allies_on <= 1 && budget()[g] > 0)
            {
                --budget()[g];
                tactic::alarm_contested(obs, g);
                std::cout << "TACTIC deny: contest @r" << obs.round << '\n';
                return exec::Wait{};
            }
            continue;
        }
        if (on_reward || allies_on > 0 || budget()[g] <= 0) continue;
        cambc::bitboard step_in = region & ~tactic::blocked_tiles();
        bool adjacent = false;
        for (const auto tile : step_in.positions())
            if (obs.self_pos.chebyshev_distance(tile) == 1) { adjacent = true; break; }
        if (!adjacent) continue;
        --budget()[g];
        tactic::alarm_contested(obs, g);
        std::cout << "TACTIC deny: contest @r" << obs.round << '\n';
        return exec::Move{ std::move(step_in) };
    }

    // Park on an enemy-claimed group's tiles through the last rounds of its
    // countdown — occupation at the deadline pushes the respawn out.
    if (here >= 0 && enemy_owned()[static_cast<std::size_t>(here)]
        && budget()[static_cast<std::size_t>(here)] > 0)
        if (const CampObs* c = tactic::seen_camp(obs, here); c)
            if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&c->state);
                r && r->timer <= 2)
            {
                --budget()[static_cast<std::size_t>(here)];
                std::cout << "TACTIC deny: park @r" << obs.round << '\n';
                return exec::Wait{};
            }

    return std::nullopt;
}

