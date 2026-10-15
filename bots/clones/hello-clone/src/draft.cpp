// Draft: compile the map atlas once, project the full roster from the fixed
// pick order, and write the planner's Plan blob into draft memory. The pick
// is submitted BEFORE any compile work — a trapped compile must never cost
// the pick; a missed pick is a random champion. The turn-one plan names the
// full projected roster, so later turns only recompile when actual picks
// deviated from the projection (a forced/random fill).
#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>
#include "atlas.h"
#include "plan.h"
#include "planner.h"
#include "clone_config.h"
#include <span>

namespace {

// Pick priority — docs/strategy.md §Draft policy: the fixed order covers the
// roster's needs; counter-drafting stays a tactic concern. Entries past the
// named five exist only so every enum has a place if a pick was forced.
constexpr cambc::champion_draft pick_order[] = {
    cambc::champion_draft::cassi_and_issac,
    cambc::champion_draft::varg,
    cambc::champion_draft::stellar,
    cambc::champion_draft::vendetta,
    cambc::champion_draft::tavi,
    cambc::champion_draft::ren,
    cambc::champion_draft::ryth,
    cambc::champion_draft::marcella,
    cambc::champion_draft::willow,
    cambc::champion_draft::prism,
    cambc::champion_draft::vesper_vale,
};

// One pick → its champion bodies, in the order the engine lists them in the
// ally handle table (draft index ≥4 skips the cassi/issac pair in
// champion_type).
void append_pick(cambc::champion_draft pick, Roster& roster)
{
    if (pick == cambc::champion_draft::cassi_and_issac)
    {
        // Ally handle order lists cassi before issac (verified via SLOT log).
        roster.bodies.push_back(cambc::champion_type::cassi);
        roster.bodies.push_back(cambc::champion_type::issac);
        return;
    }
    const auto raw = static_cast<std::int32_t>(pick);
    roster.bodies.push_back(
        static_cast<cambc::champion_type>(raw + (raw > static_cast<std::int32_t>(cambc::champion_draft::cassi_and_issac) ? 1 : 0)));
}

// The measured per-map comp (issue #84), fingerprinted on
// (width, height, camp-group count) — draft mode exposes no map name and
// the pool's fingerprints are unique. camp_groups() is a host call, so the
// fingerprint binds once per match.
std::span<const cambc::champion_draft> map_picks()
{
    static const std::span<const cambc::champion_draft> picks = [] {
        const auto groups = cambc::map::camp_groups().size();
        for (const auto& mc : clone::draft_table)
            if (mc.w == cambc::map::width() && mc.h == cambc::map::height() && mc.groups == groups)
                return mc.picks;
        return std::span<const cambc::champion_draft>{};
    }();
    return picks;
}

// First comp-table champion our team has not picked; the fixed priority
// list is the tail when the comp runs out or the map is unlisted.
std::optional<cambc::champion_draft> next_pick(const std::vector<cambc::champion_draft>& chosen)
{
    for (const auto want : map_picks())
        if (std::find(chosen.begin(), chosen.end(), want) == chosen.end()) return want;
    for (const auto want : pick_order)
        if (std::find(chosen.begin(), chosen.end(), want) == chosen.end()) return want;
    return std::nullopt;
}

} // namespace

void play_draft()
{
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    const auto rounds = cambc::draft::team_size();

    // Roster = picks already made + the priority projection for the rest of
    // the draft, so the plan names every body slot from turn one.
    Roster roster;
    roster.team_index = cambc::detail::sdk_my_team();
    auto projected = chosen;
    for (const auto pick : chosen) append_pick(pick, roster);
    while (projected.size() < rounds)
    {
        const auto pick = next_pick(projected);
        if (!pick) break;
        projected.push_back(*pick);
        append_pick(*pick, roster);
    }

    try
    {
        // optional<> rather than a magic-static: if build() traps mid-init a
        // guarded static can wedge poisoned, while the optional just stays
        // empty and the next draft turn retries cleanly.
        static std::optional<Atlas> atlas;
        // Deterministic picks make every turn's projected roster identical —
        // recompiling it would only burn later turns' 1x fuel on the plan
        // turn one already wrote. The fuel gate below keeps an unfinished
        // compile from suspending mid-turn and eating a later pick turn.
        static std::vector<cambc::champion_type> compiled_bodies;
        if (compiled_bodies != roster.bodies && cambc::fuel_remaining() >= 350000)
        {
            // The atlas eats most of the 10x turn (lanes: ~725k). The compile
            // must fit in what REMAINS — a mid-compile suspension crashes the
            // draft process entirely and every later pick lands random, so
            // the gate is measured here, not before the atlas build.
            if (!atlas) atlas = atlas::build();
            if (cambc::fuel_remaining() >= 240000)
            {
                const PlanSpec spec = planner::compile(*atlas, roster);
                const auto blob = spec.encode();
                // Draft memory freezes the moment this round's pick lands —
                // the write has to happen now, before pick(), or it is dead bytes.
                cambc::draft_memory = blob;
                compiled_bodies = roster.bodies;

                // Round-trip audit: a blob that decodes to a different slot
                // or group count is silently rewritten as no-plan rather
                // than handed to the runtime half-corrupt.
                const Plan plan = Plan::decode_bytes(blob);
                if (plan.num_slots() != roster.bodies.size()
                    || plan.num_groups() != spec.altar_by_group.size())
                    cambc::draft_memory = {};
            }
        }
    }
    catch (const std::exception&)
    {
        // A failed compile leaves whatever a previous turn wrote; champions
        // fall back to the stub partition when the blob is undecodable.
    }

    // Pick last: the memory write above is rejected once the round's pick is
    // in, and a missed pick is a random champion.
    if (const auto pick = next_pick(chosen))
    {
        cambc::draft::pick(*pick);
        return;
    }
    // Every priority entry already taken — only reachable on a roster
    // repeat; pick anything legal rather than yield the pick to the engine.
    for (int i = 0; i < 11; ++i)
    {
        const auto pick = static_cast<cambc::champion_draft>(i);
        if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end())
        {
            cambc::draft::pick(pick);
            return;
        }
    }
}
