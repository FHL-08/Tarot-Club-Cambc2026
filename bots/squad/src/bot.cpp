// Champion decisions. main.cpp dispatches here after the draft.
//
// Every turn: shop, track damage, survive (retreat/recall), bank gems, then run
// this champion's role. Roles fight when engaged and farm in formation otherwise.
#include <cambc.h>
#include "bot.h"
#include "team.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using cambc::bitboard;
using cambc::team;
using cambc::vec2;

// Unspent gold at which a champion recalls to shop.
constexpr std::uint32_t shop_trip_gold = 150;

// Health share at which Ryth / everyone else retreats.
constexpr float retreat_tank = 0.3f;
constexpr float retreat_other = 0.35f;
// Value bonus for staying on the current camp target.
constexpr double stick_bonus = 1.3;

// A back-liner farms a gem cluster on its own within this many tiles.
constexpr int solo_cluster_radius = 16;

// Join a cleared camp's capture (to share its gold) from this many tiles away.
// Tested 4 / 6 / 8: 345 / 351 / 348 of 380 league games; without joining: 301.
constexpr int join_capture_range = 6;

// Fall back instead of fighting when nearby enemies are this much stronger.
constexpr float outmatched_ratio = 1.3f;

// Chase a foe only at or above this health share; below it the champion
// kite-farms — hits what is in range while orbiting its squad's camp.
constexpr float engage_health = 0.6f;
// Yield-park: hold a cleared camp through a respawn of at most this many
// rounds, or while the wait still beats the next-best camp by park_margin.
constexpr int park_wait = 20;
constexpr int park_margin = 10;
// Kite-farm: orbit tiles sit within this of the owned camp, this far from foes.
constexpr int kite_camp_radius = 4;
constexpr int kite_foe_radius = 4;
// Followers park on fixed sectors this far off the anchor — a squad-level
// pack ~8-12 tiles mean pairwise, not a blob.

// Game phases (the user's plan). Early: farm and feed Stellar, only push away
// enemies that come into reach. Mid (Stellar or Varg owns an emblem): hit
// enemies within mid_leash, then go back to farming. Late (Stellar owns three
// emblems): fight as before. Early and mid stay out of the enemy's half and
// back off when outnumbered by 2.
enum class Phase { early, mid, late };
constexpr std::uint32_t mid_game_by = 600;  // round: mid game even without an emblem
constexpr int mid_leash = 5;                // mid game: engage enemies this close
constexpr int deep_margin = 4;              // enemy half: this much closer to their spawn
constexpr int outnumbered_by = 2;           // enemies - allies within 6 tiles -> back off
constexpr int stellar_join_range = 8;       // Stellar walks this far to take a capture's gold


namespace {


// ---------------------------------------------------------------- basics

int dist(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

// Tiles within Chebyshev radius r of c.
bitboard square(vec2 c, int r) {
    bitboard tiles;
    tiles.clear();
    if (r >= 0)
        tiles.draw_chebyshev(c, static_cast<float>(r));
    return tiles;
}

// Tiles whose Chebyshev distance from c is between lo and hi.
bitboard ring(vec2 c, int lo, int hi) {
    return square(c, hi) & ~square(c, lo - 1);
}

// Run an SDK action; a rejected action is logged and reported as "did nothing"
// so the rest of the turn still runs.
template<class F>
bool attempt(F&& action) {
    try {
        return action();
    } catch (const std::exception& error) {
        std::cout << "bot error: " << error.what() << '\n';
        return false;
    }
}

bool alive(const cambc::unit& u) {
    try {
        return u.is_visible() && u.health() > 0;
    } catch (const std::exception&) {
        return false;
    }
}

float health_pct(const cambc::unit& u) {
    try {
        return u.max_health() > 0 ? u.health() / u.max_health() : 1.0f;
    } catch (const std::exception&) {
        return 1.0f;
    }
}

// A unit killed (or gone out of sight) earlier this turn throws on every read;
// an uncaught throw costs the champion the rest of its turn. These helpers
// read safely.
vec2 safe_pos(const cambc::unit& u, vec2 fallback) {
    try {
        return u.position();
    } catch (const std::exception&) {
        return fallback;
    }
}

// Building champion lists is expensive (~10k+ fuel each), so the lists are
// memoised per (round, position): vision only changes between rounds or when
// this champion moves. Live stats (health, position) are still read from the
// handles on use.
template<class Build>
const std::vector<cambc::champion>& memo_champions(Build build, std::uint32_t& round, vec2& pos,
                                                   std::vector<cambc::champion>& cache) {
    const auto here = cambc::self().position();
    const auto now = cambc::round_number();
    if (now != round || here != pos) {
        cache = build();
        round = now;
        pos = here;
    }
    return cache;
}

std::vector<cambc::champion> visible_enemies() {
    static std::uint32_t round = 0xFFFFFFFF;
    static vec2 pos{-1, -1};
    static std::vector<cambc::champion> cache;
    auto list = memo_champions([] {
        std::vector<cambc::champion> out;
        for (const auto& e : cambc::vision::champions(team::enemy))
            if (alive(e))
                out.push_back(e);
        return out;
    }, round, pos, cache);
    std::erase_if(list, [](const auto& e) { return !alive(e); }); // killed this turn
    return list;
}

// Visible living allies, not including this champion.
std::vector<cambc::champion> visible_allies() {
    static std::uint32_t round = 0xFFFFFFFF;
    static vec2 pos{-1, -1};
    static std::vector<cambc::champion> cache;
    return memo_champions([] {
        std::vector<cambc::champion> out;
        const auto& me = cambc::self();
        for (const auto& a : cambc::vision::champions(team::ally))
            if (!(a == me) && alive(a))
                out.push_back(a);
        return out;
    }, round, pos, cache);
}

// How much damage an enemy can deal: basic attacks plus spell power.
// Focus fire: damage it deals (plus the gems a kill would take) per point of
// health left, so the whole team finishes the cheapest dangerous kill first.
float threat(const cambc::champion& e) {
    float gems = 0;
    try {
        gems = static_cast<float>(e.gem_count());
    } catch (const std::exception&) {
    }
    try {
        return (e.strength() + 0.5f * e.focus() + 2.0f * gems) / std::max(50.0f, e.health());
    } catch (const std::exception&) {
        return -1.0f;
    }
}

// Greatest damage threat, optionally only among enemies within max_range of us.
std::optional<cambc::champion> top_threat(const std::vector<cambc::champion>& enemies,
                                          int max_range = 1000) {
    const auto me = cambc::self().position();
    std::optional<cambc::champion> best;
    float best_threat = -1;
    for (const auto& e : enemies) {
        if (!alive(e) || dist(me, safe_pos(e, vec2{-1000, -1000})) > max_range)
            continue;
        const float t = threat(e);
        if (!best || t > best_threat)
            best = e, best_threat = t;
    }
    return best;
}

int nearest_enemy_distance(const std::vector<cambc::champion>& enemies, vec2 from) {
    int best = 1000;
    for (const auto& e : enemies)
        if (alive(e))
            best = std::min(best, dist(from, safe_pos(e, vec2{-1000, -1000})));
    return best;
}

// ---------------------------------------------------------------- abilities

using Abilities = std::array<cambc::ability, 2>;

template<class Pred>
const cambc::ability* find_ability(const Abilities& abilities, Pred pred) {
    for (const auto& a : abilities)
        if (pred(a))
            return &a;
    return nullptr;
}

bool is_reset(const cambc::ability& a) { return a.is_attack_reset(); }
bool is_dash(const cambc::ability& a) { return a.is_dash(); }
bool is_heal(const cambc::ability& a) { return a.is_heal(); }
bool is_shield(const cambc::ability& a) { return a.is_shield(); }
bool is_area(const cambc::ability& a) { return a.can_target_ground() && !a.is_dash(); }
bool is_single(const cambc::ability& a) {
    return (a.is_damage() || a.is_cc()) && a.can_target_enemies() && !a.can_target_ground();
}

bool ready(const cambc::ability* a) {
    return a && cambc::self().can_cast() && attempt([&] { return a->is_ready(); });
}

bool cast_self(const cambc::ability* a) {
    return ready(a) && attempt([&] { return a->use(); });
}

template<class Target>
bool cast_on(const cambc::ability* a, const Target& target) {
    return ready(a) && attempt([&] {
        return target.is_visible() && target.health() > 0
            && a->valid_tiles().is_set(target.position()) && a->use(target);
    });
}

bool cast_at(const cambc::ability* a, vec2 tile) {
    return ready(a) && attempt([&] { return a->valid_tiles().is_set(tile) && a->use(tile); });
}

// Ground tile for a radius-1 area spell that hits the most enemies. When `must`
// is given, the area has to include that tile. Returns {tile, hits}.
std::pair<std::optional<vec2>, int> best_area_tile(const cambc::ability* a,
        const std::vector<cambc::champion>& enemies, std::optional<vec2> must = std::nullopt) {
    std::optional<vec2> best;
    int best_hits = 0;
    if (!a)
        return {best, 0};
    attempt([&] {
        const auto range = a->valid_tiles();
        const auto& walls = cambc::map::walls();
        for (const auto& e : enemies)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 t = e.position() + vec2{dx, dy};
                    if (!cambc::map::is_in_bounds(t) || !range.is_set(t) || walls.is_set(t))
                        continue;
                    if (must && dist(*must, t) > 1)
                        continue;
                    int hits = 0;
                    for (const auto& other : enemies)
                        hits += dist(other.position(), t) <= 1;
                    if (hits > best_hits) {
                        best_hits = hits;
                        best = t;
                    }
                }
        return true;
    });
    return {best, best_hits};
}

// ---------------------------------------------------------------- burst
// mingstudy-ttsburst: port of ming_bot tactic/burst.cpp part 1. Whenever a
// ready damage / area / stun spell can reach an enemy champion (not one healing
// on its own spawn), cast the one that hits the most enemies, then deals the
// most damage (engine damage_amount); ties go to channelling, low-health,
// big-bag targets; a stun on an already-stunned target ranks last. Runs every
// turn before banking and the role turn, at >= 60% health.
bool burst() {
    auto& me = cambc::self();
    try {
        if (!me.can_cast() || me.health() < me.max_health() * 0.6f)
            return false;
    } catch (const std::exception&) {
        return false;
    }
    const auto& enemies = visible_enemies();
    if (enemies.empty())
        return false;
    const auto& their_spawn = cambc::map::spawn_region(team::enemy);
    struct T {
        cambc::champion handle;
        vec2 pos;
        float hp;
        std::uint32_t gems;
        bool channel;
        bool stunned;
    };
    std::vector<T> ts;
    for (const auto& e : enemies) {
        try {
            if (!e.is_visible() || e.health() <= 0 || their_spawn.is_set(e.position()))
                continue;
            ts.push_back({e, e.position(), e.health(), e.gem_count(),
                          e.is_submitting_gems() || e.is_recalling(), e.buffs().has_stun()});
        } catch (const std::exception&) {
        }
    }
    if (ts.empty())
        return false;
    const auto abilities = me.abilities();
    int best_slot = -1;
    std::size_t best_idx = 0;
    vec2 best_tile{};
    bool best_ground = false, best_restun = false;
    int best_hits = 0;
    float best_dmg = -1;
    const T* best_t = nullptr;
    const auto better_target = [](const T& a, const T* b) {
        if (!b)
            return true;
        if (a.channel != b->channel)
            return a.channel;
        if (a.hp != b->hp)
            return a.hp < b->hp;
        return a.gems > b->gems;
    };
    const auto consider = [&](int slot, vec2 tile, bool ground, int hits, float dmg, const T& anchor,
                              bool restun, std::size_t idx) {
        bool better;
        if (best_slot < 0) better = true;
        else if (restun != best_restun) better = !restun;
        else if (hits != best_hits) better = hits > best_hits;
        else if (dmg != best_dmg) better = dmg > best_dmg;
        else better = better_target(anchor, best_t);
        if (better) {
            best_slot = slot; best_tile = tile; best_ground = ground; best_hits = hits;
            best_dmg = dmg; best_restun = restun; best_t = &anchor; best_idx = idx;
        }
    };
    for (int s = 0; s < 2; ++s) {
        const auto& a = abilities[s];
        bitboard reach;
        bool cc = false, aoe = false, enemy_target = false, ground = false;
        try {
            if (!a.is_ready() || !(a.is_damage() || a.is_aoe_damage() || a.is_cc()))
                continue;
            cc = a.is_cc();
            aoe = a.is_aoe_damage();
            enemy_target = a.can_target_enemies();
            ground = a.can_target_ground();
            reach = a.valid_tiles();
        } catch (const std::exception&) {
            continue;
        }
        const auto dmg_of = [&](const T& t) {
            try {
                return a.damage_amount(t.handle);
            } catch (const std::exception&) {
                return 0.0f;
            }
        };
        if (enemy_target)
            for (std::size_t i = 0; i < ts.size(); ++i)
                if (reach.is_set(ts[i].pos))
                    consider(s, ts[i].pos, false, 1, dmg_of(ts[i]), ts[i], cc && ts[i].stunned, i);
        if (ground && (aoe || cc)) {
            float radius = 1.0f;
            if (aoe)
                try {
                    radius = a.get<cambc::ability_aoe_damage>().radius;
                } catch (const std::exception&) {
                }
            for (const auto& t : ts)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        const vec2 c{t.pos.x + dx, t.pos.y + dy};
                        if (!cambc::map::is_in_bounds(c) || !reach.is_set(c))
                            continue;
                        int hits = 0;
                        float dmg = 0;
                        bool all_stunned = true;
                        const T* anchor = nullptr;
                        std::size_t anchor_idx = 0;
                        for (std::size_t i = 0; i < ts.size(); ++i) {
                            if (dist(c, ts[i].pos) > static_cast<int>(radius))
                                continue;
                            ++hits;
                            all_stunned = all_stunned && ts[i].stunned;
                            dmg += dmg_of(ts[i]);
                            if (better_target(ts[i], anchor)) {
                                anchor = &ts[i];
                                anchor_idx = i;
                            }
                        }
                        if (hits > 0)
                            consider(s, c, true, hits, dmg, *anchor, cc && all_stunned, anchor_idx);
                    }
        }
    }
    if (best_slot < 0)
        return false;
    const auto& a = abilities[best_slot];
    if (best_ground)
        return attempt([&] { return a.use(best_tile); });
    return attempt([&] { return a.use(ts[best_idx].handle); });
}

