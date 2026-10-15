#pragma once
// Respawn-arithmetic table — docs/strategy.md §RUNTIME "ready_at[camp] =
// capture_round + period". Three evidence sources: a group never seen dead is
// assumed alive (ready now), a visible `camp_group_respawning` timer is the
// engine's own countdown (enemy or ally capture, inferred), and a capture we
// completed is exact via note_capture. Replan consensus computes over this
// table across champions, so every update is a pure function of the module
// inputs — nothing here may sample wall-clock, RNG, or hidden process state.
#include <cstdint>

struct Obs;

namespace ledger {

// A group seen dead with no completed capture on record cannot respawn before
// someone captures it; ready_round reports `never` so routes skip it.
inline constexpr int never = 0x7FFF'FFFF;

// Fold one turn's snapshot into the table; walks obs.camps only.
void observe(const Obs& obs);

// Round `group_idx` is next expected alive — `<=` the current round means
// believed ready now, `never` when known dead and unscheduled. Throws
// std::out_of_range for indices outside the map's camp layout.
int ready_round(int group_idx);

// Record a completed capture at `round`: ready_at = round + the group's
// max_respawn_timer, and the prediction is marked self-made for the LEDGER
// predicted-vs-actual audit.
void note_capture(int group_idx, int round);

} // namespace ledger
