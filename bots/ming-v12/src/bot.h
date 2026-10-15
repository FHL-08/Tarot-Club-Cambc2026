#pragma once

#include <optional>
#include "plan.h"

// Per-champion state survives between turns.
class Bot {
    std::optional<Role> role_;
    std::optional<Plan> plan_;

    // This champion's plan role: its index in the ally handle order
    // (plan.h slot space), FLEX when the slot table doesn't cover it.
    Role role();

    // The decoded draft blob; an empty Plan when the bytes don't decode so
    // exec still gets a valid reference.
    const Plan& plan();

public:
    // Refresh observations and the camp ledger, execute the chosen action if
    // alive, then shop (after round one). Dead champions can still buy items;
    // the executor protects active channels from ordinary task changes.
    void play_turn();
};