// ---------------------------------------------------------------- attacks

template<class Target>
bool basic_attack(const Target& target) {
    auto& me = cambc::self();
    return attempt([&] {
        if (!me.can_attack() || !target.is_visible() || target.health() <= 0
            || dist(me.position(), target.position()) > me.attack_range())
            return false;
        me.attack(target);
        return true;
    });
}

// fx-lockfix: the enemy lock on an altar. cambc::vision::altar_lock_at prefers OUR
// lock when both teams locked the same altar, and attacking our own lock fails
// ("Target must be a visible enemy or neutral camp") every turn forever.
std::optional<cambc::altar_lock> enemy_lock_at(vec2 altar) {
    try {
        for (const auto& l : cambc::vision::altar_locks(team::enemy))
            if (l.position() == altar)
                return l;
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

bool attack_lock(const cambc::altar_lock& lock) {
    auto& me = cambc::self();
    return attempt([&] {
        if (!me.can_attack() || dist(me.position(), lock.position()) > me.attack_range())
            return false;
        me.attack(lock);
        return true;
    });
}

// Attack, then use an attack reset (if one is ready) to attack again.
template<class Target>
bool attack_with_reset(const Target& target, const cambc::ability* reset) {
    if (!basic_attack(target))
        return false;
    if (cast_self(reset) && !basic_attack(target))
        for (const auto& e : visible_enemies())
            if (basic_attack(e))
                break;
    return true;
}

// Basic-attack the greatest threat in range, or any enemy in range.
bool attack_enemies(const std::vector<cambc::champion>& enemies, const cambc::ability* reset) {
    auto& me = cambc::self();
    if (auto t = top_threat(enemies, me.attack_range()))
        return attack_with_reset(*t, reset);
    return false;
}

// ---------------------------------------------------------------- movement

// Terrain plus every visible unit, except this champion's own tile.
// Memoised per (round, position): occupancy only changes between rounds or
// when this champion moves, and rebuilding it costs fuel on big maps.
bitboard blocked_tiles() {
    static const bitboard terrain = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy);
    static std::uint32_t memo_round = 0xFFFFFFFF;
    static vec2 memo_pos{-1, -1};
    static bitboard memo;
    const auto here = cambc::self().position();
    const auto round = cambc::round_number();
    if (round != memo_round || here != memo_pos) {
        memo = terrain | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        memo.set(here, false);
        memo_round = round;
        memo_pos = here;
    }
    return memo;
}

// One step along a shortest path to the nearest free goal tile. A step blocked
// by a hidden unit returns false without spending the move, so try the other
// equally short directions.
bool step_towards(bitboard goals, const bitboard* avoid = nullptr) {
    auto& me = cambc::self();
    if (!me.can_move())
        return false;
    const auto here = me.position();
    auto blocked = blocked_tiles();
    if (avoid) {
        blocked |= *avoid;
        blocked.set(here, false);
    }
    goals &= ~blocked;
    if (goals.is_empty() || goals.is_set(here))
        return false;
    return attempt([&] {
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(blocked, goals, square(here, 0), visited);
        for (const auto delta : field.next_moves_from(here))
            if (!blocked.is_set(here + delta) && me.move(delta))
                return true;
        return false;
    });
}

// Move to a tile between lo and hi tiles from `target`.
bool keep_distance(vec2 target, int lo, int hi) {
    const auto here = cambc::self().position();
    const int d = dist(here, target);
    if (d >= lo && d <= hi)
        return false;
    return step_towards(ring(target, lo, hi));
}

// ---------------------------------------------------------------- camps

// Static facts about each camp group, read from the engine once per bot.
// (Asking the engine for every group every turn used up the fuel budget.)
struct GroupInfo {
    bitboard tiles;
    std::vector<vec2> spawns;
    bool wolf = false;
    bool monolith = false;
    int gems = 0;
    cambc::map::camp_group group;
    int gold = 0;
    int health = 0; // total health of every camp in the group
    bool passive = true; // only gem/gold clusters: never fights back
    int strength = 0; // total damage per turn of the group's camps
};

const std::vector<GroupInfo>& groups() {
    static const std::vector<GroupInfo> cache = [] {
        std::vector<GroupInfo> out;
        for (const auto& g : cambc::map::camp_groups()) {
            GroupInfo info{g.spawn_tiles(), {}, false, false, static_cast<int>(g.gem_value()), g};
            info.spawns = info.tiles.positions();
            info.gold = static_cast<int>(g.gold_value());
            for (const auto& c : g.camps()) {
                info.wolf |= c.is_wolf();
                info.monolith |= c.is_monolith();
                info.passive &= c.is_gem_cluster() || c.is_gold_cluster();
                info.health += c.is_monolith() ? 20000 : c.is_elk() ? 2400 : c.is_wolf() ? 1350 : 500;
                info.strength += c.is_monolith() ? 200 : c.is_elk() ? 60 : c.is_wolf() ? 20 : 0;
            }
            out.push_back(std::move(info));
        }
        return out;
    }();
    return cache;
}

const GroupInfo* group_at(vec2 tile) {
    for (const auto& g : groups())
        if (g.tiles.is_set(tile))
            return &g;
    return nullptr;
}

bool quietly(const cambc::unit& u) {
    try {
        return u.is_visible() && u.health() > 0;
    } catch (const std::exception&) {
        return false;
    }
}

// Visible, living camps belonging to the group that owns `tile`.
std::vector<cambc::camp> live_camps_of(vec2 tile) {
    std::vector<cambc::camp> out;
    const auto* group = group_at(tile);
    if (!group)
        return out;
    for (const auto& c : cambc::vision::camps())
        if (quietly(c) && group->tiles.is_set(c.spawn_position()))
            out.push_back(c);
    return out;
}

// Respawn memory: the round each group is next expected alive (0 = believed
// alive now). Refreshed from whatever this champion sees; unseen groups keep
// their last known value, so a camp captured out of sight of us is still
// believed alive, while one we saw respawning is skipped until it is back.
std::vector<int>& ready_round() {
    static std::vector<int> ready(groups().size(), 0);
    return ready;
}

// Groups whose every spawn tile is already taken by someone capturing it
// (more allies don't speed a capture up): skipped until this round.
std::vector<int>& taken_until() {
    static std::vector<int> taken(groups().size(), 0);
    return taken;
}

bool off_limits(std::size_t i);
bool leave_to_stellar(const GroupInfo& g);

// Is any of the group's spawn tiles in vision? Checks its few tiles instead of
// intersecting two full-map bitboards (that cost ~34k fuel a turn on lanes).
bool group_in_view(const GroupInfo& g, const bitboard& visible) {
    for (const auto p : g.spawns)
        if (visible.is_set(p))
            return true;
    return false;
}

void update_ledger() {
    const auto visible = cambc::vision::visible_tiles();
    const int round = static_cast<int>(cambc::round_number());
    auto& ready = ready_round();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (!group_in_view(g, visible))
            continue;
        try {
            const auto state = g.group.state();
            // mp-fixspawn: a respawning group whose timer has run out is held back by
            // someone standing on its tiles (often us, waiting for it). Treat it as one
            // round away so farm_group waits one step OFF its tiles and lets it respawn.
            if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state))
                ready[i] = round + std::max<int>(static_cast<int>(r->timer), 1);
            else
                ready[i] = round;
        } catch (const std::exception&) {
        }
    }
}

