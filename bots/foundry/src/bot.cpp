// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <iostream>
#include "econ.h"
#include "exec.h"
#include "ledger.h"
#include "obs.h"
#include "plan.h"
#include "random.h"
#include <cstdint>

Role Bot::role() {
    if (role_) return *role_;
    Role resolved = Role::FLEX;
    try {
        const Plan plan = Plan::decode(cambc::draft_memory);
        const auto allies = cambc::champions(cambc::team::ally);
        const auto& me = turn::self();
        for (std::uint8_t slot = 0; slot < allies.size(); ++slot)
            if (allies[slot] == me) {
                resolved = slot < plan.num_slots() ? plan.role_at(slot) : Role::FLEX;
                break;
            }
    } catch (const std::exception&) {
        // No decodable plan: FLEX is the safe default queue.
    }
    role_ = resolved;
    return resolved;
}

const Plan& Bot::plan() {
    if (!plan_) {
        try {
            plan_ = Plan::decode(cambc::draft_memory);
        } catch (const std::exception&) {
            plan_ = Plan{}; // undecodable blob: exec's stub route carries on without it
        }
    }
    return *plan_;
}

void Bot::play_turn() {
    auto& me = turn::self();
    // Fuel suspension commits a turn's buffered action on the resume round
    // (GameRules §fuel) — a first turn that overruns the 2× budget lands
    // its move on round 2-3 even when apply() accepted it. Round 1 nobody
    // is visible from spawn, so collect()'s vision/camp/wire scans (~64k
    // fuel) read empty vectors anyway; snapshot self state only and leave
    // the full collect for round 2 onward.
    Obs obs;
    try {
        obs = collect();
    } catch (const std::exception&) {
        obs.round = cambc::round_number();
        obs.self_gold = me.gold_count();
    }

    // Passive respawn-ledger refresh on the same snapshot — runs even on dead
    // or channelling turns so capture flips are never missed. A ledger error
    // must not cost the turn, so SDK errors are swallowed here.
    try {
        ledger::observe(obs);
    } catch (const cambc::error& err) {
        std::cout << "LEDGER_ERR " << err.what() << '\n';
    }

    // Econ runs AFTER the action: shopping before acting starved tick's
    // fuel and the body never moved on round 1. Its first call costs up
    // to ~110k in shop-list init, which left the thinnest body ~500 fuel
    // of margin on round 1 — skip it there (starting gold buys nothing)
    // and let the init land on a later turn.
    if (me.is_alive())
    {
        exec::apply(exec::tick(obs, plan()));
    }

    if (obs.round > 1)
        econ::maybe_buy(role(), obs);

    exec::warm_statics();

    me.set_priority(1 + random_number(100));
}
