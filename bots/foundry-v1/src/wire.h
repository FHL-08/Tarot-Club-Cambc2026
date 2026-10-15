#pragma once
// 8-byte shared-memory codec — the byte layout from
// docs/strategy.md §Shared memory protocol:
//
//   [0]   frame parity: writer's round & 0xFF; entries from another parity
//         are stale and expire (ally turns are sequential within a round,
//         so single-writer-per-slot is safe)
//   [1-2] last enemy sighting: pos x[0-6] | y[7-13] | round tag[14-15];
//         both coords == no_coord marks "no sighting"
//   [3]   contested camp idx[0-3] | alarm altar idx[4-7]
//   [4]   claim mask lo — camp bits 0-7
//   [5]   claim mask hi — camp bits 8-11 [0-3] | relief claim bits [4-7]
//   [6]   tribute alarm: altar idx[0-3] | enemy-channel flag [4]
//   [7]   checksum: bytes 0-6 summed mod 256
//
// Indices are the same engine-order index spaces as plan.h; camp idx and
// altar idx fields are 4 bits, so wire traffic can only name the first 16.
#include <cambc.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace wire {

inline constexpr std::size_t size = 8;
// Field widths; the byte map above shows where each field lives.
inline constexpr unsigned coord_bits = 7;         // maps reach 100×100
inline constexpr unsigned sighting_tag_bits = 2;  // sighting round mod 4
inline constexpr unsigned index_bits = 4;         // contested/alarm/tribute indices
inline constexpr unsigned claim_bits = 12;        // byte 4 + low nibble of byte 5
inline constexpr unsigned relief_bits = 4;        // high nibble of byte 5
inline constexpr unsigned enemy_channel_bit = 4;  // bit inside byte 6
// Sentinels.
inline constexpr std::uint8_t none = 0xF;      // unset 4-bit index
inline constexpr std::uint8_t no_coord = 0x7F; // coord 127 is never a map tile

static_assert(size == 8);
static_assert(coord_bits * 2 + sighting_tag_bits == 16, "sighting fills bytes 1-2");
static_assert(index_bits * 2 == 8, "byte 3 holds two indices");
static_assert(claim_bits - 8 + relief_bits == 8, "byte 5 holds claim hi + relief");

using Bytes = std::array<std::byte, size>;

// One decoded shared-memory frame. Index fields use `none` when unset;
// claim_mask bits 0-11 are camp claims, relief_mask bits 0-3 are relief claims.
struct Frame {
    std::uint8_t parity = 0;            // round & 0xFF of the writing turn
    bool sighting_valid = false;        // bytes 1-2 hold a position
    cambc::vec2 sighting_pos{};         // last spotted enemy tile
    std::uint8_t sighting_tag = 0;      // round & 3 when the sighting was written
    std::uint8_t contested_camp = none; // camp-group idx currently contested
    std::uint8_t alarm_altar = none;    // altar idx under deny/alarm
    std::uint16_t claim_mask = 0;       // camps 0-11 claimed for capture
    std::uint8_t relief_mask = 0;       // champion slots claimed by the relief rule
    std::uint8_t tribute_altar = none;  // altar idx where tribute is channelled
    bool enemy_channel = false;         // an enemy is channelling at tribute_altar
};
static_assert(std::is_trivially_copyable_v<Frame>);

// Writers stamp parity/tag from the live round; readers reject stale frames.
[[nodiscard]] constexpr std::uint8_t parity_of(std::uint32_t round) { return static_cast<std::uint8_t>(round & 0xFF); }
[[nodiscard]] constexpr std::uint8_t tag_of(std::uint32_t round) { return static_cast<std::uint8_t>(round & 0x3); }
[[nodiscard]] constexpr bool is_current(const Frame& f, std::uint32_t round)
{
    return f.parity == parity_of(round);
}
// Rounds since the sighting was written, mod 4 (the tag's resolution).
[[nodiscard]] constexpr std::uint8_t sighting_age(const Frame& f, std::uint32_t round)
{
    return static_cast<std::uint8_t>((tag_of(round) - f.sighting_tag) & 0x3);
}

