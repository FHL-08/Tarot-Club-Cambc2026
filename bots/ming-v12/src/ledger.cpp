// The table is plain per-process state: deterministic because every update is
// a pure function of observe()/note_capture() inputs.
#include "ledger.h"

#include <cambc.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <variant>
#include <vector>

#include "obs.h"

namespace {

enum class seen_state : std::uint8_t { unseen, alive, reward, respawning };

struct entry {
    std::int32_t ready_at = 0;        // round the group is next expected alive
    std::int32_t period = 0;          // max_respawn_timer: respawn = capture + period
    std::int32_t predicted = -1;      // open-window audit anchor (a note_capture
                                      // claim, else the first timer-implied round)
    std::int32_t last_seen = -1;      // most recent round the group was in vision
    seen_state state = seen_state::unseen;
};

// Sized from the map's camp layout on first call; camp_groups() is a host
// query, so it is run once and reused all match.
std::vector<entry>& table()
{
    // Initialize one record per camp from static respawn periods. This lambda
    // runs once, avoiding a full map query on every farming decision.
    static std::vector<entry> entries = [] {
        std::vector<entry> init;
        for (const auto& group : cambc::map::camp_groups())
        {
            entry e;
            e.period = group.max_respawn_timer();
            init.push_back(e);
        }
        return init;
    }();
    return entries;
}

} // namespace

// Record a completed camp reward and predict when its monsters return, so a
// route can revisit productive camps instead of repeatedly walking to empties.
// Reject invalid indices rather than corrupt another camp's timing record.
void ledger::note_capture(int group_idx, int round)
{
    auto& entries = table();
    if (group_idx < 0 || static_cast<std::size_t>(group_idx) >= entries.size())
        throw std::out_of_range("ledger group index");
    entry& e = entries[static_cast<std::size_t>(group_idx)];
    e.ready_at = round + e.period;
    e.predicted = e.ready_at;
}

// Expose our current respawn estimate to route planning. Zero means believed
// ready; 'never' means a reward still needs capturing. This is remembered
// information, not a live query of a camp hidden by fog.
int ledger::ready_round(int group_idx)
{
    auto& entries = table();
    if (group_idx < 0 || static_cast<std::size_t>(group_idx) >= entries.size())
        throw std::out_of_range("ledger group index");
    return entries[static_cast<std::size_t>(group_idx)].ready_at;
}

// Return only the timestamp of evidence this champion actually observed.
// Route policy can age out a reward belief without pretending it has respawned.
int ledger::last_seen_round(int group_idx)
{
    auto& entries = table();
    if (group_idx < 0 || static_cast<std::size_t>(group_idx) >= entries.size())
        throw std::out_of_range("ledger group index");
    return entries[static_cast<std::size_t>(group_idx)].last_seen;
}

// Refine camp timing from this turn's observed groups. Visible countdowns
// override predictions; unobserved groups keep their last known state, so
// callers must not treat the ledger as omniscient map information.
void ledger::observe(const Obs& obs)
{
    auto& entries = table();
    const std::int32_t round = static_cast<std::int32_t>(obs.round);
    for (const auto& seen : obs.camps)
    {
        if (seen.group >= entries.size()) continue;
        entry& e = entries[seen.group];
        const bool seen_last_round = e.last_seen == round - 1;
        const seen_state was = e.state;
        e.last_seen = round;

        if (const auto* respawning = std::get_if<cambc::map::camp_group_respawning>(&seen.state))
        {
            // The live timer is the engine's own countdown: implied =
            // round + timer is the respawn round, and it already reflects
            // any occupied-tile delay, so it beats a recorded prediction.
            const std::int32_t implied = round + respawning->timer;
            // A jump of a full period or more means the previous window
            // respawned unseen and a fresh capture started a new one; a
            // smaller rise is an occupied-tile stall inside the same window,
            // which keeps its anchor so the delay shows as a real miss.
            const bool new_window = was != seen_state::respawning || implied >= e.ready_at + e.period;
            if (new_window)
            {
                if (was == seen_state::respawning)
                    e.predicted = -1; // previous window closed unobserved — audit can't resolve
                // A reward→respawning flip under continuous watch means the
                // capture completed at this round's start phase, so
                // capture_round = round exactly and round + period audits the
                // formula against the countdown.
                if (was == seen_state::reward && seen_last_round)
                {
                    note_capture(seen.group, round);
                }
                else if (e.predicted < 0)
                {
                    e.predicted = implied;
                }
            }
            e.ready_at = implied;
            e.state = seen_state::respawning;
        }
        else if (std::holds_alternative<cambc::map::camp_group_reward_available>(seen.state))
        {
            // Dead with the reward pending: no completed capture on record,
            // so no respawn is scheduled. A pending anchor can no longer be
            // audited — its respawn, if any, was never seen.
            e.ready_at = never;
            e.predicted = -1;
            e.state = seen_state::reward;
        }
        else // camp_group_alive
        {
            if (e.predicted >= 0)
            {
                // The window closed on schedule only when the group is seen
                // alive no later than the last implied round; a later
                // sighting leaves the true round ambiguous. Either way the
                // anchor has been spent — drop it.
                e.predicted = -1;
            }
            e.ready_at = 0;
            e.state = seen_state::alive;
        }
    }
}

