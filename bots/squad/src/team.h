#pragma once
// The team's 8 bytes of shared memory. Every champion runs its own copy of the
// bot, so this is the only way they can coordinate during a match.
//
// Two squads farm at once: Ryth + Tavi + Willow follow Ryth, and
// Vendetta + Prism farm the camp Vendetta picks.
//
//   byte 0-1  objective tile (x, y): the camp Vendetta's squad is farming, or
//             the map centre when there is nothing to farm (hunting)
//   byte 2    objective stamp, +128 when hunting
//   byte 3-4  Ryth's position (his squad's anchor)
//   byte 5    Ryth stamp, +128 when his squad is hunting
//   byte 6    fight alarm stamp: Ryth was attacked, or the team initiated;
//             +128 once Stellar or Varg owns an emblem (mid game)
//   byte 7    danger alarm stamp: a teammate is hurt and under attack;
//             +128 once Stellar owns three emblems (late game)
//
// A stamp is round % 120 + 1, so 0 means "never written" and age() can tell
// how many rounds ago a message was written.
#include <cambc.h>
#include <cstdint>
#include <optional>

namespace team_memory {

constexpr int period = 120;

inline std::uint8_t now() {
    return static_cast<std::uint8_t>(cambc::round_number() % period + 1);
}

inline int age(std::uint8_t stamp) {
    stamp &= 127;
    if (stamp == 0)
        return 1000;
    return (now() - stamp + period) % period;
}

inline std::uint8_t get(std::size_t i) {
    return static_cast<std::uint8_t>(cambc::shared_memory[i]);
}

inline void put(std::size_t i, std::uint8_t value) {
    cambc::shared_memory[i] = value;
}

struct Objective {
    cambc::vec2 tile;
    bool hunt;
};

inline std::optional<Objective> objective(int max_age = 3) {
    if (age(get(2)) > max_age)
        return std::nullopt;
    return Objective{{get(0), get(1)}, (get(2) & 128) != 0};
}

inline void set_objective(cambc::vec2 tile, bool hunt) {
    put(0, static_cast<std::uint8_t>(tile.x));
    put(1, static_cast<std::uint8_t>(tile.y));
    put(2, static_cast<std::uint8_t>(now() | (hunt ? 128 : 0)));
}

struct Anchor {
    cambc::vec2 tile;
    bool hunt; // Ryth's squad has no camp to farm and is hunting
};

inline std::optional<Anchor> anchor(int max_age = 2) {
    if (age(get(5)) > max_age)
        return std::nullopt;
    return Anchor{{get(3), get(4)}, (get(5) & 128) != 0};
}

inline void set_anchor(cambc::vec2 tile, bool hunt) {
    put(3, static_cast<std::uint8_t>(tile.x));
    put(4, static_cast<std::uint8_t>(tile.y));
    put(5, static_cast<std::uint8_t>(now() | (hunt ? 128 : 0)));
}

inline bool fight_alarm(int max_age = 4) { return age(get(6)) <= max_age; }
inline void raise_fight() { put(6, static_cast<std::uint8_t>(now() | (get(6) & 128))); }

inline bool danger_alarm(int max_age = 4) { return age(get(7)) <= max_age; }
inline void raise_danger() { put(7, static_cast<std::uint8_t>(now() | (get(7) & 128))); }

inline bool mid_game() { return (get(6) & 128) != 0; }
inline void set_mid_game() { put(6, static_cast<std::uint8_t>(get(6) | 128)); }
inline bool late_game() { return (get(7) & 128) != 0; }
inline void set_late_game() { put(7, static_cast<std::uint8_t>(get(7) | 128)); }

} // namespace team_memory