[[nodiscard]] constexpr std::uint8_t checksum(const Bytes& b)
{
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i + 1 < b.size(); ++i) sum += std::to_integer<std::uint8_t>(b[i]);
    return sum;
}
[[nodiscard]] constexpr bool checksum_ok(const Bytes& b)
{
    return std::to_integer<std::uint8_t>(b[7]) == checksum(b);
}

// Field values wider than their bit width are truncated, not rejected — the
// writer side of the protocol keeps them in range by construction.
[[nodiscard]] constexpr Bytes encode(const Frame& f)
{
    Bytes b{};
    b[0] = std::byte{ f.parity };
    const std::uint8_t x = f.sighting_valid ? static_cast<std::uint8_t>(f.sighting_pos.x) & 0x7F : no_coord;
    const std::uint8_t y = f.sighting_valid ? static_cast<std::uint8_t>(f.sighting_pos.y) & 0x7F : no_coord;
    const std::uint16_t sighting = static_cast<std::uint16_t>(x | (y << coord_bits)
        | ((f.sighting_tag & 0x3) << (2 * coord_bits)));
    b[1] = std::byte{ static_cast<std::uint8_t>(sighting & 0xFF) };
    b[2] = std::byte{ static_cast<std::uint8_t>(sighting >> 8) };
    b[3] = std::byte{ static_cast<std::uint8_t>((f.contested_camp & 0xF) | ((f.alarm_altar & 0xF) << 4)) };
    b[4] = std::byte{ static_cast<std::uint8_t>(f.claim_mask & 0xFF) };
    b[5] = std::byte{ static_cast<std::uint8_t>(((f.claim_mask >> 8) & 0xF) | ((f.relief_mask & 0xF) << 4)) };
    b[6] = std::byte{ static_cast<std::uint8_t>((f.tribute_altar & 0xF) | (f.enemy_channel ? 0x10 : 0)) };
    b[7] = std::byte{ checksum(b) };
    return b;
}

[[nodiscard]] constexpr Frame decode(const Bytes& b)
{
    Frame f;
    f.parity = std::to_integer<std::uint8_t>(b[0]);
    const std::uint16_t sighting = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(b[1]) | (std::to_integer<std::uint8_t>(b[2]) << 8));
    const std::uint8_t x = sighting & 0x7F, y = (sighting >> coord_bits) & 0x7F;
    f.sighting_valid = x != no_coord && y != no_coord;
    f.sighting_pos = { x, y };
    f.sighting_tag = static_cast<std::uint8_t>(sighting >> (2 * coord_bits));
    const auto b3 = std::to_integer<std::uint8_t>(b[3]);
    f.contested_camp = b3 & 0xF;
    f.alarm_altar = b3 >> 4;
    f.claim_mask = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(b[4]) | ((std::to_integer<std::uint8_t>(b[5]) & 0xF) << 8));
    f.relief_mask = std::to_integer<std::uint8_t>(b[5]) >> 4;
    const auto b6 = std::to_integer<std::uint8_t>(b[6]);
    f.tribute_altar = b6 & 0xF;
    f.enemy_channel = (b6 >> enemy_channel_bit) & 1;
    return f;
}

// Raw bytes off the wire, then through the codec.
[[nodiscard]] inline Bytes read_bytes(const cambc::shared_memory_buffer& mem = cambc::shared_memory)
{
    Bytes b{};
    mem.read(0, std::span<std::byte>{ b });
    return b;
}
[[nodiscard]] inline std::optional<Frame> read(const cambc::shared_memory_buffer& mem = cambc::shared_memory)
{
    const Bytes b = read_bytes(mem);
    if (!checksum_ok(b)) return std::nullopt;
    return decode(b);
}
inline void write(const Frame& f, const cambc::shared_memory_buffer& mem = cambc::shared_memory)
{
    const Bytes b = encode(f);
    mem.write(0, std::span<const std::byte>{ b });
}

} // namespace wire
