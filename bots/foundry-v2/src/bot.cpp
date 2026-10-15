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
        const auto& me = cambc::self();
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
    auto& me = cambc::self();

    // Shopping is legal on spawn tiles and while dead — econ runs before the
    // alive check. Vision queries can fail on dead turns, so keep a minimal
    // snapshot for the shop path then.
    Obs obs;
    try {
        obs = collect();
    } catch (const std::exception&) {
        obs.round = cambc::round_number();
        obs.self_gold = me.gold_count();
    }
    econ::maybe_buy(role(), obs);

    // Passive respawn-ledger refresh on the same snapshot — runs even on dead
    // or channelling turns so capture flips are never missed. A ledger error
    // must not cost the turn, so SDK errors are swallowed here.
    try {
        ledger::observe(obs);
    } catch (const cambc::error& err) {
        std::cout << "LEDGER_ERR " << err.what() << '\n';
    }

    if (!me.is_alive()) return;

    exec::apply(exec::tick(obs, plan()));

    me.set_priority(1 + random_number(100));
}