// Pick the camp group worth the most gems (plus a little gold) per turn spent
// walking there, clearing it and capturing it. The monolith hits too hard to
// be worth it unless nothing else is left, the group in `exclude` (the other
// squad's camp) is skipped, and the current choice is kept unless another is
// clearly better.
std::optional<vec2> choose_camp(vec2 from, std::optional<vec2> current,
                                std::optional<vec2> exclude = std::nullopt, bool wolves = false,
                                int helpers = -1) {
    const int round = static_cast<int>(cambc::round_number());
    // Health of the champion who will take the camp's hits (only checked when
    // the leader has nobody near to help).
    float my_health = 0;
    if (helpers == 0)
        try {
            my_health = cambc::self().health();
        } catch (const std::exception&) {
            helpers = -1;
        }
    std::optional<vec2> best;
    double best_value = -1;
    double best_turns = 0;
    // Fallback when every group is respawning: the one back soonest after we
    // could walk there, so the squad waits beside it instead of wandering.
    std::optional<vec2> soonest;
    int soonest_wait = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (exclude && g.tiles.is_set(*exclude))
            continue;
        if (off_limits(i))
            continue;
        if (taken_until()[i] > round)
            continue;
        // Alone, don't start a fighting camp that would beat us (camps hit
        // the nearest champion; one champion deals about 55 a turn).
        if (helpers == 0 && !g.passive && !(current && g.tiles.is_set(*current))
            && g.strength * (g.health / 55.0f) * 0.6f > 0.7f * my_health)
            continue;
        int d = 1000;
        vec2 closest{};
        for (const auto p : g.spawns)
            if (dist(from, p) < d) {
                d = dist(from, p);
                closest = p;
            }
        // Still respawning when we would arrive: not a target yet.
        const int wait = ready_round()[i] - round - d;
        if (wait > 0) {
            if (!g.monolith && wait < soonest_wait) {
                soonest_wait = wait;
                soonest = closest;
            }
            continue;
        }
        const double turns = d + g.health / 250.0 + cambc::constants::capture_time;
        // Gems win the game; a gold-only camp is filler, picked only when no
        // gem camp is available (value 0 still beats "nothing").
        double value = g.gems / turns;
        if (g.monolith)
            value *= 0.2;
        if (wolves && g.wolf)
            value *= 1.3;
        if (current && g.tiles.is_set(*current))
            value *= stick_bonus;
        if (value > best_value) {
            best_value = value;
            best = closest;
            best_turns = turns;
        }
    }
    // Yield-park: a squad already holding a cleared camp waits out a short
    // respawn on its ring instead of relocating — park when the wait fits
    // park_wait, or when it still beats the next-best camp by park_margin.
    if (current)
        if (const auto* g = group_at(*current)) {
            const auto i = static_cast<std::size_t>(g - groups().data());
            const int left = ready_round()[i] - round;
            if (left > 0 && taken_until()[i] <= round) {
                int alt = std::numeric_limits<int>::max();
                if (best)
                    alt = static_cast<int>(best_turns);
                if (soonest)
                    alt = std::min(alt, soonest_wait + dist(from, *soonest));
                if (left <= park_wait || left <= alt - park_margin)
                    return current;
            }
        }
    return best ? best : soonest;
}

// Should we spend attacks on this camp? Only the farmed group, or camps that
// are already awake (damaged), so we don't wake extra groups by accident.
bool worth_hitting(const cambc::camp& c, const GroupInfo* farmed) {
    if (farmed && farmed->tiles.is_set(c.spawn_position()))
        return true;
    try {
        return c.health() < c.max_health();
    } catch (const std::exception&) {
        return false;
    }
}

// Spells (unless we already walked: most spells can't follow a walk), then
// basic attacks, on worthwhile camps in range.
bool hit_camps(std::optional<vec2> objective, const Abilities& abilities, bool spells = true) {
    const auto* farmed = objective ? group_at(*objective) : nullptr;
    const auto here = cambc::self().position();
    std::vector<cambc::camp> targets;
    for (const auto& c : cambc::vision::camps())
        if (dist(here, c.position()) <= 4 && quietly(c) && worth_hitting(c, farmed))
            targets.push_back(c);
    if (targets.empty())
        return false;
    std::sort(targets.begin(), targets.end(),
              [](const auto& a, const auto& b) { return a.health() < b.health(); });

    if (spells) {
        const auto* single = find_ability(abilities, [](const auto& a) {
            return (a.is_damage() || a.is_cc()) && a.can_target_camps() && !a.can_target_ground();
        });
        const auto* area = find_ability(abilities, is_area);
        for (const auto& c : targets)
            if (cast_on(single, c) || cast_at(area, c.position()))
                break;
    }

    const auto* reset = find_ability(abilities, is_reset);
    for (const auto& c : targets)
        if (attack_with_reset(c, reset))
            return true;
    return false;
}

