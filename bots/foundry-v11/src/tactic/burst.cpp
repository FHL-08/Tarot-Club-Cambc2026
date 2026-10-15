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
    float attack_range = 0; // read once — it can't change mid-turn
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
    const auto& me = turn::self();
    if (!turn::self_state().contains(cambc::champion_state::can_cast)) return std::nullopt;
    // Below the pitstop line the retreat owns the turn — poke carries the
    // same guard; casting instead of recalling is how bodies get chained.
    // Under a reachable enemy the floor drops to 0.3: spending a cast beats
    // dying with it held.
    float gate = 0.6f;
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0) continue;
        const auto r = turn::enemy_attack_range(e.position);
        if (r && obs.self_pos.chebyshev_distance(e.position) <= *r + 1) { gate = 0.3f; break; }
    }
    if (obs.self_health < obs.self_max_health * gate) return std::nullopt;
    const auto& abilities = turn::abilities();

    // The stun flag only feeds cc re-stun scoring — a buffs() call per
    // enemy is wasted when no cc slot could cast anyway. A throwing
    // info() inside this probe is swallowed: the slot loop below reaches
    // the same throw on the same slot and aborts identically.
    bool want_stun = false;
    try
    {
        for (std::uint32_t s = 0; s < 2 && !want_stun; ++s)
            want_stun = abilities[s].info().has_value() && abilities[s].is_cc()
                && turn::slot_ready(s);
    }
    catch (...) { }
    std::vector<Target> targets;
    for (const auto& e : obs.enemies)
    {
        if (e.health <= 0) continue;
        const auto* handle = turn::enemy_at(e.position);
        const auto recalling = turn::enemy_recalling(e.position);
        const auto stunned = want_stun ? turn::enemy_stunned(e.position)
            : std::optional<bool>{ false };
        const auto range = turn::enemy_attack_range(e.position);
        // left vision between collect and now — a nullopt is a thrown read
        if (!handle || !recalling || !stunned || !range) continue;
        targets.push_back({ *handle, e.position, e.health, e.gems,
            e.submitting || *recalling, *stunned, static_cast<float>(*range) });
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
    // damage_amount is a host call per (slot, enemy) — the turn-wide memo
    // in turn.h is shared with gank and dedupes the 9-centre scan.
    // -1 marks a thrown read (treated like today's catch-and-skip).
    auto dmg_of = [&](std::uint32_t slot, std::size_t i) {
        const auto d = turn::damage_amount(slot, targets[i].pos);
        return d ? *d : -1.f;
    };
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.info() || !turn::slot_ready(slot)) continue;
        const bool cc = ab.is_cc(), aoe = ab.is_aoe_damage();
        if (!ab.is_damage() && !cc && !aoe) continue;
        const cambc::bitboard& reach = turn::slot_reach(slot);
        const auto targets_mask = turn::slot_targets(slot);
        if (targets_mask.contains(cambc::ability_target_mask::enemies))
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                const auto& t = targets[i];
                if (!reach.is_set(t.pos)) continue;
                const float dmg = dmg_of(slot, i);
                if (dmg < 0) continue;
                consider(slot, t.pos, false, 1, dmg, t, cc && t.stunned);
            }
        if (targets_mask.contains(cambc::ability_target_mask::ground) && (aoe || cc))
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
                        for (std::size_t i = 0; i < targets.size(); ++i)
                        {
                            const auto& e = targets[i];
                            if (centre.chebyshev_distance(e.pos)
                                > static_cast<std::int64_t>(radius))
                                continue;
                            ++hits;
                            all_stunned = all_stunned && e.stunned;
                            dmg += std::max(0.f, dmg_of(slot, i));
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
                if (pos.chebyshev_distance(t.pos) <= t.attack_range + 1)
                    return true;
            }
            catch (const cambc::error&) { continue; }
        return false;
    };
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& ab = abilities[slot];
        if (!ab.info() || !turn::slot_ready(slot) || (!ab.is_heal() && !ab.is_shield()))
            continue;
        const cambc::bitboard& reach = turn::slot_reach(slot);
        const auto targets_mask = turn::slot_targets(slot);
        if (targets_mask.contains(cambc::ability_target_mask::self)
            && obs.self_health < obs.self_max_health * 0.7f
            && in_danger(obs.self_pos))
            return exec::Cast{ static_cast<std::uint8_t>(slot), obs.self_pos, false };
        if (!targets_mask.contains(cambc::ability_target_mask::allies)) continue;
        cambc::vec2 ally_pos{};
        float ally_hp = std::numeric_limits<float>::max();
        for (const auto& ally : turn::allies())
        {
            if (ally.pos == obs.self_pos || !reach.is_set(ally.pos)) continue;
            const auto hp = turn::ally_health(ally);
            const auto max_hp = turn::ally_max_health(ally);
            if (!hp || !max_hp) continue;
            if (*hp >= *max_hp * 0.7f || !in_danger(ally.pos)) continue;
            if (*hp < ally_hp) { ally_hp = *hp; ally_pos = ally.pos; }
        }
        if (ally_hp < std::numeric_limits<float>::max())
            return exec::Cast{ static_cast<std::uint8_t>(slot), ally_pos, false };
    }

    // 3. Attack reset: when a basic attack would land this turn anyway,
    // casting the reset first is free damage — it restores the attack
    // allowance the cast doesn't consume (GameRules §Combat: reset
    // abilities are "walking compatible", attack allowance is separate),
    // and an enhanced reset turns the follow-up into a stun. apply()
    // performs the follow-up attack after a successful reset cast.
    if (turn::self_state().contains(cambc::champion_state::can_attack))
    {
        const auto& attack_tiles = turn::attack_tiles();
        for (const auto& t : targets)
            if (attack_tiles.is_set(t.pos))
                for (std::uint32_t slot = 0; slot < 2; ++slot)
                {
                    const auto& ab = abilities[slot];
                    if (ab.info() && turn::slot_ready(slot) && ab.is_attack_reset())
                        return exec::Cast{ static_cast<std::uint8_t>(slot),
                            obs.self_pos, false };
                }
    }
    return std::nullopt;
}
