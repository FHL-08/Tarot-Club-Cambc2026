#pragma once
// Per-turn live-read cache shared by exec and the tactic modules. Vision
// can change after our own move, cast or attack. Live memos key on the
// round and a mutation epoch; added same-turn followups invalidate before
// reading again so dead monsters and spent allowances cannot remain cached.
// Each champion runs its own WASI instance, so the statics are per-body.
// Entries only ever reuse a value the engine already returned this turn;
// a read that threw still throws (rethrown or skipped exactly as the
// caller did before), so no decision input can differ.
#include <cambc.h>
#include <array>
#include <cstdint>
#include <exception>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace turn {

// The historical name is retained for callers; this epoch now also changes
// after combat. Every champion has its own instance, not a team-wide counter.
inline std::uint32_t& move_epoch()
{
    static std::uint32_t e = 0;
    return e;
}

// Navigation calls this after moving, so later decisions do not reuse the old firing position.
inline void note_moved() { ++move_epoch(); }

// A cast/attack can kill a monster, reveal a target or spend an allowance.
// Refresh all live snapshots before choosing another action in the same turn.
inline void note_action() { ++move_epoch(); }

// Refresh after a new turn or action; the references store the new cache stamp.
inline bool stale(std::uint32_t& round, std::uint32_t& epoch)
{
    const std::uint32_t r = cambc::round_number();
    if (r == round && epoch == move_epoch()) return false;
    round = r;
    epoch = move_epoch();
    return true;
}

// self() re-resolves the same constant handle on every call — cache it
// once per process. Only used from match turns (never the draft).
[[nodiscard]] inline const cambc::champion& self()
{
    static const cambc::champion v = cambc::self();
    return v;
}

// One sdk_champion record covers every live-stat accessor — the SDK's
// per-field getters each re-issue the identical host fetch, so a per-turn
// snapshot is strictly cheaper and returns the same values.
[[nodiscard]] inline const cambc::detail::champion_info* self_info()
{
    static std::uint32_t r = 0;
    static std::optional<cambc::detail::champion_info> v;
    static std::uint32_t e = 0;
    if (stale(r, e))
    {
        v.reset();
        cambc::detail::champion_info info{};
        if (cambc::detail::sdk_champion(static_cast<std::uint32_t>(
                static_cast<cambc::handle_champion_t>(self())), &info)
            == 0)
            v = info;
    }
    return v ? &*v : nullptr;
}

// Self state/stats off the same per-turn record; the direct accessor is
// the fallback for the collect-threw path (dead/channelled turns never
// reach the tick callers that use these).
[[nodiscard]] inline cambc::champion_state self_state()
{
    if (const auto* i = self_info())
        return cambc::champion_state::from_bits_retain(i->state);
    return self().state();
}
// Reuse the current basic-attack strength when comparing damage against camps or champions.
[[nodiscard]] inline float self_strength()
{
    if (const auto* i = self_info()) return i->strength;
    return self().strength();
}
// Give combat and movement the same attack reach without repeated engine calls.
[[nodiscard]] inline std::int64_t self_attack_range()
{
    if (const auto* i = self_info()) return i->attack_range;
    return self().attack_range();
}

// Cache the currently reachable basic-attack tiles for target selection this turn.
[[nodiscard]] inline const cambc::bitboard& attack_tiles()
{
    static std::uint32_t r = 0;
    static cambc::bitboard v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v = self().attack_tiles();
    return v;
}

// Cache only this champion's visible map tiles; unseen camps remain unknown to live policy.
[[nodiscard]] inline const cambc::bitboard& visible_tiles()
{
    static std::uint32_t r = 0;
    static cambc::bitboard v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v = cambc::vision::visible_tiles();
    return v;
}

// Visible occupied tiles guide pathfinding and checks for allies already holding a camp.
[[nodiscard]] inline const cambc::bitboard& champion_tiles(cambc::team side)
{
    static std::uint32_t r[2]{};
    static cambc::bitboard v[2];
    const int i = side == cambc::team::ally ? 0 : 1;
    static std::uint32_t e[2]{};
    if (stale(r[i], e[i])) v[i] = cambc::vision::champion_tiles(side);
    return v[i];
}

