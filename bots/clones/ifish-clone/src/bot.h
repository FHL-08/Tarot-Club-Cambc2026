#pragma once

#include <optional>
#include "plan.h"

// Per-champion state survives between turns.
class Bot {
    std::optional<Role> role_;
    std::optional<Plan> plan_;
    std::uint8_t wards_placed_ = 0;
    std::uint32_t last_ward_round_ = 0;

    // This champion's plan role: its index in the ally handle order
    // (plan.h slot space), FLEX when the slot table doesn't cover it.
    Role role();

    // The decoded draft blob; an empty Plan when the bytes don't decode so
    // exec still gets a valid reference.
    const Plan& plan();

public:
    // Passive phases (econ buys, ledger observe) run even on dead or
    // channelling turns, then exec's FSM picks the turn's action.
    void play_turn();
};
