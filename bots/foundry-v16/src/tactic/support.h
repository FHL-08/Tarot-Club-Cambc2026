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
#include "../turn.h"
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
        for (const auto& group : turn::map_groups())
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
    return (g >= 0 && g < 64) ? obs.camp_at[static_cast<std::size_t>(g)] : nullptr;
}

// The same legality mask nav.cpp/exec.cpp build — tactic Moves pre-mask goal
// tiles so distance fields only seed landable ground. Served from the
// per-turn cache: the boards behind it can't change until apply() moves us.
[[nodiscard]] inline const cambc::bitboard& blocked_tiles()
{
    return turn::blocked_tiles();
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
    for (const auto tile : turn::spawn_region(side).positions())
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

} // namespace tactic