// Live visible monster positions block ordinary movement toward farming destinations.
[[nodiscard]] inline const cambc::bitboard& camp_tiles()
{
    static std::uint32_t r = 0;
    static cambc::bitboard v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v = cambc::vision::camp_tiles();
    return v;
}

// Include visible teleport occupancy when building the movement-obstacle mask.
[[nodiscard]] inline const cambc::bitboard& teleport_tiles()
{
    static std::uint32_t r = 0;
    static cambc::bitboard v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v = cambc::vision::teleport_tiles();
    return v;
}

// The legality mask nav/exec/tactic all rebuild — walls/holes/altars and
// the enemy spawn region are static map data, so only the three vision
// masks renew per turn.
[[nodiscard]] inline const cambc::bitboard& blocked_tiles()
{
    static std::uint32_t r = 0;
    static cambc::bitboard v;
    static const cambc::bitboard hard = cambc::map::walls() | cambc::map::holes()
        | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
    static std::uint32_t e = 0;
    if (stale(r, e))
    {
        v = hard | champion_tiles(cambc::team::ally) | champion_tiles(cambc::team::enemy)
            | camp_tiles() | teleport_tiles();
        v.set(self().position(), false);
    }
    return v;
}

// Static map layout — one query for the whole match, shared by collect(),
// econ and every geo cache instead of a static per translation unit.
[[nodiscard]] inline const auto& map_groups()
{
    static const auto v = cambc::map::camp_groups();
    return v;
}

// Spawn geometry never changes: reuse it for homeward travel and enemy-side classification.
[[nodiscard]] inline const cambc::bitboard& spawn_region(cambc::team side)
{
    static const cambc::bitboard ally = cambc::map::spawn_region(cambc::team::ally);
    static const cambc::bitboard enemy = cambc::map::spawn_region(cambc::team::enemy);
    return side == cambc::team::ally ? ally : enemy;
}

// The ability array binds self's fixed slots — the owner handle is
// constant for the match and every accessor re-queries live state, so a
// single copy serves every caller.
[[nodiscard]] inline const std::array<cambc::ability, 2>& abilities()
{
    static const std::array<cambc::ability, 2> v = self().abilities();
    return v;
}

// valid_targets() is a host call re-fetching fixed design data — the
// mask cannot change mid-match, so one read per slot is enough.
[[nodiscard]] inline const cambc::ability_target_mask& slot_targets(std::uint32_t slot)
{
    static std::optional<cambc::ability_target_mask> v[2];
    if (!v[slot]) v[slot] = abilities()[slot].valid_targets();
    return *v[slot];
}

// Cooldown ticks between turns and starts after a cast. Reads before an action
// share one query; note_action invalidates it before followups. A throw is retried on the next call, matching
// the uncached behaviour. is_ready() is !cooldown && can_cast — the
// can_cast bit already rides in the per-turn self_info snapshot collect()
// fetched, so only the cooldown query remains a host call.
[[nodiscard]] inline bool slot_ready(std::uint32_t slot)
{
    static std::uint32_t r[2]{};
    static bool v[2]{};
    static std::uint32_t e[2]{};
    if (stale(r[slot], e[slot]))
    {
        const auto& ab = abilities()[slot];
        v[slot] = ab.info().has_value() && !ab.is_on_cooldown()
            && self_info()
            && cambc::champion_state::from_bits_retain(self_info()->state)
                   .contains(cambc::champion_state::can_cast);
    }
    return v[slot];
}

// mask_into() re-resolves the owner handle and redraws the chebyshev disc
// every call; the mask is a pure function of self position. A cached
// throw is replayed so callers see the same exception they would have.
[[nodiscard]] inline const cambc::bitboard& slot_reach(std::uint32_t slot)
{
    static std::uint32_t r[2]{};
    static cambc::bitboard v[2];
    static std::exception_ptr ep[2];
    static std::uint32_t e[2]{};
    if (stale(r[slot], e[slot]))
    {
        ep[slot] = nullptr;
        try
        {
            abilities()[slot].mask_into(v[slot]);
        }
        catch (...)
        {
            ep[slot] = std::current_exception();
        }
    }
    if (ep[slot]) std::rethrow_exception(ep[slot]);
    return v[slot];
}

