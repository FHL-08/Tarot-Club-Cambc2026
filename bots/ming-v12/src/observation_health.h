#pragma once
#include <cstdint>

// Observation failures are uncertainty, not evidence that the map is empty.
// This small, SDK-independent policy is also exercised by compile-time tests.
namespace observation {

enum class Section : std::uint8_t { self = 1, enemies = 2, camps = 4, wire = 8 };

struct Health {
    bool self_valid = false;
    // Complete means all legally visible records were read successfully, never
    // that we know what enemy champions or camps are doing behind fog of war.
    bool enemies_complete = false;
    bool camps_complete = false;
    bool wire_valid = false;
    std::uint8_t failure_mask = 0;

    // Mark only the failed source unknown. An unreadable enemy must never erase
    // our own movement/attack allowance or the camps already observed this turn.
    constexpr void failed(Section section) {
        failure_mask |= static_cast<std::uint8_t>(section);
        switch (section) {
        case Section::self: self_valid = false; break;
        case Section::enemies: enemies_complete = false; break;
        case Section::camps: camps_complete = false; break;
        case Section::wire: wire_valid = false; break;
        }
    }
};

struct DiagnosticGate {
    std::uint32_t last_round = 0;
    bool reported = false;

    // A persistent API problem should be visible in a replay log without
    // consuming the champion's entire turn printing the same error repeatedly.
    constexpr bool permit(std::uint32_t round, std::uint64_t fuel) {
        if (fuel < 6000 || (reported && round - last_round < 32)) return false;
        last_round = round;
        reported = true;
        return true;
    }
};

} // namespace observation
