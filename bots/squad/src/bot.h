#pragma once
#include <cambc.h>
#include <cstdint>
#include <array>
#include <optional>
#include <vector>
#include "team.h"

// One pack farms gems: every champion converges on the camp the leader
// publishes, yield-parks cleared camps through their respawn, fights while
// healthy, and only full-bag carriers detour to an altar.
enum class Role { tank, fighter, marksman, support, mage, farmer };

// Shared per-turn view, so every champion reads the same facts.
struct Context {
    std::vector<cambc::champion> enemies;
    std::vector<cambc::champion> allies;
    std::optional<team_memory::Objective> objective;
    std::optional<cambc::vec2> anchor; // Ryth's position, if fresh
    bool hunt = false; // the pack is hunting (no camp to farm)
    std::vector<cambc::champion> threats; // every enemy in sight off its spawn; enemies = the ones to engage
};

// Per-champion state survives between this champion's turns (not shared).
class Bot {
    bool initialised = false;
    Role role = Role::fighter;
    float last_health = 0;
    std::uint32_t shards_bought = 0;
    std::optional<cambc::vec2> farm_target; // a spawn tile of the chosen camp group
    bool hunting = false;                   // pack leader: no camp to farm

    void init();
    void shop();
    void track_damage();
    bool survive();
    bool bank();
    bool shopping_trip();
    bool fighting_back() const;
    void choose_objective();

    void pack_turn();
    void pack_fight(const Context&, const std::array<cambc::ability, 2>&);
    void kite_farm(const Context&, const std::array<cambc::ability, 2>&);
    void pack_farm(const Context&, const std::array<cambc::ability, 2>&);

public:
    void play_turn();
};