// One lazily-filled live bundle per visible enemy tile. Each field is
// read at most once per turn and shared across peel/gank/burst; a nullopt
// means the read threw (never retried — the engine state cannot change
// mid-turn, so a retry would throw again identically).
struct EnemyLive {
    bool handle_tried = false;
    std::optional<cambc::champion> handle;
    std::optional<cambc::detail::champion_info> info;
    bool info_tried = false;
    std::optional<bool> recalling;
    std::optional<std::int32_t> channel_left;
    std::optional<bool> stunned;
    std::optional<std::int64_t> attack_range;
};

// Share enemy reads across tactics this turn, discarding them when the view may have changed.
inline std::vector<std::pair<cambc::vec2, EnemyLive>>& enemy_lives()
{
    static std::uint32_t r = 0;
    static std::vector<std::pair<cambc::vec2, EnemyLive>> v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v.clear();
    return v;
}

// collect() already enumerates visible enemy champions — seeding the
// handle here spares every tactic module its own champion_at() lookup.
inline void note_enemy(cambc::vec2 pos, const cambc::champion& handle)
{
    auto& v = enemy_lives();
    for (auto& [p, e] : v)
        if (p == pos)
        {
            e.handle = handle;
            e.handle_tried = true;
            return;
        }
    v.emplace_back(pos, EnemyLive{});
    v.back().second.handle = handle;
    v.back().second.handle_tried = true;
}

// Resolve a currently visible enemy once; an unreadable tile leaves an empty handle, not a guess.
inline EnemyLive& enemy(cambc::vec2 pos)
{
    auto& v = enemy_lives();
    for (auto& [p, e] : v)
        if (p == pos) return e;
    v.emplace_back(pos, EnemyLive{});
    auto& e = v.back().second;
    e.handle_tried = true;
    try
    {
        e.handle = cambc::vision::champion_at(pos);
    }
    catch (const cambc::error&)
    {
    }
    return e;
}

// Seed the snapshot collected this turn — later per-enemy accessor calls
// then hit the same record instead of re-fetching it.
inline void seed_enemy(cambc::vec2 pos, const cambc::detail::champion_info& info)
{
    auto& e = enemy(pos);
    e.info = info;
    e.info_tried = true;
}

// Return a usable enemy handle for targeting, or nullptr when that tile cannot be resolved.
[[nodiscard]] inline const cambc::champion* enemy_at(cambc::vec2 pos)
{
    auto& e = enemy(pos);
    return e.handle ? &*e.handle : nullptr;
}

// Same one-call consolidation as self_info: recalling/attack_range (and
// any future live field) all read the single state record.
[[nodiscard]] inline const cambc::detail::champion_info* enemy_info(cambc::vec2 pos)
{
    auto& e = enemy(pos);
    if (!e.handle) return nullptr;
    if (!e.info_tried)
    {
        e.info_tried = true;
        cambc::detail::champion_info v{};
        if (cambc::detail::sdk_champion(
                static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(*e.handle)),
                &v)
            == 0)
            e.info = v;
    }
    return e.info ? &*e.info : nullptr;
}

// Recall is interruptible by damage; unknown is distinct from a confirmed non-recalling enemy.
[[nodiscard]] inline std::optional<bool> enemy_recalling(cambc::vec2 pos)
{
    const auto* info = enemy_info(pos);
    if (!info) return std::nullopt;
    return cambc::champion_state::from_bits_retain(info->state)
        .contains(cambc::champion_state::recalling);
}

