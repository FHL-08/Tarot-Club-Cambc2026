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
#include "clone_config.h"
#include <limits>
#include <cstdint>

namespace {

// Free ward: relocate it toward this body's current goal each time the
// trinket recharges — measured clones place ~21-28 per team per game and
// wards are permanent vision, so they ride the travel axis (issue #84).
void clone_ward(const Obs& obs, const exec::Action& action, std::uint8_t& placed, std::uint32_t& last)
{
    auto& me = turn::self();
    // The budget is a team total — this body rides its equal share.
    static const std::uint32_t share = [] {
        std::size_t bodies = 4;
        try { bodies = std::max<std::size_t>(1, cambc::champions(cambc::team::ally).size()); }
        catch (const std::exception&) { }
        return static_cast<std::uint32_t>(
            (clone::ward_per_body + bodies - 1) / bodies);
    }();
    try
    {
        if (me.ward_cooldown() > 0 || placed >= share
            || (last != 0 && obs.round < last + clone::ward_min_gap))
            return;
        const auto tiles = me.ward_placement_tiles();
        if (tiles.is_empty()) return;
        const auto* mv = std::get_if<exec::Move>(&action);
        if (!mv) return; // standing turns keep the current ward
        cambc::vec2 toward = obs.self_pos;
        std::int64_t best_d = std::numeric_limits<std::int64_t>::max();
        for (const auto t : mv->goals.positions())
            if (const auto d = obs.self_pos.chebyshev_distance(t); d < best_d)
            { best_d = d; toward = t; }
        cambc::vec2 best = obs.self_pos;
        best_d = std::numeric_limits<std::int64_t>::max();
        for (const auto t : tiles.positions())
            if (const auto d = t.chebyshev_distance(toward); d < best_d)
            { best_d = d; best = t; }
        // A failed attempt (hidden occupant) still throttles via `last`,
        // but only a placed ward consumes the per-champion quota.
        if (me.place_ward(best)) ++placed;
        last = obs.round;
        std::cout << "WARD r" << obs.round << " @" << best.x << ',' << best.y << '\n';
    }
    catch (const std::exception&) { }
}

} // namespace

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
        const auto action = exec::tick(obs, plan());
        exec::apply(action);
        clone_ward(obs, action, wards_placed_, last_ward_round_);
    }

    if (obs.round > 1)
        econ::maybe_buy(role(), obs);

    exec::warm_statics();

    me.set_priority(1 + random_number(100));
}
