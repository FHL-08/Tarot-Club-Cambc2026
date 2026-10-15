#pragma once
// Draft-memory Plan blob: the compile-time schedule every champion decodes.
// This byte layout is the contract between the draft planner and the runtime
// FSM (docs/strategy.md §Architecture — "seams are data").
//
//   [0]      'F' 'P' magic, [2] version=1, [3] num_groups N, [4] num_slots S,
//            [5] planner flags (breakpoint asymmetry etc.)
//   [6..9]   bag_cap[Role]          gems carried before diverting to bank
//   [10:+S]  role_of_slot[S]        champion slot → Role
//   [+S:+N]  altar_by_group[N]      camp group → altar index (no_index = unset)
//   [+N:+3S] route dir              S × { u16 offset, u8 stop_count }
//   [off..]  route stops            2 bytes each: [ kind:2 | index:6 ], flags
//
// Index spaces follow engine enumeration order, which is deterministic across
// draft and match: camp groups as map::camp_groups(), altars as
// map::altars().positions(), champion slots as the ally handle order.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

enum class Role : std::uint8_t { FARMER = 0, CARRY, ESCORT, FLEX };
inline constexpr std::size_t role_count = 4;

enum class StopKind : std::uint8_t { CAMP = 0, ALTAR = 1, PITSTOP = 2 };

// Stop flags.
// Stand on this group's altar-adjacent tile the round its capture lands, so a
// 1-tick channel sweeps the reward (capture-overlap tribute).
inline constexpr std::uint8_t stop_flag_capture_overlap = 1u << 0;
// Leave the spawn tiles before the capture tick so one capturer takes all.
inline constexpr std::uint8_t stop_flag_vacate = 1u << 1;

struct Stop {
    StopKind kind = StopKind::CAMP;
    // Camp-group index for CAMP, altar index for ALTAR, planner-defined for PITSTOP.
    std::uint8_t index = 0;
    std::uint8_t flags = 0;
    friend bool operator==(const Stop&, const Stop&) = default;
};
static_assert(sizeof(Stop) == 3);

// Non-owning view over one route's stop bytes inside a decoded Plan; valid
// while the Plan object lives. Routes are cyclic: at() wraps, operator[] is strict.
class Route {
public:
    Route() = default;
    [[nodiscard]] std::uint8_t size() const { return static_cast<std::uint8_t>(bytes_.size() / stop_bytes); }
    [[nodiscard]] bool is_empty() const { return bytes_.empty(); }
    [[nodiscard]] Stop at(std::uint32_t i) const
    {
        if (is_empty()) throw std::out_of_range("empty route");
        return (*this)[i % size()];
    }
    [[nodiscard]] Stop operator[](std::size_t i) const
    {
        if (i >= size()) throw std::out_of_range("route stop index");
        const auto b0 = std::to_integer<std::uint8_t>(bytes_[i * stop_bytes]);
        const auto b1 = std::to_integer<std::uint8_t>(bytes_[i * stop_bytes + 1]);
        return Stop{ static_cast<StopKind>(b0 >> 6), static_cast<std::uint8_t>(b0 & 0x3F), b1 };
    }

private:
    friend class Plan;
    static constexpr std::size_t stop_bytes = 2;
    explicit Route(std::span<const std::byte> bytes) : bytes_{ bytes } {}
    std::span<const std::byte> bytes_;
};

class Plan {
public:
    static constexpr std::size_t max_bytes = 1024; // draft-memory capacity
    static constexpr std::uint8_t max_slots = 8;   // C&I doubles a pick → up to 2 bodies per slot
    static constexpr std::uint8_t max_groups = 64; // stop index field is 6 bits
    static constexpr std::uint8_t no_index = 0xFF;

