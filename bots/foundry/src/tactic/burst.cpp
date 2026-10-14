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

    // 1. Damage / AoE / cc on enemy champions. Every ready slot — whether
    // it targets enemy entities or only ground tiles (Stellar/Ryth/Prism
    // area spells: enemy-only checks used to skip them entirely) — scores
    // candidate casts the same way: enemies hit, then summed
    // damage_amount, then channel/health/bag priority. An area stun that
    // would only re-stun already-stunned enemies ranks last.
    int best_slot = -1;
    cambc::vec2 best_tile{};
    bool best_ground = false;
    int best_hits = 0;
    float best_dmg = -1;
    bool best_restun = false;
    const Target* best_t = nullptr;
    auto better_target = [](const Target& a, const Target* b) {
        if (!b) return true;
        if (a.channelling != b->channelling) return a.channelling;
        if (a.health != b->health) return a.health < b->health;
        return a.gems > b->gems;
    };
    auto consider = [&](std::uint32_t slot, cambc::vec2 tile, bool ground,
                        int hits, float dmg, const Target& anchor, bool restun) {
        bool better;
        if (best_slot < 0) better = true;
        else if (restun != best_restun) better = !restun;
        else if (hits != best_hits) better = hits > best_hits;
        else if (dmg != best_dmg) better = dmg > best_dmg;
        else better = better_target(anchor, best_t);
        if (better)
        {
            best_slot = static_cast<int>(slot);
            best_tile = tile;
            best_ground = ground;
            best_hits = hits;
            best_dmg = dmg;
            best_restun = restun;
            best_t = &anchor;
        }
    };
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.info() || !ab.is_ready()) continue;
        const bool cc = ab.is_cc(), aoe = ab.is_aoe_damage();
        if (!ab.is_damage() && !cc && !aoe) continue;
        cambc::bitboard reach{ cambc::uninitialized };
        ab.mask_into(reach);
        if (ab.can_target_enemies())
            for (const auto& t : targets)
            {
                if (!reach.is_set(t.pos)) continue;
                float dmg = 0;
                try { dmg = ab.damage_amount(t.handle); }
                catch (const cambc::error&) { continue; }
                consider(slot, t.pos, false, 1, dmg, t, cc && t.stunned);
            }
        if (ab.can_target_ground() && (aoe || cc))
        {
            // The cc payload carries no radius — every ground cc kit ships
            // "radius one" (Ryth's area stun), so 1 is the kit constant.
            const float radius = aoe ? ab.get<cambc::ability_aoe_damage>().radius : 1.0f;
            for (const auto& t : targets)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        const cambc::vec2 centre{ t.pos.x + dx, t.pos.y + dy };
                        if (!reach.is_set(centre)) continue;
                        int hits = 0;
                        float dmg = 0;
                        bool all_stunned = true;
                        const Target* anchor = nullptr;
                        for (const auto& e : targets)
                        {
                            if (centre.chebyshev_distance(e.pos)
                                > static_cast<std::int64_t>(radius))
                                continue;
                            ++hits;
                            all_stunned = all_stunned && e.stunned;
                            try { dmg += ab.damage_amount(e.handle); }
                            catch (const cambc::error&) { }
                            if (better_target(e, anchor)) anchor = &e;
                        }
                        if (hits > 0)
                            consider(slot, centre, true, hits, dmg, *anchor,
                                cc && all_stunned);
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
