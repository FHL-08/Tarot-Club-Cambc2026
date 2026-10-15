// burst — spend abilities on enemy champions (issue #53). Platform data:
// opponents cast ~24 ability hits on our champions per game against our
// 1.3 — basic attacks alone lose the champion-damage race 6.9k to 12.7k.
// Sits between lock and deny in the dispatch order: cheaper than a gank
// commitment, worth more than a sabotage walk.
#include "tactic.h"
#include "support.h"

namespace {

// A visible enemy champion re-resolved to a live handle: buffs and channel
// flags are reads the Obs snapshot doesn't carry.
struct Target {
    cambc::champion handle;
    cambc::vec2 pos;
    float health = 0;
    std::uint32_t gems = 0;
    bool channelling = false; // tribute or recall — pinned in place
    bool stunned = false;
};

[[nodiscard]] float range_of(const cambc::ability& ab)
{
    return std::visit(
        [](const auto& v) -> float {
            if constexpr (requires { v.range; }) return v.range;
            return 0.f;
        },
        *ab.info());
}

} // namespace

std::optional<exec::Action> tactic::burst(const Obs& obs)
{
    if (!obs.self_alive) return std::nullopt;
    const auto& me = cambc::self();
    if (!me.can_cast()) return std::nullopt;
    // Below the pitstop line the retreat owns the turn — poke carries the
    // same guard; casting instead of recalling is how bodies get chained.
    if (obs.self_health < obs.self_max_health * 0.6f) return std::nullopt;
    // abilities() returns its array by value — a reference into the
    // temporary would dangle (same hazard camp_cast documents).
    const auto abilities = me.abilities();

    std::vector<Target> targets;
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0) continue;
        try
        {
            auto handle = cambc::vision::champion_at(e.position);
            targets.push_back({ handle, e.position, e.health, e.gems,
                e.submitting || handle.is_recalling(),
                handle.buffs().has_stun() });
        }
        catch (const cambc::error&) { continue; } // left vision between collect and now
    }

    // 1. Damage / AoE / cc on an enemy champion. Pick the (slot, target)
    // pair by damage_amount; cc scoring zero still fires when nothing
    // hurts. Priority: interrupt a channel, then lowest health, then bag.
    int best_slot = -1;
    cambc::vec2 best_tile{};
    bool best_ground = false;
    float best_dmg = -1;
    const Target* best_t = nullptr;
    auto better_target = [](const Target& a, const Target* b, bool cc) {
        if (!b) return true;
        if (a.channelling != b->channelling) return a.channelling;
        // Stuns are wasted on an already-stunned enemy when another legal
        // target exists.
        if (cc && a.stunned != b->stunned) return !a.stunned;
        if (a.health != b->health) return a.health < b->health;
        return a.gems > b->gems;
    };
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.info() || !ab.is_ready() || !ab.can_target_enemies()) continue;
        const bool cc = ab.is_cc(), aoe = ab.is_aoe_damage();
        if (!ab.is_damage() && !cc && !aoe) continue;
        cambc::bitboard reach{ cambc::uninitialized };
        ab.mask_into(reach);
        for (const auto& t : targets)
        {
            if (!reach.is_set(t.pos)) continue;
            cambc::vec2 tile = t.pos;
            int hits = 1;
            if (aoe)
            {
                // Centre the blast on the enemy covering the most enemies.
                const float radius = ab.get<cambc::ability_aoe_damage>().radius;
                for (const auto& centre : targets)
                {
                    if (!reach.is_set(centre.pos)) continue;
                    int n = 0;
                    for (const auto& other : targets)
                        if (centre.pos.chebyshev_distance(other.pos)
                            <= static_cast<std::int64_t>(radius))
                            ++n;
                    if (n > hits) { hits = n; tile = centre.pos; }
                }
            }
            if (!better_target(t, best_t, cc)) continue;
            float dmg = 0;
            try { dmg = ab.damage_amount(t.handle); }
            catch (const cambc::error&) { continue; }
            const bool ground = aoe && !ab.can_target_enemies();
            // One action spends one slot: the biggest hit wins; a cc that
            // would only re-stun loses to any alternative via better_target.
            if (best_slot < 0 || dmg > best_dmg
                || (dmg == best_dmg && better_target(t, best_t, cc)))
            {
                best_slot = static_cast<int>(slot);
                best_tile = tile;
                best_ground = ground;
                best_dmg = dmg;
                best_t = &t;
            }
        }
    }
    if (best_slot >= 0)
        return exec::Cast{ static_cast<std::uint8_t>(best_slot), best_tile, best_ground };

    // 2. Shield / heal: spend on self, else the lowest-hp readable ally in
    // cast range — only when the body is hurt (<70%) and an enemy can reach
    // it next turn (enemy attack range + 1 step, same model as the threat
    // clause in exec).
    auto in_danger = [&](cambc::vec2 pos) {
        for (const auto& t : targets)
            try
            {
                if (pos.chebyshev_distance(t.pos)
                    <= t.handle.attack_range() + 1)
                    return true;
            }
            catch (const cambc::error&) { continue; }
        return false;
    };
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.info() || !ab.is_ready() || (!ab.is_heal() && !ab.is_shield()))
            continue;
        cambc::bitboard reach{ cambc::uninitialized };
        ab.mask_into(reach);
        if (ab.can_target_self() && obs.self_health < obs.self_max_health * 0.7f
            && in_danger(obs.self_pos))
            return exec::Cast{ static_cast<std::uint8_t>(slot), obs.self_pos, false };
        if (!ab.can_target_allies()) continue;
        cambc::vec2 ally_pos{};
        float ally_hp = std::numeric_limits<float>::max();
        for (const auto& ally : cambc::vision::champions(cambc::team::ally))
            try
            {
                const auto p = ally.position();
                if (p == obs.self_pos || !reach.is_set(p)) continue;
                const float hp = ally.health();
                if (hp >= ally.max_health() * 0.7f || !in_danger(p)) continue;
                if (hp < ally_hp) { ally_hp = hp; ally_pos = p; }
            }
            catch (const cambc::error&) { continue; }
        if (ally_hp < std::numeric_limits<float>::max())
            return exec::Cast{ static_cast<std::uint8_t>(slot), ally_pos, false };
    }

    // 3. Attack reset: when a basic attack would land this turn anyway,
    // casting the reset first is free damage — it restores the attack
    // allowance the cast doesn't consume (GameRules §Combat: reset
    // abilities are "walking compatible", attack allowance is separate),
    // and an enhanced reset turns the follow-up into a stun. apply()
    // performs the follow-up attack after a successful reset cast.
    if (me.can_attack())
    {
        const auto attack_tiles = me.attack_tiles();
        for (const auto& t : targets)
            if (attack_tiles.is_set(t.pos))
                for (std::uint32_t slot = 0; slot < 2; ++slot)
                {
                    const auto& ab = abilities[slot];
                    if (ab.info() && ab.is_ready() && ab.is_attack_reset())
                        return exec::Cast{ static_cast<std::uint8_t>(slot),
                            obs.self_pos, false };
                }
    }
    return std::nullopt;
}
