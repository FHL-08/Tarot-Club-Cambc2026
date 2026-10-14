#pragma once
// Shared geography and alarm helpers for the tactic/*.cpp modules.
// Everything here is a pure function of immutable map data or the caller's
// Obs — duplicated from exec.cpp's private cache deliberately so the tactic
// layer never reaches into another module's anonymous namespace.
#include <cambc.h>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#include "../obs.h"
#include "../wire.h"

namespace tactic {

// Camp layouts are immutable — the host layout query runs once per module
// translation unit, then serves every turn.
struct GroupGeo {
    cambc::bitboard tiles;
    std::vector<cambc::vec2> tile_list;
};

[[nodiscard]] inline const std::vector<GroupGeo>& groups()
{
    static const std::vector<GroupGeo> cache = [] {
        std::vector<GroupGeo> out;
        for (const auto& group : cambc::map::camp_groups())
        {
            GroupGeo geo;
            group.spawn_tiles_into(geo.tiles);
            geo.tile_list = geo.tiles.positions();
            out.push_back(std::move(geo));
        }
        return out;
    }();
    return cache;
}

// Spawn tile → group index, -1 elsewhere.
[[nodiscard]] inline int group_of(cambc::vec2 p)
{
    static const std::vector<std::int16_t> table = [] {
        const std::int32_t w = cambc::map::width(), h = cambc::map::height();
        std::vector<std::int16_t> t(static_cast<std::size_t>(w) * h, -1);
        for (std::size_t g = 0; g < groups().size(); ++g)
            for (const auto tile : groups()[g].tile_list)
                t[static_cast<std::size_t>(tile.y) * w + tile.x] = static_cast<std::int16_t>(g);
        return t;
    }();
    return table[static_cast<std::size_t>(p.y) * cambc::map::width() + p.x];
}

[[nodiscard]] inline const CampObs* seen_camp(const Obs& obs, int g)
{
    for (const auto& c : obs.camps)
        if (c.group == g) return &c;
    return nullptr;
}

// The same legality mask nav.cpp/exec.cpp build — tactic Moves pre-mask goal
// tiles so distance fields only seed landable ground.
[[nodiscard]] inline cambc::bitboard blocked_tiles()
{
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(cambc::team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

// Altar index space = map::altars().positions() order — the wire byte-6
// alarm field names altars by this index (4 bits, first 16 only).
[[nodiscard]] inline const std::vector<cambc::vec2>& altars()
{
    static const std::vector<cambc::vec2> cache = cambc::map::altars().positions();
    return cache;
}

[[nodiscard]] inline int altar_index(cambc::vec2 altar)
{
    for (std::size_t i = 0; i < altars().size(); ++i)
        if (altars()[i] == altar) return static_cast<int>(i);
    return -1;
}

// Chebyshev distance to the nearest tile of a team's spawn region — an altar
// is "enemy-side" when it sits closer to their spawn than to ours.
[[nodiscard]] inline std::int64_t dist_to_spawn(cambc::vec2 p, cambc::team side)
{
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto tile : cambc::map::spawn_region(side).positions())
        best = std::min(best, p.chebyshev_distance(tile));
    return best;
}

// Merge fields into this round's wire frame. Ally turns are sequential
// within a round, so the read-modify-write of a frame already decoded into
// Obs cannot race; a stale-parity frame is discarded wholesale because its
// fields expired with the old round.
template <class F>
inline void wire_merge(const Obs& obs, F&& mutate)
{
    wire::Frame frame;
    if (wire::is_current(obs.wire, obs.round))
        frame = obs.wire;
    else
        frame.parity = wire::parity_of(obs.round);
    static_cast<F&&>(mutate)(frame);
    wire::write(frame);
}

// sighting fields = "a verified gank target was here this round" — only
// gank writes them, so a fresh sighting is an unambiguous converge call.
inline void alarm_sighting(const Obs& obs, cambc::vec2 pos)
{
    wire_merge(obs, [&](wire::Frame& f) {
        f.sighting_valid = true;
        f.sighting_pos = pos;
        f.sighting_tag = wire::tag_of(obs.round);
    });
}

// Byte 6 = tribute alarm: an enemy channel is running at this altar.
inline void alarm_channel(const Obs& obs, int altar_idx)
{
    wire_merge(obs, [&](wire::Frame& f) {
        f.enemy_channel = true;
        if (altar_idx >= 0 && altar_idx < 16)
            f.tribute_altar = static_cast<std::uint8_t>(altar_idx);
    });
}

// Byte 3 high nibble = the altar under deny/lock alarm.
inline void alarm_altar(const Obs& obs, int altar_idx)
{
    if (altar_idx < 0 || altar_idx >= 16) return;
    wire_merge(obs, [&](wire::Frame& f) { f.alarm_altar = static_cast<std::uint8_t>(altar_idx); });
}

// Byte 3 low nibble = the camp group currently being contested.
inline void alarm_contested(const Obs& obs, int group)
{
    if (group < 0 || group >= 16) return;
    wire_merge(obs, [&](wire::Frame& f) { f.contested_camp = static_cast<std::uint8_t>(group); });
}

// Kill-race primitives (issue #39): the win condition is 1000 banked
// gems, so survival matters only as bag custody — a body that dies after
// banking lost nothing, while a dead carrier hands the killer the bag
// plus 50g (GameRules §Death). dps is damage per own turn at reach
// attack_range + 1 (one step closes the gap next turn).

[[nodiscard]] inline float basic_dps_on(float strength, int attack_range,
    cambc::vec2 from, cambc::vec2 to)
{
    return from.chebyshev_distance(to) <= attack_range + 1 ? strength : 0;
}

// Enemy ability cooldowns aren't readable per-slot; amortise 2×focus over
// the 8-turn cooldown, the same burst term my_dps uses for our kits. The
// whole term is reach-gated — a visible enemy that can't close next turn
// is no threat at all.
[[nodiscard]] inline float enemy_dps_on(const EnemyObs& e, cambc::vec2 target)
{
    if (e.position.chebyshev_distance(target) > e.attack_range + 1) return 0;
    return e.strength + 2.f * e.focus / 8.f;
}

// Our own slots are readable: basic term plus the best ready damage
// payload on this target — one cast per turn, so it's max, not a sum.
[[nodiscard]] inline float self_dps_on(const cambc::champion& target)
{
    const auto& me = cambc::self();
    float dps = basic_dps_on(me.strength(), me.attack_range(), me.position(), target.position());
    float burst = 0;
    for (const auto& ab : me.abilities())
        if (ab.is_ready() && (ab.is_damage() || ab.is_aoe_damage()))
            burst = std::max(burst, ab.damage_amount(target));
    return dps + burst;
}

// Ally live reads are vision-gated (engine error 97 past ~8 tiles) — an
// unreadable ally contributes nothing rather than faulting the turn.
[[nodiscard]] inline float ally_dps_on(const cambc::champion& ally, cambc::vec2 target)
{
    try
    {
        return basic_dps_on(ally.strength(), ally.attack_range(), ally.position(), target);
    }
    catch (const cambc::error&) { return 0; }
}

// All visible enemy dps landing on `target` next turn.
[[nodiscard]] inline float incoming_dps(const Obs& obs, cambc::vec2 target)
{
    float total = 0;
    for (const auto& e : obs.enemies)
        if (e.health > 0) total += enemy_dps_on(e, target);
    return total;
}

// Turns until incoming damage kills the body; infinite when nobody can
// reach it.
[[nodiscard]] inline float time_to_die(const Obs& obs)
{
    const float incoming = incoming_dps(obs, obs.self_pos);
    return incoming > 0 ? obs.self_health / incoming
        : std::numeric_limits<float>::infinity();
}

// Our side's dps on a visible enemy: self basic+burst plus every readable
// ally's basic term.
[[nodiscard]] inline float our_dps_on(const EnemyObs& e)
{
    const auto& me = cambc::self();
    float dps = basic_dps_on(me.strength(), me.attack_range(), me.position(), e.position);
    try
    {
        if (const auto handle = cambc::vision::champion_at(e.position); handle.is_visible())
            dps = self_dps_on(handle);
    }
    catch (const cambc::error&) { }
    for (const auto& ally : cambc::champions(cambc::team::ally))
    {
        if (ally == me) continue;
        dps += ally_dps_on(ally, e.position);
    }
    return dps;
}

[[nodiscard]] inline float time_to_kill(const EnemyObs& e)
{
    const float dps = our_dps_on(e);
    return dps > 0 ? e.health / dps : std::numeric_limits<float>::infinity();
}

} // namespace tactic
