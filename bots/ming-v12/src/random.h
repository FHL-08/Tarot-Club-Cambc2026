#pragma once
#include <cambc.h>
#include <cstdint>
#include <random>

// Derive repeatable variation from our spawn and this champion's initial
// position. It spreads turn priorities without relying on unavailable WASI
// randomness; identical match inputs still generate the same sequence.
inline std::uint32_t random_seed() {
    const auto spawn = cambc::map::spawn_region(cambc::team::ally).positions()[0];
    std::uint32_t seed = spawn.x * 73856093u ^ spawn.y * 19349663u;
    if (!cambc::is_draft()) {
        const auto start = cambc::self().position();
        seed ^= start.x * 83492791u ^ start.y * 2654435761u;
    }
    return seed;
}

// Draw a value in [0, limit) for turn priority. The generator persists between
// turns, so champions do not repeat their first draw forever. Callers must
// pass a positive limit; there is no valid distribution for limit == 0.
inline unsigned random_number(unsigned limit) {
    static std::mt19937 random{random_seed()};
    return std::uniform_int_distribution<unsigned>{0, limit - 1}(random);
}