    // Decode is a pure function of the bytes; malformed input throws.
    static Plan decode(const cambc::draft_memory_buffer& buffer)
    {
        return decode_bytes({ buffer.data(), buffer.size() });
    }
    static Plan decode_bytes(std::span<const std::byte> blob)
    {
        if (blob.size() < header_bytes || blob[0] != magic0 || blob[1] != magic1
            || blob[2] != std::byte{ version })
            throw std::invalid_argument("not a foundry plan blob");
        Plan plan;
        plan.num_groups_ = std::to_integer<std::uint8_t>(blob[3]);
        plan.num_slots_ = std::to_integer<std::uint8_t>(blob[4]);
        plan.flags_ = std::to_integer<std::uint8_t>(blob[5]);
        if (plan.num_groups_ > max_groups || plan.num_slots_ > max_slots)
            throw std::invalid_argument("plan census out of range");
        plan.roles_off_ = header_bytes;
        plan.altars_off_ = plan.roles_off_ + plan.num_slots_;
        plan.dir_off_ = plan.altars_off_ + plan.num_groups_;
        const std::size_t dir_end = plan.dir_off_ + dir_entry_bytes * plan.num_slots_;
        if (dir_end > blob.size()) throw std::invalid_argument("truncated plan tables");
        // Route offsets point past the directory; the blob ends at the last stop byte used.
        std::size_t end = dir_end;
        for (std::uint8_t s = 0; s < plan.num_slots_; ++s)
        {
            const std::size_t dir = plan.dir_off_ + dir_entry_bytes * s;
            const std::size_t off = std::to_integer<std::uint8_t>(blob[dir])
                | (std::to_integer<std::uint8_t>(blob[dir + 1]) << 8);
            const std::size_t len = std::to_integer<std::uint8_t>(blob[dir + 2]);
            if (off < dir_end || off + len * 2 > blob.size())
                throw std::invalid_argument("plan route out of bounds");
            for (std::size_t i = 0; i < len; ++i)
            {
                const auto b0 = std::to_integer<std::uint8_t>(blob[off + i * 2]);
                if (b0 >> 6 > static_cast<std::uint8_t>(StopKind::PITSTOP))
                    throw std::invalid_argument("plan stop kind out of range");
            }
            end = std::max(end, off + len * 2);
        }
        plan.size_ = static_cast<std::uint16_t>(end);
        std::copy_n(blob.begin(), end, plan.data_.begin());
        return plan;
    }

    [[nodiscard]] std::uint8_t num_slots() const { return num_slots_; }
    [[nodiscard]] std::uint8_t num_groups() const { return num_groups_; }
    [[nodiscard]] std::uint8_t flags() const { return flags_; }
    [[nodiscard]] std::span<const std::byte> bytes() const { return { data_.data(), size_ }; }

    [[nodiscard]] Role role_at(std::uint8_t slot) const
    {
        if (slot >= num_slots_) throw std::out_of_range("plan slot");
        const auto value = std::to_integer<std::uint8_t>(data_[roles_off_ + slot]);
        if (value >= role_count) throw std::invalid_argument("plan role out of range");
        return static_cast<Role>(value);
    }
    // The route compiled for one champion slot.
    [[nodiscard]] Route route_at(std::uint8_t slot) const
    {
        if (slot >= num_slots_) throw std::out_of_range("plan slot");
        const std::size_t dir = dir_off_ + dir_entry_bytes * slot;
        const std::size_t off = std::to_integer<std::uint8_t>(data_[dir])
            | (std::to_integer<std::uint8_t>(data_[dir + 1]) << 8);
        const std::size_t len = std::to_integer<std::uint8_t>(data_[dir + 2]);
        return Route{ { data_.data() + off, len * 2 } };
    }
    // The route of the first slot assigned this role; empty when unassigned.
    [[nodiscard]] Route route_for(Role role) const
    {
        for (std::uint8_t s = 0; s < num_slots_; ++s)
            if (role_at(s) == role) return route_at(s);
        return Route{};
    }
    // Altar index a group banks at; no_index when the plan leaves it open.
    [[nodiscard]] std::uint8_t altar_for(std::uint8_t group_idx) const
    {
        if (group_idx >= num_groups_) throw std::out_of_range("plan camp group");
        return std::to_integer<std::uint8_t>(data_[altars_off_ + group_idx]);
    }
    // Gem threshold that diverts this role to its next ALTAR stop.
    [[nodiscard]] std::uint8_t bag_cap(Role role) const
    {
        return std::to_integer<std::uint8_t>(data_[bag_cap_off + static_cast<std::size_t>(role)]);
    }

private:
    static constexpr std::byte magic0{ 0x46 }; // 'F'
    static constexpr std::byte magic1{ 0x50 }; // 'P'
    static constexpr std::uint8_t version = 1;
    static constexpr std::size_t header_bytes = 10;
    static constexpr std::size_t bag_cap_off = 6;
    static constexpr std::size_t dir_entry_bytes = 3;

