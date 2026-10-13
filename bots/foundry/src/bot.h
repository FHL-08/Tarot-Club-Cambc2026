#pragma once

#include <optional>
#include "plan.h"

// Per-champion state survives between turns.
class Bot {
    int chosen_stat = -1;
    std::optional<Role> role_;

    // This champion's plan role: its index in the ally handle order
    // (plan.h slot space), FLEX when the slot table doesn't cover it.
    Role role();

public:
    // Priorities: shop (spawn or dead), recall, attack enemies in range,
    // bank gems at the econ bag-cap, then farm.
    void play_turn();
};