// Estimate how long a visible channel can still be interrupted; failed reads provide no timer.
[[nodiscard]] inline std::optional<std::int32_t> enemy_channel_left(cambc::vec2 pos)
{
    auto& e = enemy(pos);
    if (!e.handle) return std::nullopt;
    if (!e.channel_left)
        try
        {
            e.channel_left = e.handle->remaining_channel_time();
        }
        catch (const cambc::error&)
        {
        }
    return e.channel_left;
}

// Tactics avoid wasting a fresh stun on an already-stunned target when another target is useful.
[[nodiscard]] inline std::optional<bool> enemy_stunned(cambc::vec2 pos)
{
    auto& e = enemy(pos);
    if (!e.handle) return std::nullopt;
    if (!e.stunned)
        try
        {
            e.stunned = e.handle->buffs().has_stun();
        }
        catch (const cambc::error&)
        {
        }
    return e.stunned;
}

// A visible enemy's attack reach contributes to immediate danger estimates for shields and healing.
[[nodiscard]] inline std::optional<std::int64_t> enemy_attack_range(cambc::vec2 pos)
{
    const auto* info = enemy_info(pos);
    if (!info) return std::nullopt;
    return static_cast<std::int64_t>(info->attack_range);
}

// Visible allies with the stats the tactic modules read — one
// enumeration plus one stat bundle per turn instead of gank and burst
// each walking the roster. health/max_health stay lazy: gank never reads
// them, so a throwing read must only cost the reader that asked.
struct AllyLive {
    cambc::champion handle;
    cambc::vec2 pos{};
    float strength = 0;
    std::int64_t attack_range = 0;
    mutable std::optional<cambc::detail::champion_info> info;
    mutable bool info_tried = false;
    mutable std::optional<float> health;
    mutable std::optional<float> max_health;
};

// Collect visible allies once so gank support and defensive spell selection share one snapshot.
[[nodiscard]] inline const std::vector<AllyLive>& allies()
{
    static std::uint32_t r = 0;
    static std::vector<AllyLive> v;
    static std::uint32_t e = 0;
    if (stale(r, e))
    {
        v.clear();
        // One sdk_champion record per visible ally replaces the per-field
        // position/strength/attack_range host calls — same values.
        for (const auto& ally : cambc::vision::champions(cambc::team::ally))
        {
            AllyLive a{ ally };
            cambc::detail::champion_info info{};
            if (cambc::detail::sdk_champion(static_cast<std::uint32_t>(
                    static_cast<cambc::handle_champion_t>(ally)), &info) == 0)
            {
                a.info = info;
                a.info_tried = true;
                a.pos = { info.x, info.y };
                a.strength = info.strength;
                a.attack_range = info.attack_range;
            }
            else
            {
                a.pos = ally.position();
                a.strength = ally.strength();
                a.attack_range = ally.attack_range();
            }
            v.push_back(std::move(a));
        }
    }
    return v;
}

// Lazy health reads are paid only when a tactic considers helping or comparing this ally.
[[nodiscard]] inline std::optional<float> ally_health(const AllyLive& a)
{
    if (!a.health)
        try
        {
            if (!a.info_tried)
            {
                a.info_tried = true;
                cambc::detail::champion_info info{};
                if (cambc::detail::sdk_champion(static_cast<std::uint32_t>(
                        static_cast<cambc::handle_champion_t>(a.handle)), &info) == 0)
                    a.info = info;
            }
            a.health = a.info ? a.info->health : a.handle.health();
        }
        catch (const cambc::error&)
        {
        }
    return a.health;
}

// Maximum HP turns an ally's health into a fraction for heal/shield urgency checks.
[[nodiscard]] inline std::optional<float> ally_max_health(const AllyLive& a)
{
    if (!a.max_health)
        try
        {
            if (!a.info_tried)
            {
                a.info_tried = true;
                cambc::detail::champion_info info{};
                if (cambc::detail::sdk_champion(static_cast<std::uint32_t>(
                        static_cast<cambc::handle_champion_t>(a.handle)), &info) == 0)
                    a.info = info;
            }
            a.max_health = a.info ? a.info->max_health : a.handle.max_health();
        }
        catch (const cambc::error&)
        {
        }
    return a.max_health;
}

