#pragma once
#include <cstdint>
#include <limits>

// Small pure rules shared by the executor and compile-time regression tests.
// These do not read the engine or manufacture knowledge of unseen units.
namespace runtime_policy {

// A carrier who can finish the gem race must not wait for the usual bag cap.
// Subtract only before victory, avoiding unsigned overflow in score + bag.
constexpr bool winning_bag(std::uint32_t submitted, std::uint32_t carried,
                           std::uint32_t victory_target) {
    return carried > 0 && submitted < victory_target
        && carried >= victory_target - submitted;
}

// Retreat spends movement before considering a walking-exclusive spell.
// If the escape attempt fails, that unused turn can still defend the carrier;
// a compatible spell never needs to trade away the planned escape step.
constexpr bool escape_spell_allowed(bool excludes_walking, bool escape_failed) {
    return !excludes_walking || escape_failed;
}

// A farmer below 55% HP risks losing a whole respawn cycle to the next camp
// fight. A visible enemy within three tiles raises the escape trigger to 75%:
// recall needs five turn ends and damage interrupts it, so leaving earlier
// gives this champion a chance to walk clear before trying the channel.
// The bag does not change the health trigger; even an empty farmer has value.
constexpr bool should_retreat(float hp, float maximum, bool nearby_enemy) {
    return maximum <= 0 || hp < maximum * 0.55f
        || (nearby_enemy && hp < maximum * 0.75f);
}

// A respawn wait is justified only while a future timer is near. An overdue
// belief requires inspection or reassignment, not another indefinite Wait.
constexpr bool worth_waiting(std::int32_t ready, std::uint32_t round, int travel) {
    return ready > static_cast<std::int32_t>(round)
        && ready - static_cast<std::int32_t>(round) <= travel + 12;
}

// A reward seen long ago may since have been captured by either team. After
// 24 rounds outside vision it merits inspection, not an eternal exclusion.
// This never labels the camp alive or assigns a speculative respawn timer.
constexpr bool stale_reward(std::int32_t ready, std::int32_t last_seen, std::uint32_t round) {
    return ready == std::numeric_limits<std::int32_t>::max() && last_seen >= 0
        && round >= static_cast<std::uint32_t>(last_seen)
        && round - static_cast<std::uint32_t>(last_seen) >= 24;
}

// Rank reachable altars: open routes beat known hostile locks; actual path
// length beats geometric closeness. A planned altar only breaks equal ties.
constexpr bool better_bank(bool hostile, unsigned distance, bool preferred,
                           bool best_hostile, unsigned best_distance, bool best_preferred) {
    if (hostile != best_hostile) return !hostile;
    if (distance != best_distance) return distance < best_distance;
    return preferred && !best_preferred;
}

// A hostile altar lock takes three basic attacks to remove. For a carrier
// already heading there, walking to an open altar is worth the diversion only
// when its extra path length costs fewer turns than clearing that lock.
constexpr bool open_altar_saves_time(unsigned locked_distance, unsigned open_distance) {
    return open_distance < locked_distance
        || static_cast<std::uint64_t>(open_distance) - locked_distance < 3;
}
}
