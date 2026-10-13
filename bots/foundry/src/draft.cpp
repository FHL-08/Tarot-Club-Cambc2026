#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include "plan.h"
#include "wire.h"
#include "obs.h"
#include "random.h"

namespace {

// Stand-in plan until the planner lands: fixed bytes every draft writes so
// champions always share one decode target.
std::vector<std::byte> fixed_plan_blob() {
    PlanSpec spec;
    spec.flags = 1;
    spec.bag_cap = {12, 40, 8, 25}; // FARMER, CARRY, ESCORT, FLEX
    spec.slot_roles = {Role::FARMER, Role::CARRY, Role::FLEX};
    spec.altar_by_group = {2, 0, Plan::no_index, 1};
    spec.routes = {
        {{StopKind::CAMP, 0, 0}, {StopKind::CAMP, 1, stop_flag_capture_overlap}, {StopKind::ALTAR, 2, 0}},
        {{StopKind::CAMP, 2, 0}, {StopKind::PITSTOP, 0, 0}},
        {},
    };
    return spec.encode();
}

// Codec check: decode the blob and compare every field against the spec,
// once on the in-process bytes and once re-read through the engine.
bool plan_self_test(std::span<const std::byte> blob) {
    bool ok = true;
    const auto check = [&](bool cond) { ok = ok && cond; };

    const Plan plan = Plan::decode_bytes(blob);
    check(plan.num_slots() == 3 && plan.num_groups() == 4 && plan.flags() == 1);
    check(plan.bag_cap(Role::FARMER) == 12 && plan.bag_cap(Role::CARRY) == 40);
    check(plan.bag_cap(Role::ESCORT) == 8 && plan.bag_cap(Role::FLEX) == 25);
    check(plan.role_at(0) == Role::FARMER && plan.role_at(1) == Role::CARRY);
    check(plan.altar_for(0) == 2 && plan.altar_for(2) == Plan::no_index);
    const auto farmer = plan.route_for(Role::FARMER);
    check(farmer.size() == 3);
    check(farmer[0] == Stop{StopKind::CAMP, 0, 0});
    check(farmer[1] == Stop{StopKind::CAMP, 1, stop_flag_capture_overlap});
    check(farmer[2] == Stop{StopKind::ALTAR, 2, 0});
    const auto carry = plan.route_at(1);
    check(carry.size() == 2 && carry.at(3) == carry[1]); // routes are cyclic
    check(plan.route_for(Role::ESCORT).is_empty());

    // Through the engine: a fresh buffer re-reads the bytes submitted above.
    const cambc::draft_memory_buffer reread;
    const Plan back = Plan::decode(reread);
    check(back.num_slots() == 3 && back.bag_cap(Role::FARMER) == 12);
    check(back.route_at(0).size() == 3 && back.altar_for(2) == Plan::no_index);

    // Wire codec round-trip while we're here.
    wire::Frame frame;
    frame.parity = wire::parity_of(37);
    frame.sighting_valid = true;
    frame.sighting_pos = {42, 17};
    frame.sighting_tag = wire::tag_of(37);
    frame.contested_camp = 5;
    frame.alarm_altar = 3;
    frame.claim_mask = 0x0A53;
    frame.relief_mask = 0x5;
    frame.tribute_altar = 3;
    frame.enemy_channel = true;
    auto bytes = wire::encode(frame);
    check(wire::checksum_ok(bytes));
    const auto again = wire::decode(bytes);
    check(again.sighting_valid && again.sighting_pos == cambc::vec2{42, 17});
    check(again.sighting_tag == wire::tag_of(37) && wire::sighting_age(again, 38) == 1);
    check(again.contested_camp == 5 && again.alarm_altar == 3 && again.claim_mask == 0x0A53);
    check(again.relief_mask == 0x5 && again.tribute_altar == 3 && again.enemy_channel);
    bytes[4] ^= std::byte{1};
    check(!wire::checksum_ok(bytes));
    return ok;
}

} // namespace

void play_draft() {
    // Draft memory carries the plan blob (replaces the starter greeting).
    const auto blob = fixed_plan_blob();
    cambc::draft_memory = blob;

    // Round-trip self-test once, on the first draft turn.
    static bool self_tested = false;
    if (!self_tested) {
        self_tested = true;
        try {
            const bool ok = plan_self_test(blob);
            std::cout << "plan self-test " << (ok ? "PASS" : "FAIL") << " blob=" << blob.size() << "B" << std::endl;
        } catch (const std::exception& error) {
            std::cout << "plan self-test FAIL: " << error.what() << std::endl;
        }
    }

    // Choose randomly from the champions our team has not picked.
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    std::vector<cambc::champion_draft> available;
    for (int i = 0; i < 11; ++i) { // Released champions only.
        auto pick = static_cast<cambc::champion_draft>(i);
        if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end()) available.push_back(pick);
    }

    cambc::draft::pick(available.at(random_number(available.size())));
}