// Farm one camp group: hit it when in range, otherwise walk into attack range
// of its living camps; once it is cleared, stand on its tiles to capture.
void farm_group(vec2 target, const Abilities& abilities) {
    if (hit_camps(target, abilities))
        return;
    const auto range = cambc::self().attack_range();
    bitboard goals;
    goals.clear();
    for (const auto& c : live_camps_of(target))
        goals |= square(c.position(), range);
    if (goals.is_empty())
        if (const auto* g = group_at(target)) {
            const auto i = static_cast<std::size_t>(g - groups().data());
            if (ready_round()[i] > static_cast<int>(cambc::round_number())) {
                // Respawning: wait one step off its tiles.
                for (const auto t : g->spawns)
                    goals |= square(t, 1);
                goals &= ~g->tiles;
            } else if (leave_to_stellar(*g)) {
                // Stellar takes this gold: wait next to it, off its tiles.
                for (const auto t : g->spawns)
                    goals |= square(t, 1);
                goals &= ~g->tiles;
            } else {
                goals = g->tiles;
                // Every spawn tile is taken by someone capturing it: move on.
                if (!g->tiles.is_set(cambc::self().position()) && (g->tiles & ~blocked_tiles()).is_empty()) {
                    bool cleared = false;
                    try {
                        cleared = group_in_view(*g, cambc::vision::visible_tiles())
                            && std::holds_alternative<cambc::map::camp_group_reward_available>(g->group.state());
                    } catch (const std::exception&) {
                    }
                    if (cleared) {
                        taken_until()[i] = static_cast<int>(cambc::round_number()) + 8;
                        goals.clear();
                    }
                }
            }
        }
    const bool moved = step_towards(goals);
    hit_camps(target, abilities, !moved);
}

// Gem clusters never fight back (500 health, 24 gems), so a back-line champion
// farms a ready one nearby on its own instead of idling behind its leader.
bool farm_nearby_cluster(const Abilities& abilities, int radius) {
    const auto here = cambc::self().position();
    const int round = static_cast<int>(cambc::round_number());
    std::optional<vec2> best;
    int best_d = radius + 1;
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (!g.passive || g.gems == 0 || ready_round()[i] > round || taken_until()[i] > round || off_limits(i))
            continue;
        for (const auto t : g.spawns)
            if (dist(here, t) < best_d) {
                best_d = dist(here, t);
                best = t;
            }
    }
    if (!best)
        return false;
    farm_group(*best, abilities);
    return true;
}

// Rewards are split between allies standing on a cleared group's tiles when the
// capture finishes, so step onto a nearby cleared group to earn gold as well.
bool join_capture() {
    const auto here = cambc::self().position();
    const auto visible = cambc::vision::visible_tiles();
    const int range = cambc::self().champion_name() == cambc::champion_type::stellar ? stellar_join_range
                                                                                      : join_capture_range;
    for (const auto& g : groups()) {
        bool near = false;
        for (const auto p : g.spawns)
            near |= dist(here, p) <= range;
        if (!near || !group_in_view(g, visible))
            continue;
        bool cleared = false;
        try {
            cleared = std::holds_alternative<cambc::map::camp_group_reward_available>(g.group.state());
        } catch (const std::exception&) {
        }
        if (!cleared)
            continue;
        if (leave_to_stellar(g))
            continue; // Stellar takes this gold
        if (g.tiles.is_set(here))
            return true; // already capturing: stay
        if (g.gold == 0 || (g.tiles & ~blocked_tiles()).is_empty())
            continue; // nothing to share, or no free tile to stand on
        if (step_towards(g.tiles))
            return true;
    }
    return false;
}

// With nothing to farm the squads hold the middle of the map: enemies on their
// own spawn heal to full every turn, so walking in there only feeds them kills.
vec2 map_centre() {
    return {cambc::map::width() / 2, cambc::map::height() / 2};
}

// Followers hold a loose ring (3-5 tiles) off the anchor — squad-level pack,
// not a blob stacked on one tile.
void hold_sector(vec2 anchor) {
    const auto here = cambc::self().position();
    const auto band = ring(anchor, 3, 5);
    if (band.is_set(here))
        return;
    step_towards(band);
}

// ---------------------------------------------------------------- shopping

std::vector<cambc::item_type> build_for(Role role) {
    using I = cambc::item_type;
    switch (role) {
    case Role::tank: return {I::jungle_shard_of_health, I::power_shard_of_health};
    case Role::fighter: return {I::monster_shard_of_strength, I::power_shard_of_strength};
    case Role::marksman: return {I::power_shard_of_focus};
    case Role::support: return {I::power_shard_of_focus, I::durability_shard_of_health, I::power_shard_of_focus};
    case Role::mage: return {I::power_shard_of_focus, I::durability_shard_of_health, I::power_shard_of_focus};
    case Role::farmer: return {I::monster_shard_of_strength, I::power_shard_of_strength};
    }
    return {I::power_shard_of_strength};
}

// Altar lock denial: a lock blocks the enemy team's tributes at that altar
// (ours still work) until they spend three basic attacks on it.
std::int64_t distance_to_region(vec2 p, const bitboard& region) {
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto t : region.positions())
        best = std::min(best, p.chebyshev_distance(t));
    return best;
}

void place_lock() {
    auto& me = cambc::self();
    const auto lock = cambc::consumable_type::altar_lock;
    if (attempt([&] { return me.consumables()[lock].count() == 0; }))
        return;
    const auto enemies = visible_enemies();
    for (const auto altar : cambc::map::altars().positions()) {
        if (dist(me.position(), altar) > 2 || cambc::vision::has_altar_lock(altar))
            continue;
        const bool enemy_side = distance_to_region(altar, cambc::map::spawn_region(team::enemy))
            < distance_to_region(altar, cambc::map::spawn_region(team::ally));
        if (!enemy_side && nearest_enemy_distance(enemies, altar) > 6)
            continue;
        if (attempt([&] { return me.consumables()[lock].use(altar); }))
            return;
    }
}

// Peel (Faisal-bot's top tactic): an enemy tributing or recalling in sight is
// worth interrupting before anything else. Only a stun breaks a tribute, so
// those gems never score; any damage breaks a recall. The biggest bag with
// the most channel time left goes first. Spends at most one cast and/or the
// basic attack, so the role turn that follows still has its move.
void peel() {
    auto& me = cambc::self();
    struct Channeler {
        cambc::champion enemy;
        bool tribute;
        float worth;
    };
    std::vector<Channeler> targets;
    for (const auto& e : visible_enemies()) {
        try {
            const bool tribute = e.is_submitting_gems();
            if (!tribute && !e.is_recalling())
                continue;
            const float left = static_cast<float>(e.remaining_channel_time()) + 1.0f;
            targets.push_back({e, tribute, (static_cast<float>(e.gem_count()) + 1.0f) * left});
        } catch (const std::exception&) {
        }
    }
    if (targets.empty())
        return;
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.worth > b.worth; });

    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, [](const auto& a) {
        return a.is_cc() && a.can_target_enemies() && !a.can_target_ground();
    });
    const auto* area_stun = find_ability(abilities, [](const auto& a) {
        return a.is_cc() && a.can_target_ground() && !a.is_dash();
    });
    for (const auto& t : targets) {
        if (cast_on(stun, t.enemy) || cast_at(area_stun, t.enemy.position()))
            return;
        if (!t.tribute && basic_attack(t.enemy))
            return;
    }
}

// Shared per-turn view, so each role reads the same facts.
struct Context {
    std::vector<cambc::champion> enemies;
    std::vector<cambc::champion> allies;
    std::optional<team_memory::Objective> objective;
    std::optional<vec2> anchor; // Ryth's position, if fresh
    bool hunt = false; // this champion's squad is hunting (no camp to farm)
    std::vector<cambc::champion> threats; // every enemy in sight off its spawn; enemies = the ones to engage
};

// Fight odds: a side's strength near here is the sum of health x damage of
// its visible champions within 6 tiles. Clearly outmatched -> don't take the
// fight, fall back towards our own side instead.
float side_strength(const std::vector<cambc::champion>& side, vec2 around) {
    float total = 0;
    for (const auto& c : side) {
        try {
            if (dist(c.position(), around) <= 6)
                total += c.health() * (c.strength() + 0.5f * c.focus());
        } catch (const std::exception&) {
        }
    }
    return total;
}

bool outmatched(const std::vector<cambc::champion>& enemies, const std::vector<cambc::champion>& allies) {
    const auto& me = cambc::self();
    const auto here = me.position();
    const float ours = side_strength(allies, here) + me.health() * (me.strength() + 0.5f * me.focus());
    return side_strength(enemies, here) > outmatched_ratio * ours;
}

// Back off one step towards our spawn, away from the enemies.
void fall_back() {
    step_towards(cambc::map::spawn_region(team::ally));
}

Phase game_phase() {
    if (team_memory::late_game())
        return Phase::late;
    if (team_memory::mid_game() || cambc::round_number() >= mid_game_by)
        return Phase::mid;
    return Phase::early;
}

// Deep in the enemy's half: clearly closer to their spawn than to ours.
bool deep(vec2 t) {
    return distance_to_region(t, cambc::map::spawn_region(team::enemy)) + deep_margin
        < distance_to_region(t, cambc::map::spawn_region(team::ally));
}

// Per group (cached): is its spawn tile nearest our base deep in their half?
bool deep_group(std::size_t i) {
    static std::vector<signed char> cache(groups().size(), -1);
    if (cache[i] < 0) {
        std::int64_t best = std::numeric_limits<std::int64_t>::max();
        vec2 closest{};
        const auto& ours = cambc::map::spawn_region(team::ally);
        for (const auto t : groups()[i].spawns)
            if (const auto d = distance_to_region(t, ours); d < best) {
                best = d;
                closest = t;
            }
        cache[i] = deep(closest) ? 1 : 0;
    }
    return cache[i] == 1;
}

