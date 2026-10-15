#pragma once
// Pure, constant-cost route bookkeeping: moving A -> B -> A must not look
// productive forever just because each individual move was legal. The caller
// supplies distances to the same camp or chosen altar, and resets on real work.
#include <cstdint>
#include <limits>

namespace progress {

inline constexpr std::uint16_t unreachable = std::numeric_limits<std::uint16_t>::max();

class Tracker {
    bool active_ = false;
    int objective_ = -1;
    std::uint16_t best_ = unreachable;
    std::uint32_t last_observed_ = 0;
    std::uint32_t last_progress_ = 0;

public:
    // Successful damage, capture, tribute, death/respawn, or an intentional
    // change of goal geometry starts a new journey, rather than a stale timeout.
    constexpr void reset() { *this = Tracker{}; }

    // Observe path distance, not Euclidean distance or whether move() succeeded.
    // A new BEST distance earns more travel time; revisiting old tiles does not.
    // Objective keys must stay stable while pursuing the same camp/altar. If a
    // live target genuinely changes the goal geometry, the caller resets first.
    constexpr void observe(int objective, std::uint32_t round, std::uint16_t distance)
    {
        if (!active_ || objective_ != objective || round < last_observed_)
        {
            active_ = true;
            objective_ = objective;
            best_ = distance;
            last_progress_ = round;
        }
        else if (distance < best_)
        {
            best_ = distance;
            last_progress_ = round;
        }
        last_observed_ = round;
    }

    // Do not charge a legitimate channel, stun, healing turn, or unavailable
    // movement allowance against the route. Unlike reset(), suspension keeps
    // the best distance and previous unproductive time, so cooldowns cannot
    // continually grant a looping champion a completely fresh timeout window.
    constexpr void pause(std::uint32_t round)
    {
        if (!active_) return;
        if (round < last_observed_)
        {
            reset();
            return;
        }
        last_progress_ += round - last_observed_;
        last_observed_ = round;
    }

    // The executor may replan after this many ACTIVE rounds without new path
    // progress. Arrival counts once, not forever: repeatedly asking to move
    // while already at the goal without doing work still deserves replanning.
    // The caller pauses/resets for legitimate capture, tribute, healing, etc.
    // Unreachable distances never count as progress.
    [[nodiscard]] constexpr bool expired(std::uint32_t round, std::uint32_t window) const
    {
        return active_ && round >= last_progress_
            && round - last_progress_ >= window;
    }

    // Expose the distance record for a compact debug message, without querying
    // fog-of-war state or spending fuel on another map search.
    [[nodiscard]] constexpr std::uint16_t best_distance() const { return best_; }
};

} // namespace progress