// Draft roster — fixed for the match (a body reads it for the grouped
// opening's capture arbitration).
[[nodiscard]] inline const std::vector<cambc::champion>& roster(cambc::team side)
{
    static const std::vector<cambc::champion> ally = cambc::champions(cambc::team::ally);
    static const std::vector<cambc::champion> enemy = cambc::champions(cambc::team::enemy);
    return side == cambc::team::ally ? ally : enemy;
}

// Roster positions — one read per body per round, not per arbitration
// iteration. Reads stay lazy per index: a throwing position (dead or
// unresolvable body) must surface at the same loop step the uncached
// read would have, after earlier indices already voted.
[[nodiscard]] inline cambc::vec2 roster_position(cambc::team side, std::size_t idx)
{
    static std::uint32_t r[2]{};
    static std::vector<std::optional<cambc::vec2>> v[2];
    static std::uint32_t e[2]{};
    const int i = side == cambc::team::ally ? 0 : 1;
    if (stale(r[i], e[i])) v[i].assign(roster(side).size(), std::nullopt);
    if (!v[i][idx]) v[i][idx] = roster(side)[idx].position();
    return *v[i][idx];
}

// Scoreboard totals are frozen for the turn — the engine only ticks
// tributes at owner turn end, which no bot call can advance mid-turn.
[[nodiscard]] inline std::int64_t submitted_gems(cambc::team side)
{
    static std::uint32_t r[2]{};
    static std::int64_t v[2]{};
    static std::uint32_t e[2]{};
    const int i = side == cambc::team::ally ? 0 : 1;
    if (stale(r[i], e[i])) v[i] = cambc::global_submitted_gems(side);
    return v[i];
}

// Reuse the fixed altar list when searching for a place to bank carried gems.
[[nodiscard]] inline const std::vector<cambc::vec2>& altars()
{
    static const std::vector<cambc::vec2> v = cambc::map::altars().positions();
    return v;
}

// damage_amount against a champion — host call priced per (slot, target)
// and queried by both gank's lethal check and burst's scoring, so share
// the read for the turn. A thrown read is cached as thrown: no decision
// input can differ from the uncached version.
[[nodiscard]] inline std::optional<float> damage_amount(std::uint32_t slot, cambc::vec2 target)
{
    static std::uint32_t r = 0;
    static std::vector<std::tuple<std::uint32_t, cambc::vec2, float>> v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v.clear();
    for (const auto& [s, t, d] : v)
        if (s == slot && t == target) return d;
    const auto* en = enemy_at(target);
    if (!en) return std::nullopt;
    try
    {
        const float d = abilities()[slot].damage_amount(*en);
        v.emplace_back(slot, target, d);
        return d;
    }
    catch (const cambc::error&)
    {
        return std::nullopt;
    }
}

// Altar locks: one altar_locks() snapshot (two host calls) replaces the
// per-altar has_altar_lock + altar_lock_at chain — up to three calls per
// altar per bank turn. Both queries are vision-scoped to the same set, so
// membership in the snapshot is exactly has_altar_lock's true case, and
// the recorded team answers is_enemy without a second fetch.
[[nodiscard]] inline const std::vector<cambc::altar_lock>& altar_locks()
{
    static std::uint32_t r = 0;
    static std::vector<cambc::altar_lock> v;
    static std::uint32_t e = 0;
    if (stale(r, e)) v = cambc::vision::altar_locks();
    return v;
}

// Only our own UseItem changes lock state mid-turn, and nothing reads
// the flag after placing one.
[[nodiscard]] inline bool altar_locked(cambc::vec2 altar)
{
    for (const auto& lock : altar_locks())
        if (lock.position() == altar) return true;
    return false;
}

// An enemy lock blocks our banking; a friendly one should not be mistaken for a hostile obstacle.
[[nodiscard]] inline bool altar_lock_is_enemy(cambc::vec2 altar)
{
    for (const auto& lock : altar_locks())
        if (lock.position() == altar) return lock.is_enemy();
    return false;
}

} // namespace turn