// Camps we stay away from before the late game.
bool off_limits(std::size_t i) {
    return game_phase() != Phase::late && deep_group(i);
}

std::optional<vec2> stellar_position() {
    for (const auto& a : visible_allies())
        if (alive(a) && a.champion_name() == cambc::champion_type::stellar)
            return safe_pos(a, vec2{-100, -100});
    return std::nullopt;
}

// Gold goes to Stellar: when she is near a cleared group that pays gold,
// Varg leaves it to her and at most one other teammate shares it with her.
bool leave_to_stellar(const GroupInfo& g) {
    auto& me = cambc::self();
    const auto mine = me.champion_name();
    if (g.gold == 0 || mine == cambc::champion_type::stellar)
        return false;
    const auto s = stellar_position();
    if (!s)
        return false;
    bool near = false;
    for (const auto t : g.spawns)
        near |= dist(*s, t) <= 6;
    if (!near)
        return false;
    if (mine == cambc::champion_type::varg)
        return true;
    if (g.tiles.is_set(me.position()))
        return false; // already sharing it
    int others = 0;
    for (const auto& a : visible_allies())
        others += alive(a) && a.champion_name() != cambc::champion_type::stellar
            && g.tiles.is_set(safe_pos(a, vec2{-100, -100}));
    return others >= 1;
}

// Squad A follows Ryth's position; squad B follows the objective its leader shares.
Context read_context(bool squad_a) {
    Context ctx;
    ctx.enemies = visible_enemies();
    ctx.allies = visible_allies();
    ctx.objective = team_memory::objective();
    const auto anchor = team_memory::anchor();
    if (anchor)
        ctx.anchor = anchor->tile;
    ctx.hunt = squad_a ? (anchor && anchor->hunt) : (ctx.objective && ctx.objective->hunt);
    // Enemies on their own spawn heal to full every turn: never engage them.
    const auto& their_spawn = cambc::map::spawn_region(team::enemy);
    std::erase_if(ctx.enemies, [&](const auto& e) { return their_spawn.is_set(safe_pos(e, vec2{-1, -1})); });
    ctx.threats = ctx.enemies;
    // Who to engage depends on the phase: early only enemies already in
    // reach (push them away, no chasing); mid those within the leash and not
    // deep in their half; late everyone in sight.
    const auto phase = game_phase();
    if (phase != Phase::late) {
        auto& me = cambc::self();
        const auto here = me.position();
        const int reach = phase == Phase::early ? std::max<int>(static_cast<int>(me.attack_range()), 2) : mid_leash;
        std::erase_if(ctx.enemies, [&](const auto& e) {
            const auto at = safe_pos(e, vec2{-1000, -1000});
            return dist(here, at) > reach || (phase == Phase::mid && deep(at));
        });
    }
    return ctx;
}

// Back off? Clearly outmatched in strength (any phase), or, before the late
// game, outnumbered by 2+ within 6 tiles.
bool should_retreat(const Context& ctx) {
    if (ctx.threats.empty())
        return false;
    const auto here = cambc::self().position();
    if (outmatched(ctx.threats, ctx.allies) && nearest_enemy_distance(ctx.threats, here) <= 4)
        return true;
    if (game_phase() == Phase::late)
        return false;
    int enemies = 0, allies = 1;
    for (const auto& e : ctx.threats)
        enemies += alive(e) && dist(here, safe_pos(e, vec2{-1000, -1000})) <= 6;
    for (const auto& a : ctx.allies)
        allies += alive(a) && dist(here, safe_pos(a, vec2{-1000, -1000})) <= 6;
    return enemies - allies >= outnumbered_by;
}

// Fall back to the nearest teammate who is not under attack, else to base.
void retreat(const Context& ctx) {
    const auto here = cambc::self().position();
    std::optional<vec2> best;
    int best_d = 1000;
    for (const auto& a : ctx.allies) {
        if (!alive(a))
            continue;
        const auto at = safe_pos(a, vec2{-1000, -1000});
        if (nearest_enemy_distance(ctx.threats, at) <= 4 || dist(here, at) >= best_d)
            continue;
        best_d = dist(here, at);
        best = at;
    }
    if (best && best_d > 2)
        step_towards(ring(*best, 1, 2));
    else
        fall_back();
}

} // namespace

const char* champion_label(cambc::champion_type t); // scouting section below

// ==================================================================== Bot

// Squad A's anchor tile. While Ryth walks a bag to an altar (or is walking
// back) the anchor stays on the squad's camp — one tribute trip never pulls
// the whole squad off its farm.
vec2 Bot::squad_anchor() {
    auto& me = cambc::self();
    if (group_at(me.position()) != nullptr)
        banking_trip = false;
    else if (me.gem_count() > 0)
        banking_trip = true;
    if (banking_trip && farm_target)
        return *farm_target;
    return me.position();
}

// The camp this champion's squad is working: its own farm target, else the
// group containing the squad leader's shared tile.
std::optional<vec2> Bot::owned_camp() {
    if (farm_target)
        return farm_target;
    if (squad_a) {
        if (const auto a = team_memory::anchor(); a && !a->hunt && group_at(a->tile))
            return a->tile;
    } else if (const auto o = team_memory::objective(); o && !o->hunt && group_at(o->tile)) {
        return o->tile;
    }
    return std::nullopt;
}

// Low health with a foe near: never channel — hit what's in range and keep
// moving on the owned camp's far side, out of the foe's reach. Enemies on
// their own spawn heal every turn, so they can't be fought and aren't chased.
// Falls back to walking home when there is no camp to orbit.
void Bot::kite_farm(const std::vector<cambc::champion>& foes, const Abilities& abilities) {
    auto& me = cambc::self();
    const auto here = me.position();
    const auto& their_spawn = cambc::map::spawn_region(team::enemy);
    std::vector<cambc::champion> fight;
    for (const auto& e : foes)
        if (alive(e) && !their_spawn.is_set(safe_pos(e, vec2{-1, -1})))
            fight.push_back(e);
    const auto* reset = find_ability(abilities, is_reset);
    if (!attack_enemies(fight, reset))
        hit_camps(farm_target, abilities);
    // Orbiting beside a pursuer that can already reach us only drags the
    // chase out: break for spawn and heal instead.
    if (nearest_enemy_distance(foes, here) <= kite_foe_radius) {
        step_towards(cambc::map::spawn_region(team::ally));
        return;
    }
    const auto camp = owned_camp();
    if (camp)
        if (const auto* g = group_at(*camp)) {
            bitboard orbit;
            orbit.clear();
            for (const auto t : g->spawns)
                orbit |= ring(t, 1, kite_camp_radius);
            orbit &= ~g->tiles;
            for (const auto& e : foes)
                orbit &= ~square(safe_pos(e, here), kite_foe_radius);
            if (orbit.is_set(here))
                return; // already orbiting safely — hold
            if (!orbit.is_empty()) {
                step_towards(orbit);
                return;
            }
        }
    step_towards(cambc::map::spawn_region(team::ally));
}

void Bot::init() {
    initialised = true;
    auto& me = cambc::self();
    // Squad A: Ryth (leads), Tavi, Stellar. Squad B: Issac (leads), Cassi, Prism.
    using T = cambc::champion_type;
    switch (me.champion_name()) {
    case T::ryth: role = Role::tank; squad_a = true; break;
    case T::tavi: role = Role::marksman; squad_a = true; break;
    case T::stellar: role = Role::mage; squad_a = true; break;
    case T::willow: role = Role::support; squad_a = true; break;
    case T::issac: role = Role::fighter; squad_a = false; break;
    case T::vendetta: role = Role::fighter; squad_a = false; break;
    case T::cassi: role = Role::mage; squad_a = false; break;
    case T::prism: role = Role::mage; squad_a = false; break;
    case T::varg: role = Role::farmer; squad_a = true; break;
    default: role = Role::fighter; squad_a = false; break;
    }
    // Higher priority acts first: the initiator, then the front line, then the back line.
    const bool five = cambc::champions(team::ally).size() >= 5;
    int priority = 30;
    switch (role) {
    case Role::mage: priority = five && me.champion_name() == cambc::champion_type::prism ? 60 : 45; break;
    case Role::tank: priority = 55; break;
    case Role::fighter: priority = 50; break;
    case Role::marksman: priority = 40; break;
    case Role::support: priority = 30; break;
    case Role::farmer: priority = 35; break;
    }
    attempt([&] { me.set_priority(priority); return true; });
    last_health = me.health();
}

