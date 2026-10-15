#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Pure estimates used only to choose a cast target. They never issue an
// engine action, read hidden units, or change Foundry's route and retreat
// rules.
namespace camp_scoring {

inline constexpr std::size_t max_targets = 24;
inline constexpr std::uint64_t execution_reserve = 22000;
inline constexpr float max_area_radius = 4.0f;

struct Target {
    int x = 0, y = 0;
    float health = 0, strength = 0;
};

// Damage beyond a monster's remaining HP cannot help clear this camp. Clipping
// it stops a large spell from looking valuable just because it overkills.
constexpr float useful_damage(float health, float damage) {
    if (health <= 0 || damage <= 0) return 0;
    return health < damage ? health : damage;
}

// Finishing a monster removes its future retaliation, so a kill earns a
// fixed bonus plus the attack pressure removed — a heuristic, not an exact
// prediction of the engine's target-specific damage modifiers.
constexpr float basic_value(const Target& target, float damage) {
    if (target.health <= 0 || damage <= 0) return 0;
    const float pressure = target.strength > 0 ? target.strength : 0;
    return useful_damage(target.health, damage) + pressure * 0.3f
        + (target.health <= damage ? 80.0f + pressure : 0.0f);
}

// A cast scores the useful damage of its hit plus a one-tick stun credit —
// but only while the target survives: a kill already denies its
// retaliation and must not be paid twice. Measured against foundry-v14,
// adding basic_value's kill credit here regressed local_eval — the payload
// was spent finishing monsters a basic swing would have taken anyway.
constexpr float cast_value(const Target& target, float damage, bool stuns) {
    if (target.health <= 0) return 0;
    return useful_damage(target.health, damage)
        + (stuns && target.health > damage && target.strength > 0 ? target.strength : 0);
}

// CAMBC's square ranges use Chebyshev distance: diagonally adjacent monsters
// are one tile apart. This pure geometry also handles fractional radii.
constexpr bool in_area(int x, int y, const Target& target, float radius) {
    const int dx = x > target.x ? x - target.x : target.x - x;
    const int dy = y > target.y ? y - target.y : target.y - y;
    return radius >= 0 && static_cast<float>(dx > dy ? dx : dy) <= radius;
}

// Add only personally observed, living members of the selected group. Callers
// separately reject spillover into another group before using this benefit.
constexpr float area_value(std::span<const Target> targets, int x, int y,
                           float radius, float damage, bool stuns) {
    float total = 0;
    for (const auto& target : targets)
        if (in_area(x, y, target, radius)) total += cast_value(target, damage, stuns);
    return total;
}

// An empty map cell is safe; a spawn belonging to a different group is not.
// Its public spawn position is known even when its live monster is unseen.
constexpr bool foreign_group(int tile_group, int active_group) {
    return tile_group >= 0 && tile_group != active_group;
}

// Stable tie-breaking makes equal-value shots reproducible: lower ability
// slot first, then the leftmost/topmost target. No random tie-breaking is added.
constexpr bool better(float value, int slot, int x, int y,
                      float best, int best_slot, int best_x, int best_y) {
    if (best_slot < 0) return true;
    if (value != best) return value > best;
    if (slot != best_slot) return slot < best_slot;
    return x < best_x || (x == best_x && y < best_y);
}

// Optional ranking stops before exhausting the fuel needed to perform the
// selected action. This guard is not a claim that arbitrary work is free.
constexpr bool can_score(std::uint64_t fuel) { return fuel > execution_reserve; }

} // namespace camp_scoring
