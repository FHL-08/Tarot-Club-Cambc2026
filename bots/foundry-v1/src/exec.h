#pragma once
// The runtime FSM — ordered-guard decision list from docs/strategy.md
// §RUNTIME, executed against the stub planner (a greedy route rebuilt from
// the ledger each turn; compiled plan routes land with #8).
//
// tick is a pure decision: same (Obs, Plan, ledger state) → same Action.
// It performs engine *reads* only (self flags, vision occupancy, static
// map data); the returned Action carries plain data so tactic modules can
// emit one without touching the SDK. apply() is the single write seam.
#include <cambc.h>
#include <cstdint>
#include <variant>

struct Obs;
class Plan;

namespace exec {

// Goal-seeded move: apply() descends onto the best free tile via nav::step.
struct Move {
    cambc::bitboard goals;
};

// Basic attack on whatever occupies `target` at apply time.
struct Attack {
    cambc::vec2 target{};
    enum class Kind : std::uint8_t { camp, champion };
    Kind kind = Kind::camp;
};

// Ability slot cast: ground → use(tile), unit → resolved at `target`.
struct Cast {
    std::uint8_t slot = 0;
    cambc::vec2 target{};
    bool ground = true;
};

// tribute → submit_gems(altar); recall → recall channel home.
struct Channel {
    enum class Kind : std::uint8_t { tribute, recall };
    Kind kind = Kind::tribute;
    cambc::vec2 altar{};
};

struct Buy {
    cambc::item_type item{};
    std::uint32_t count = 0;
};

// Consumable use: altar locks place on an altar tile (range 2), teleport
// crystals start a channel toward a landing tile. The engine validates
// inventory and range at apply time.
struct UseItem {
    cambc::consumable_type item{};
    cambc::vec2 target{};
};

struct Wait {};

using Action = std::variant<Move, Attack, Cast, Channel, Buy, UseItem, Wait>;

// Guard order: channel-continuation → tactic::offer → capture-stay →
// pitstop → bank → route (contested/respawning filtered) → haul → idle.
// Ledger access is the ledger:: table itself — read-only from here.
[[nodiscard]] Action tick(const Obs& obs, const Plan& plan);

// Performs the action against the engine. False when nothing ran — a
// stale target, a move with no improving step, or a plain Wait.
bool apply(const Action& action);

} // namespace exec