void Bot::shop() {
    auto& me = cambc::self();
    using T = cambc::champion_type;
    using I = cambc::item_type;
    const auto name = me.champion_name();
    // The phase advances with the carries' emblems (5 matching shards each).
    if ((name == T::stellar || name == T::varg) && shards_bought >= 5 && !team_memory::mid_game()) {
        team_memory::set_mid_game();
        std::cout << "SCOUT PHASE " << cambc::round_number() << " mid " << champion_label(name) << std::endl;
    }
    if (name == T::stellar && shards_bought >= 15 && !team_memory::late_game()) {
        team_memory::set_late_game();
        std::cout << "SCOUT PHASE " << cambc::round_number() << " late Stellar" << std::endl;
    }
    // Stellar is the team's carry: all Focus. Varg farms alone: Jungle
    // Strength (camps hit softer), then Power Strength. Issac leads squad B
    // into camps, so he builds like a tank.
    const auto build = name == T::stellar ? std::vector<cambc::item_type>{I::power_shard_of_focus}
        : name == T::varg ? std::vector<cambc::item_type>{I::jungle_shard_of_strength, I::power_shard_of_strength}
        : build_for(name == T::issac ? Role::tank : role);
    const auto lock = cambc::consumable_type::altar_lock;
    if (me.consumables()[lock].count() == 0 && me.can_buy(lock))
        attempt([&] { me.buy(lock); return true; });
    for (int purchases = 0; purchases < 10; ++purchases) {
        const auto step = std::min<std::size_t>(shards_bought / 5, build.size() - 1);
        const auto item = build[step];
        if (me.gold_count() < item.cost() || !me.can_buy(item))
            return;
        if (!attempt([&] { me.buy(item); return true; }))
            return;
        ++shards_bought;
    }
}

// Lost health since our last turn with an enemy champion close by counts as
// being attacked by a champion: this champion may fight back for the next
// few rounds. (The engine doesn't say who hit us, so a camp hit while an
// enemy champion stands within 4 tiles also counts.)
void Bot::track_damage() {
    auto& me = cambc::self();
    const float health = me.health();
    const bool hurt = health < last_health - 0.5f;
    last_health = health;
    if (!hurt)
        return;
    const auto enemies = visible_enemies();
    if (nearest_enemy_distance(enemies, me.position()) > 4)
        return;
    if (role == Role::tank)
        team_memory::raise_fight();
    if (health < me.max_health() * 0.4f)
        team_memory::raise_danger();
}

// Survival comes first: retreat or recall when low.
bool Bot::survive() {
    auto& me = cambc::self();
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    const float threshold = role == Role::tank ? retreat_tank : retreat_other;
    if (me.health() >= me.max_health() * threshold)
        return false;
    if (at_spawn)
        return true; // stay put; spawn heals at turn end

    // Any damage cancels a recall, so first step out of reach of awake camps
    // (attack range 3, and they never chase), then recall.
    bitboard camp_reach;
    camp_reach.clear();
    for (const auto& c : cambc::vision::camps())
        if (alive(c) && c.health() < c.max_health())
            camp_reach |= square(c.position(), 3);
    if (camp_reach.is_set(me.position())) {
        step_towards(square(me.position(), 6) & ~camp_reach);
        return true;
    }

    const auto enemies = visible_enemies();
    const bool threatened = nearest_enemy_distance(enemies, me.position()) <= 5;
    if (!threatened)
        return attempt([&] { me.recall(); return true; });

    // Vendetta falls back to Willow for healing when the way is clear.
    if (role == Role::fighter)
        for (const auto& ally : visible_allies())
            if (ally.champion_name() == cambc::champion_type::willow
                && nearest_enemy_distance(enemies, ally.position()) >= 4) {
                const auto abilities = me.abilities();
                const auto* dash = find_ability(abilities, is_dash);
                for (const auto t : (ring(ally.position(), 1, 1) & ~blocked_tiles()).positions())
                    if (cast_at(dash, t))
                        return true;
                step_towards(ring(ally.position(), 1, 1));
                return true;
            }

    // Kite-farm around the owned camp; running home through a chasing enemy
    // dies mid-field and a recall channel only breaks.
    kite_farm(enemies, me.abilities());
    return true;
}

// Carried gems are banked straight away (tested best: carrying gems loses them).
// Each altar is priced by real walking distance; an enemy-locked one costs a
// few extra turns to break the lock (3 basic attacks), so a locked altar on
// our side still beats an unlocked one behind the enemy spawn. (Skipping
// locked altars left champions frozen with full bags once Faisal-bot locked
// the reachable altar.)
bool Bot::bank() {
    auto& me = cambc::self();
    if (me.gem_count() == 0)
        return false;
    const auto enemy_locks = cambc::vision::altar_lock_tiles(team::enemy);
    const auto here = me.position();
    const int range = me.attack_range();

    // Carrying gems, route around tiles within 3 of a visible enemy when an
    // altar can still be reached that way; otherwise take the direct route.
    bitboard danger;
    danger.clear();
    for (const auto& e : visible_enemies())
        if (alive(e))
            danger |= square(safe_pos(e, here), 3);
    danger.set(here, false);
    bool safe_route = !danger.is_empty();
    auto blocked = blocked_tiles();
    if (safe_route)
        blocked |= danger;

    // Distances from here to every reachable tile.
    cambc::distance_field field;
    bitboard visited;
    attempt([&] { field.compute(blocked, square(here, 0), visited); return true; });
    if (safe_route) {
        // No altar reachable without passing enemies: take the direct route.
        bool reachable = false;
        for (const auto altar : cambc::map::altars().positions())
            reachable |= !(square(altar, std::max(range, 1)) & visited).is_empty();
        if (!reachable) {
            safe_route = false;
            blocked = blocked_tiles();
            attempt([&] { field.compute(blocked, square(here, 0), visited); return true; });
        }
    }
    const auto steps_to = [&](const bitboard& tiles) {
        int best = std::numeric_limits<int>::max();
        for (const auto t : (tiles & visited).positions())
            best = std::min<int>(best, field.get(t));
        return best;
    };

    std::optional<vec2> best_altar;
    bool best_locked = false;
    int best_cost = std::numeric_limits<int>::max();
    for (const auto altar : cambc::map::altars().positions()) {
        const bool locked = enemy_locks.is_set(altar);
        if (!locked && dist(here, altar) <= cambc::constants::submit_gems_range)
            return attempt([&] { me.submit_gems(altar); return true; });
        if (locked && dist(here, altar) <= range)
            if (const auto l = enemy_lock_at(altar); l && attack_lock(*l))
                return true;
        const auto stand = locked ? square(altar, range) & ~square(altar, 0)
                                  : square(altar, cambc::constants::submit_gems_range);
        int cost = steps_to(stand);
        if (cost == std::numeric_limits<int>::max())
            continue;
        if (locked)
            cost += 4;
        if (cost < best_cost) {
            best_cost = cost;
            best_altar = altar;
            best_locked = locked;
        }
    }

    // Fight back on the way if a champion hurt us and we are melee (ranged
    // would lose the move).
    if (range <= 1 && fighting_back())
        attack_enemies(visible_enemies(), nullptr);
    if (!best_altar)
        return true; // no altar reachable right now (blocked in): wait
    const auto stand = best_locked ? square(*best_altar, range) & ~square(*best_altar, 0)
                                   : square(*best_altar, cambc::constants::submit_gems_range);
    step_towards(stand, safe_route ? &danger : nullptr);
    if (best_locked && dist(me.position(), *best_altar) <= range)
        if (const auto l = enemy_lock_at(*best_altar))
            attack_lock(*l);
    return true;
}

// Gold only buys upgrades on spawn, so go home to spend a full purse when it
// is safe: no gems to lose, no enemy in sight, no fight going on. Tavi shops
// anywhere, so he never needs the trip.
bool Bot::shopping_trip() {
    auto& me = cambc::self();
    if (role == Role::marksman || me.gold_count() < shop_trip_gold || me.gem_count() > 0)
        return false;
    if (cambc::map::spawn_region(team::ally).is_set(me.position()))
        return false;
    if (team_memory::fight_alarm() || !visible_enemies().empty())
        return false;
    return attempt([&] { me.recall(); return true; });
}

// Each squad leader picks a camp. Squad B's leader shares hers as the
// objective; Ryth picks a different group and shares his position. With no
// camp left to farm, a squad holds the map centre. A follower whose leader is
// silent (dead or recalling) farms on its own.
void Bot::choose_objective() {
    const auto here = cambc::self().position();
    const auto vendetta_camp = team_memory::objective();
    // Allies close enough to share a fighting camp's hits.
    int helpers = 0;
    for (const auto& a : visible_allies())
        helpers += dist(safe_pos(a, vec2{-100, -100}), here) <= 5;
    switch (role) {
    case Role::fighter:
        farm_target = choose_camp(here, farm_target, std::nullopt, false, helpers);
        if (farm_target)
            team_memory::set_objective(*farm_target, false);
        else
            team_memory::set_objective(map_centre(), true);
        break;
    case Role::tank: {
        std::optional<vec2> exclude;
        if (vendetta_camp && !vendetta_camp->hunt)
            exclude = vendetta_camp->tile;
        farm_target = choose_camp(here, farm_target, exclude, false, helpers);
        hunting = !farm_target;
        break;
    }
    case Role::marksman:
    case Role::support:
    case Role::farmer:
        break; // farmer_turn picks its own camps
    case Role::mage: {
        const bool leader_heard = squad_a ? team_memory::anchor().has_value() : vendetta_camp.has_value();
        farm_target = leader_heard ? std::nullopt : choose_camp(here, farm_target);
        break;
    }
    }
}