    std::array<std::byte, max_bytes> data_{};
    std::uint16_t size_ = 0;
    std::uint8_t num_groups_ = 0, num_slots_ = 0, flags_ = 0;
    std::size_t roles_off_ = 0, altars_off_ = 0, dir_off_ = 0;
};
static_assert(std::is_trivially_copyable_v<Stop>);

// Structured input a planner fills, then serialises into the blob above.
struct PlanSpec {
    std::uint8_t flags = 0;
    std::array<std::uint8_t, role_count> bag_cap{};
    std::vector<Role> slot_roles;                   // one entry per champion slot
    std::vector<std::uint8_t> altar_by_group;       // camp group → altar index
    std::vector<std::vector<Stop>> routes;          // one route per slot

    [[nodiscard]] std::vector<std::byte> encode() const
    {
        if (slot_roles.size() > Plan::max_slots || altar_by_group.size() > Plan::max_groups
            || routes.size() != slot_roles.size())
            throw std::invalid_argument("bad plan census");
        const std::size_t slots = slot_roles.size(), groups = altar_by_group.size();
        std::size_t stop_bytes_total = 0;
        for (const auto& route : routes)
        {
            if (route.size() > 0xFF) throw std::invalid_argument("route too long");
            for (const auto& stop : route)
            {
                if (stop.index >= Plan::max_groups) throw std::invalid_argument("stop index out of range");
                if (stop.kind == StopKind::CAMP && stop.index >= groups)
                    throw std::invalid_argument("camp stop outside census");
            }
            stop_bytes_total += route.size() * 2;
        }
        const std::size_t dir_off = 10 + slots + groups;
        const std::size_t total = dir_off + 3 * slots + stop_bytes_total;
        if (total > Plan::max_bytes) throw std::invalid_argument("plan exceeds draft memory");

        std::vector<std::byte> blob(total, std::byte{ 0 });
        blob[0] = std::byte{ 0x46 }; // 'F'
        blob[1] = std::byte{ 0x50 }; // 'P'
        blob[2] = std::byte{ 1 };
        blob[3] = std::byte{ static_cast<std::uint8_t>(groups) };
        blob[4] = std::byte{ static_cast<std::uint8_t>(slots) };
        blob[5] = std::byte{ flags };
        for (std::size_t i = 0; i < role_count; ++i) blob[6 + i] = std::byte{ bag_cap[i] };
        for (std::size_t s = 0; s < slots; ++s)
        {
            const auto role = static_cast<std::uint8_t>(slot_roles[s]);
            if (role >= role_count) throw std::invalid_argument("role out of range");
            blob[10 + s] = std::byte{ role };
        }
        for (std::size_t g = 0; g < groups; ++g)
        {
            if (altar_by_group[g] != Plan::no_index && altar_by_group[g] >= Plan::max_groups)
                throw std::invalid_argument("altar index out of range");
            blob[10 + slots + g] = std::byte{ altar_by_group[g] };
        }
        std::size_t off = dir_off + 3 * slots;
        for (std::size_t s = 0; s < slots; ++s)
        {
            blob[dir_off + 3 * s] = std::byte{ static_cast<std::uint8_t>(off & 0xFF) };
            blob[dir_off + 3 * s + 1] = std::byte{ static_cast<std::uint8_t>(off >> 8) };
            blob[dir_off + 3 * s + 2] = std::byte{ static_cast<std::uint8_t>(routes[s].size()) };
            for (const auto& stop : routes[s])
            {
                blob[off++] = std::byte{ static_cast<std::uint8_t>((static_cast<std::uint8_t>(stop.kind) << 6) | stop.index) };
                blob[off++] = std::byte{ stop.flags };
            }
        }
        return blob;
    }
};
