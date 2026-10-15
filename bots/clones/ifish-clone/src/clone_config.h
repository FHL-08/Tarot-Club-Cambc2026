#pragma once
// ifish-clone: measured policy table from issue #84's replay study
// (~3,300 decoded games). Chassis is foundry v14 — this header is the only
// per-team file; every clone source file reads it for the team deltas.
#include <cambc.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace clone {

// Attack-on-sight: an enemy inside the attack ring is hit on the focus
// cadence (nearest first). When it steps out, the same target is chased
// for up to this many consecutive turns before the route resumes
// (measured ~3, p90 6).
inline constexpr int chase_cap = 3;
// Flat hp trigger for the recall home — the threat lift is foundry's, not
// measured policy.
inline constexpr float recall_hp = 0.5f;
// Ward budget: the measured team placements per game (issue #93); each
// body rides its equal share, paced by the minimum round gap.
inline constexpr std::uint8_t ward_per_body = 20;
inline constexpr std::uint32_t ward_min_gap = 60;
// Bank when the carried bag reaches this many gems.
inline constexpr std::uint32_t trib_cap = 44;

// Champion-focus share (issue #93): an enemy champion in the attack ring
// takes the hit on every Nth in-range turn — measured ~1/N of attacks
// land on champions. Targeting is nearest-in-range; lowest-hp% was a v1
// misread (median attacked-target hp% ~0.5-0.6).
inline constexpr int champ_focus_n = 10;
// Wait signature (issue #93): never = always take an action, drifting to
// the nearest objective when idle; respawn_wait = yield only while a camp
// respawn sits within respawn_wait_rounds; heavy = yield most no-action
// turns (every yield_mod'th reposition still moves).
enum class YieldMode : std::uint8_t { never, respawn_wait, heavy };
inline constexpr YieldMode yield_mode = YieldMode::never;
inline constexpr int yield_mod = 3;
inline constexpr int respawn_wait_rounds = 8;
// Nav orders (issue #93): true = re-path on any objective change (hello's
// measured ~4x rate); false = re-path only when the target invalidates.
inline constexpr bool nav_repath_on_change = false;

// Draft comp per map, fingerprinted on (width, height, camp-group count) —
// draft mode exposes no map name and the pool's fingerprints are unique.
struct MapComp {
    std::int32_t w, h;
    std::size_t groups;
    std::span<const cambc::champion_draft> picks;
};

inline constexpr cambc::champion_draft collision_picks[] = {
    cambc::champion_draft::ren, cambc::champion_draft::stellar, cambc::champion_draft::cassi_and_issac };
inline constexpr cambc::champion_draft corners_picks[] = {
    cambc::champion_draft::ren, cambc::champion_draft::stellar, cambc::champion_draft::cassi_and_issac,
    cambc::champion_draft::ryth, cambc::champion_draft::varg };
// Elkring shares corners' measured order.
inline constexpr cambc::champion_draft expanse_picks[] = {
    cambc::champion_draft::vendetta, cambc::champion_draft::cassi_and_issac, cambc::champion_draft::varg };
inline constexpr cambc::champion_draft interlock_picks[] = {
    cambc::champion_draft::ren, cambc::champion_draft::stellar, cambc::champion_draft::cassi_and_issac,
    cambc::champion_draft::ryth };
inline constexpr cambc::champion_draft lanes_picks[] = {
    cambc::champion_draft::vendetta, cambc::champion_draft::cassi_and_issac, cambc::champion_draft::varg,
    cambc::champion_draft::ryth, cambc::champion_draft::ren };
inline constexpr cambc::champion_draft monolith_picks[] = {
    cambc::champion_draft::ren, cambc::champion_draft::stellar, cambc::champion_draft::cassi_and_issac,
    cambc::champion_draft::ryth, cambc::champion_draft::varg };
inline constexpr cambc::champion_draft packs_picks[] = {
    cambc::champion_draft::ren, cambc::champion_draft::stellar, cambc::champion_draft::cassi_and_issac };
inline constexpr cambc::champion_draft rows_picks[] = {
    cambc::champion_draft::vendetta, cambc::champion_draft::cassi_and_issac, cambc::champion_draft::varg,
    cambc::champion_draft::ryth };

inline constexpr MapComp draft_table[] = {
    { 41, 41, 20, collision_picks },
    { 71, 71, 30, corners_picks },
    { 30, 30, 10, corners_picks },
    { 80, 80, 30, expanse_picks },
    { 40, 40, 16, interlock_picks },
    { 90, 30, 42, lanes_picks },
    { 50, 50, 22, monolith_picks },
    { 31, 31, 8, packs_picks },
    { 50, 50, 9, rows_picks },
};

// Itemisation: (shard, per-game team total) in the measured buy order.
// Each body banks its equal share (total / bodies) before the queue
// advances; the last entry then repeats open-ended.
struct EconItem { cambc::item_type item; std::uint32_t team_total; };

inline constexpr EconItem econ_queue[] = {
    { cambc::item_type::power_shard_of_strength, 2 },
    { cambc::item_type::power_shard_of_focus, 2 },
    { cambc::item_type::durability_shard_of_health, 4 },
    { cambc::item_type::bruiser_shard_of_health, 10 },
    { cambc::item_type::jungle_shard_of_strength, 6 },
    { cambc::item_type::jungle_shard_of_focus, 7 },
};

} // namespace clone