// A lone wolf (1350 health, hits for 20) is safe for one healthy champion, so
// a back-liner farms one near its leader instead of trailing behind while no
// enemy has been seen for a while. Cassi stays with Issac (they die together).
bool Bot::farm_nearby_wolf(const Abilities& abilities, std::optional<vec2> leader) {
    auto& me = cambc::self();
    if (!leader || me.champion_name() == cambc::champion_type::cassi)
        return false;
    if (enemy_seen_round && cambc::round_number() <= enemy_seen_round + 20)
        return false;
    const auto here = me.position();
    const int round = static_cast<int>(cambc::round_number());
    std::optional<vec2> best;
    int best_d = 13;
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (g.passive || g.strength > 20 || g.gems == 0 || ready_round()[i] > round || taken_until()[i] > round
            || off_limits(i))
            continue;
        for (const auto t : g.spawns) {
            // Near the leader for help, but not the leader's own camp.
            const int dl = dist(*leader, t);
            if (dl > 10 || dl <= 3 || dist(here, t) >= best_d)
                continue;
            best_d = dist(here, t);
            best = t;
        }
    }
    if (!best)
        return false;
    // Start only when healthy; once the wolf is awake, finish it (survive()
    // still pulls out below the retreat threshold).
    bool awake = false;
    for (const auto& c : live_camps_of(*best))
        try {
            awake |= c.health() < c.max_health();
        } catch (const std::exception&) {
        }
    if (!awake && me.health() < me.max_health() * 0.75f)
        return false;
    farm_group(*best, abilities);
    return true;
}

// Solo farmer (Varg): camps one champion clears safely on its own (gem and
// gold clusters, lone wolves), valued by gems per turn of walking, clearing
// (~100 damage a turn with splash) and capturing. Groups next to a squad's
// target are left to the squad.
std::optional<vec2> choose_solo_camp(vec2 from, std::optional<vec2> current, std::optional<vec2> avoid_a,
                                     std::optional<vec2> avoid_b) {
    const int round = static_cast<int>(cambc::round_number());
    std::optional<vec2> best, soonest;
    double best_value = -1;
    int soonest_wait = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (g.gems == 0 || g.monolith || (!g.passive && g.strength > 20) || taken_until()[i] > round
            || off_limits(i))
            continue;
        const bool is_current = current && g.tiles.is_set(*current);
        int d = 1000;
        vec2 closest{};
        for (const auto p : g.spawns)
            if (dist(from, p) < d) {
                d = dist(from, p);
                closest = p;
            }
        if (!is_current && ((avoid_a && dist(*avoid_a, closest) <= 3) || (avoid_b && dist(*avoid_b, closest) <= 3)))
            continue;
        const int wait = ready_round()[i] - round - d;
        if (wait > 0) {
            if (wait < soonest_wait) {
                soonest_wait = wait;
                soonest = closest;
            }
            continue;
        }
        double value = g.gems / (d + g.health / 100.0 + cambc::constants::capture_time);
        if (is_current)
            value *= stick_bonus;
        if (value > best_value) {
            best_value = value;
            best = closest;
        }
    }
    return best ? best : soonest;
}

void Bot::farmer_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* reset = find_ability(abilities, is_reset);
    const auto* shield = find_ability(abilities, is_shield);
    const bool engage = !ctx.enemies.empty()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }
    if (engage) {
        const auto target = top_threat(ctx.enemies);
        if (!target)
            return;
        const auto at = safe_pos(*target, me.position());
        if (dist(me.position(), at) <= 2)
            cast_self(shield);
        if (attack_with_reset(*target, reset))
            return;
        step_towards(ring(at, 1, 1));
        attack_with_reset(*target, reset);
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    if (join_capture())
        return;
    std::optional<vec2> squad_b;
    if (ctx.objective && !ctx.objective->hunt)
        squad_b = ctx.objective->tile;
    farm_target = choose_solo_camp(me.position(), farm_target, ctx.anchor, squad_b);
    if (!farm_target) { // nothing to solo here: help Issac's squad clear its camp
        if (squad_b) {
            farm_group(*squad_b, abilities);
            return;
        }
        if (hit_camps(std::nullopt, abilities))
            return;
        if (ctx.anchor)
            step_towards(ring(*ctx.anchor, 1, 2));
        return;
    }
    // Shield up before a wolf's hits land (it soaks most of them).
    for (const auto& c : live_camps_of(*farm_target))
        if (!c.is_gem_cluster() && !c.is_gold_cluster() && dist(me.position(), c.position()) <= 3) {
            cast_self(shield);
            break;
        }
    farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- scouting
// SCOUT lines in stdout tell us what an opponent does in platform matches,
// where only our own log is available (tools/scout.py summarises them):
//   SCOUT DRAFT ...                    both rosters and the map (first turn)
//   SCOUT SEE r me@x,y gems | enemies  enemies in sight (name@x,y:gems/hp%, T tributing, R recalling)
//   SCOUT DEATH r me gems | enemies    one of ours died, its bag and who was near
//   SCOUT LOCK r x,y                   an enemy altar lock appeared
const char* champion_label(cambc::champion_type t) {
    static constexpr const char* names[] = {"Ryth", "Vesper", "Vendetta", "Cassi", "Issac", "Stellar",
                                            "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi"};
    const auto i = static_cast<int>(t);
    return i >= 0 && i < 12 ? names[i] : "?";
}

const char* draft_label(cambc::champion_draft d) {
    static constexpr const char* names[] = {"Ryth", "Vesper", "Vendetta", "Cassi&Issac", "Stellar", "Prism",
                                            "Marcella", "Willow", "Ren", "Varg", "Tavi"};
    const auto i = static_cast<int>(d);
    return i >= 0 && i < 11 ? names[i] : "?";
}

void scout(bool reporter) {
    try {
        auto& me = cambc::self();
        const auto round = cambc::round_number();
        static bool drafted = false;
        if (reporter && !drafted) {
            drafted = true;
            std::cout << "SCOUT DRAFT map " << cambc::map::width() << 'x' << cambc::map::height() << " ours";
            for (const auto d : cambc::draft::current_draft(team::ally))
                std::cout << ' ' << draft_label(d);
            std::cout << " | theirs";
            for (const auto d : cambc::draft::current_draft(team::enemy))
                std::cout << ' ' << draft_label(d);
            std::cout << std::endl;
        }
        static bool was_alive = true;
        static std::uint32_t last_gems = 0, last_seen = 0;
        static std::string last_near;
        static bitboard locks_known = [] { bitboard b; b.clear(); return b; }();
        const auto name = champion_label(me.champion_name());
        if (!me.is_alive()) {
            if (was_alive)
                std::cout << "SCOUT DEATH " << round << ' ' << name << ' ' << last_gems << " |" << last_near << std::endl;
            was_alive = false;
            return;
        }
        was_alive = true;
        last_gems = me.gem_count();
        std::string near;
        for (const auto& e : visible_enemies()) {
            if (!alive(e))
                continue;
            const auto at = safe_pos(e, vec2{-1, -1});
            near += ' ';
            near += champion_label(e.champion_name());
            near += '@' + std::to_string(at.x) + ',' + std::to_string(at.y) + ':' + std::to_string(e.gem_count()) + '/'
                + std::to_string(static_cast<int>(100 * health_pct(e)));
            if (e.is_submitting_gems())
                near += 'T';
            if (e.is_recalling())
                near += 'R';
        }
        last_near = near;
        if (!near.empty() && (last_seen == 0 || round >= last_seen + 15)) {
            last_seen = round;
            const auto here = me.position();
            std::cout << "SCOUT SEE " << round << ' ' << name << '@' << here.x << ',' << here.y << ' ' << last_gems
                      << " |" << near << std::endl;
        }
        const auto locks = cambc::vision::altar_lock_tiles(team::enemy);
        for (const auto t : (locks & ~locks_known).positions())
            std::cout << "SCOUT LOCK " << round << ' ' << t.x << ',' << t.y << std::endl;
        locks_known |= locks;
    } catch (const std::exception&) {
    }
}

// Fight any enemy champion in sight (unless clearly outmatched, see
// outmatched()). Tested overnight: fighting only after being hit lost to this
// head to head 7-13 and did worse against the Faisal-bot family.
bool Bot::fighting_back() const {
    return true;
}

void Bot::play_turn() {
    if (!initialised)
        init();
    auto& me = cambc::self();

    // Score trace for tools/league.py: the first ally reports every 5 rounds.
    static const bool reporter = [&] {
        const auto allies = cambc::champions(team::ally);
        return !allies.empty() && allies.front() == me;
    }();
    if (reporter && cambc::round_number() % 5 == 0)
        std::cout << "SCORE " << cambc::round_number() << ' '
                  << cambc::global_submitted_gems(team::ally) << ' '
                  << cambc::global_submitted_gems(team::enemy) << std::endl;
    scout(reporter);

    if (!me.is_alive()) { // dead champions can still shop
        shop();
        return;
    }
    track_damage();
    if (!visible_enemies().empty())
        enemy_seen_round = cambc::round_number();
    if (role == Role::tank)
        team_memory::set_anchor(squad_anchor(), hunting);
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;

    shop();
    if (survive())
        return;
    update_ledger();
    choose_objective();
    if (role == Role::tank)
        team_memory::set_anchor(squad_anchor(), hunting);
    burst(); // mingstudy-ttsburst: spend ready spells on enemies in reach first
    if (bank())
        return;
    if (shopping_trip())
        return;
    peel();
    place_lock();

    switch (role) {
    case Role::tank: tank_turn(); break;
    case Role::fighter: fighter_turn(); break;
    case Role::marksman: marksman_turn(); break;
    case Role::support: support_turn(); break;
    case Role::mage: mage_turn(); break;
    case Role::farmer: farmer_turn(); break;
    }
}

// ---------------------------------------------------------------- Ryth

void Bot::tank_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun_area = find_ability(abilities, is_area);
    const auto* reset = find_ability(abilities, is_reset);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());

    (void)nearest;
    // >=60% hp to chase a fight; below that still defend a foe already in reach.
    const bool engage = !ctx.enemies.empty() && fighting_back()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto [tile, hits] = best_area_tile(stun_area, ctx.enemies);
        if (tile && hits >= 1)
            cast_at(stun_area, *tile);
        if (!attack_enemies(ctx.enemies, reset)) {
            if (const auto target = top_threat(ctx.enemies))
                keep_distance(safe_pos(*target, me.position()), 1, 1);
            attack_enemies(ctx.enemies, reset);
        }
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    // Farming: keep collecting gems even with enemies in sight.
    if (hunting) {
        step_towards(square(map_centre(), 3));
        return;
    }
    // Front line: Ryth walks in first, so the camp targets him.
    if (farm_target)
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Vendetta

