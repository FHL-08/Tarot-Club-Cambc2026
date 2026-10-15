#pragma once
#include <cambc.h>
#include <cstdint>
#include <array>
#include <optional>

// Two squads farm gems: A = Ryth (tank, leads) + Tavi + Stellar,
// B = Issac (leads) + Cassi + Prism. They fight enemy champions in sight unless clearly outmatched.
enum class Role { tank, fighter, marksman, support, mage, farmer };

// Per-champion state survives between this champion's turns (not shared).
class Bot {
    bool initialised = false;
    Role role = Role::fighter;
    float last_health = 0;
    std::uint32_t shards_bought = 0;
    std::optional<cambc::vec2> farm_target; // a spawn tile of the chosen camp group
    bool hunting = false;                   // Ryth: no camp to farm, squad hunts
    bool squad_a = true;                    // follows Ryth (A) or the objective (B)
    std::uint32_t enemy_seen_round = 0;     // last round an enemy champion was in sight

    void init();
    void shop();
    void track_damage();
    bool survive();
    bool bank();
    bool shopping_trip();
    bool fighting_back() const;
    bool farm_nearby_wolf(const std::array<cambc::ability, 2>& abilities, std::optional<cambc::vec2> leader);
    void choose_objective();

    void tank_turn();
    void fighter_turn();
    void marksman_turn();
    void support_turn();
    void mage_turn();
    void farmer_turn();

public:
    void play_turn();
};
