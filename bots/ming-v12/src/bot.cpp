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
#include "warding.h"
#include <cstdint>

namespace {

// Report incomplete vision sparsely, keeping enough fuel for the champion's
// actual move/attack. The mask distinguishes missing self/enemy/camp/wire data;
// a partial observation is not evidence that unseen enemies or camps vanished.
void log_observation(const Obs& obs) {
    static observation::DiagnosticGate gate;
    if (obs.failure_mask != 0 && gate.permit(obs.round, cambc::fuel_remaining()))
        std::cout << "RG4_OBS round=" << obs.round
                  << " self=" << obs.self_valid
                  << " enemies=" << obs.enemies_complete
                  << " camps=" << obs.camps_complete
                  << " failed=" << static_cast<unsigned>(obs.failure_mask) << '\n';
}

} // namespace

// Resolve this champion's assignment from the shared draft plan once. FLEX
// keeps shopping calls valid if its roster slot or draft data is unavailable.
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

// Cache the immutable draft plan in this champion's own process. An empty
// fallback lets the executor use its default route instead of losing turns.
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

// Coordinate one champion turn: observe camps, update respawn knowledge,
// execute one chosen task, then shop and prepare caches if appropriate.
// The Bot object persists, but a fresh observation is gathered every turn.
void Bot::play_turn() {
    auto& me = turn::self();
    const Obs obs = collect();
    log_observation(obs);
    // If even our own action state cannot be read, explicitly yield this turn.
    // Optional vision failures do not enter this branch: collect keeps a valid
    // self snapshot and marks only that part of the world unknown instead.
    if (!obs.self_valid) return;

    // Passive respawn-ledger refresh on the same snapshot — runs even on dead
    // or channelling turns so capture flips are never missed. A ledger error
    // must not cost the turn, so SDK errors are swallowed here.
    try {
        ledger::observe(obs);
    } catch (const cambc::error& err) {
        static observation::DiagnosticGate ledger_gate;
        if (ledger_gate.permit(obs.round, cambc::fuel_remaining()))
            std::cout << "LEDGER_ERR " << err.what() << '\n';
    }

    // Econ runs AFTER the action: shopping before acting starved tick's
    // fuel and the body never moved on round 1. Its first call costs up
    // to ~110k in shop-list init, which left the thinnest body ~500 fuel
    // of margin on round 1 — skip it there (starting gold buys nothing)
    // and let the init land on a later turn.
    if (obs.self_alive)
    {
        const auto action = exec::tick(obs, plan());
        const bool applied = exec::apply(action);
        // A legal-looking decision can still fail at execution (for example a
        // hidden blocker). Record the result once so the next turn's progress
        // monitor can distinguish attempted movement from real economic work.
        exec::record_outcome(obs, action, applied);
    }

    // Recovery at spawn also buys affordable shards toward the existing emblem
    // build. Keep shopping outside self_alive so an unavoidable death does not
    // waste the shop turns during respawn; dying is never a shopping objective.
    if (obs.round > 1)
        econ::maybe_buy(role(), obs);

    exec::warm_statics();

    me.set_priority(1 + random_number(100));

    // Optional trinket work happens after every normal action. It cannot
    // redirect this champion's farming path or delay a carried-gem deposit.
    warding::maybe_place(obs);
}