void Bot::fighter_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* reset = find_ability(abilities, is_reset);
    const auto* dash = find_ability(abilities, is_dash);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());

    // Vendetta keeps securing her camp during a fight unless a teammate is in
    // danger, an enemy is on top of her, or there is no camp to secure.
    (void)nearest;
    // >=60% hp to chase a fight; below that still defend a foe already in reach.
    const bool engage = !ctx.enemies.empty() && fighting_back()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto target = top_threat(ctx.enemies);
        if (!target)
            return;
        if (attack_with_reset(*target, reset))
            return;
        // Flank: approach the threat from a side away from its teammates.
        bitboard flank = ring(safe_pos(*target, me.position()), 1, 1) & ~blocked_tiles();
        for (const auto& e : ctx.enemies)
            if (!(e == *target) && alive(e)) {
                const auto away = flank & ~square(safe_pos(e, me.position()), 1);
                if (!away.is_empty())
                    flank = away;
            }
        const int d = dist(me.position(), safe_pos(*target, me.position()));
        if (d >= 2 && d <= 3)
            for (const auto t : flank.positions())
                if (cast_at(dash, t))
                    break;
        if (dist(me.position(), safe_pos(*target, me.position())) > 1)
            step_towards(flank.is_empty() ? ring(safe_pos(*target, me.position()), 1, 1) : flank);
        attack_with_reset(*target, reset);
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    // Farming: lead her squad to the chosen camp (choose_objective already ran).
    if (!farm_target) {
        if (ctx.objective)
            step_towards(ring(ctx.objective->tile, 2, 4));
        return;
    }
    farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Tavi

void Bot::marksman_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, is_single);
    const auto* reset = find_ability(abilities, is_reset);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    (void)nearest;
    // >=60% hp to chase a fight; below that still defend a foe already in reach.
    const bool engage = !ctx.enemies.empty() && fighting_back()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto target = top_threat(ctx.enemies);
        // Damage + stun, then a basic attack; the reset waits for a later turn
        // because only one ability can be cast per turn.
        if (target)
            cast_on(stun, *target);
        if (attack_enemies(ctx.enemies, reset))
            return;
        if (target)
            keep_distance(safe_pos(*target, me.position()), 3, 3);
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    // Farming: share cleared camps, shoot what Ryth has woken, stay just behind him.
    if (join_capture())
        return;
    if (farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, ctx.anchor))
        return;
    if (ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        hold_sector(*ctx.anchor);
        return;
    }
    if (farm_target) // Ryth is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Willow

void Bot::support_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* heal = find_ability(abilities, is_heal);
    const auto* reset = find_ability(abilities, is_reset);

    // Heal the ally below 30% in the greatest danger (most enemies near),
    // breaking ties by lowest health percentage.
    std::optional<cambc::champion> patient;
    int patient_danger = -1;
    for (const auto& ally : ctx.allies) {
        if (health_pct(ally) >= 0.3f)
            continue;
        int danger = 0;
        for (const auto& e : ctx.enemies)
            danger += alive(e) && dist(safe_pos(e, vec2{-1000, -1000}), ally.position()) <= 4;
        if (danger > patient_danger
            || (danger == patient_danger && health_pct(ally) < health_pct(*patient))) {
            patient = ally;
            patient_danger = danger;
        }
    }
    bool healed = false;
    if (patient) {
        healed = cast_on(heal, *patient);
        if (!healed && dist(me.position(), safe_pos(*patient, me.position())) > 4)
            step_towards(square(safe_pos(*patient, me.position()), 4) & ~square(safe_pos(*patient, me.position()), 1));
    }

    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    (void)nearest;
    // >=60% hp to chase a fight; below that still defend a foe already in reach.
    const bool engage = !ctx.enemies.empty() && fighting_back()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }
    const auto* attack_reset = healed ? nullptr : reset;
    if (engage) {
        if (attack_enemies(ctx.enemies, attack_reset))
            return;
        // Back line: near Ryth (or the team) but at least 4 tiles from enemies.
        const vec2 centre = ctx.anchor ? *ctx.anchor
            : (ctx.allies.empty() ? me.position() : ctx.allies.front().position());
        bitboard safe = square(centre, 4);
        for (const auto& e : ctx.enemies)
            safe &= ~square(e.position(), 3);
        if (!safe.is_empty())
            step_towards(safe);
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    // Farming: share cleared camps, help on woken camps, back line behind Ryth.
    if (join_capture())
        return;
    if (farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, ctx.anchor))
        return;
    if (ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        hold_sector(*ctx.anchor);
        return;
    }
    if (farm_target) // Ryth is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Prism

void Bot::mage_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, is_single);
    const auto* area = find_ability(abilities, is_area);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    // On 5-champion maps Prism opens the fight when hunting.
    (void)nearest;
    // >=60% hp to chase a fight; below that still defend a foe already in reach.
    const bool engage = !ctx.enemies.empty() && fighting_back()
        && (me.health() >= me.max_health() * engage_health
            || nearest_enemy_distance(ctx.enemies, me.position()) <= 2);
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        bool cast = false;
        // Finisher: area spell on a low enemy, but only if it kills or hits 2+.
        for (const auto& low : ctx.enemies) {
            if (cast || health_pct(low) >= 0.3f || !ready(area))
                continue;
            const auto [tile, hits] = best_area_tile(area, ctx.enemies, low.position());
            const bool kills = attempt([&] { return area->damage_amount(low) >= low.health(); });
            if (tile && (hits >= 2 || kills))
                cast = cast_at(area, *tile);
        }
        // Opener: damage + stun on the greatest threat in range.
        if (!cast)
            if (const auto target = top_threat(ctx.enemies, 4)) {
                cast = cast_on(stun, *target);
                if (cast)
                    team_memory::raise_fight();
            }
        // Follow-up: area spell once the stun is on cooldown.
        if (!cast) {
            const auto [tile, hits] = best_area_tile(area, ctx.enemies);
            if (tile && hits >= 1)
                cast_at(area, *tile);
        }
        if (attack_enemies(ctx.enemies, nullptr))
            return;
        if (const auto target = top_threat(ctx.enemies))
            keep_distance(safe_pos(*target, me.position()), 2, 4);
        return;
    }
    if (!ctx.enemies.empty() && me.health() < me.max_health() * 0.45f
        && nearest_enemy_distance(ctx.enemies, me.position()) <= 6) {
        kite_farm(ctx.enemies, abilities);
        return;
    }

    // Farming: share cleared camps, hit camps the squad leader has woken, and
    // stay a step behind the leader so the camp targets the leader first.
    if (join_capture())
        return;
    // Early on Stellar stays with Ryth on camps that pay gold (gem clusters pay none).
    const bool feeding = me.champion_name() == cambc::champion_type::stellar && game_phase() == Phase::early
        && squad_a && ctx.anchor;
    if (!feeding && farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, squad_a ? ctx.anchor
                                            : (ctx.objective ? std::optional<vec2>(ctx.objective->tile) : std::nullopt)))
        return;
    if (squad_a && ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        hold_sector(*ctx.anchor);
        return;
    }
    if (!squad_a && ctx.objective) {
        if (hit_camps(std::nullopt, abilities))
            return;
        if (ctx.hunt)
            step_towards(ring(ctx.objective->tile, 3, 5));
        else
            hold_sector(ctx.objective->tile);
        return;
    }
    if (farm_target) // the squad leader is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}
