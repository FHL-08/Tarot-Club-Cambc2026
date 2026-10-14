// tactic/gank — carrier robbery (factory issue #11). A kill transfers the
// whole carried bag + 50g, but chasing a carrier burns route tempo and a
// failed gank can feed a counter-kill — so the module commits only when the
// burst math is already lethal inside the intercept window: Σ helper damage
// (strength × hits that land before the bag is safe, plus this champion's
// best ready ability once) ≥ the target's live HP. Conservative gates:
// never while hurt, never while carrying a steal-worthy bag ourselves, and
// a converge Move never steps off a live capture (attacking from the tile
// is still free).
#include "tactic.h"
#include "support.h"
#include <cambc.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace {

// Only a bag worth stealing justifies the detour — matches the verify bar
// ("enemies carry ≥20 gems").
constexpr std::uint32_t bag_threshold = 20;
// Carrying more than this, *we* are the piñata — a death hands the bag over.
constexpr std::uint32_t own_bag_limit = 30;
// A blind ally follows a wire sighting only inside roughly vision distance —
// on arrival it either sees the target and re-evaluates for real, or drops.
constexpr std::int64_t follow_range = 8;

struct Helper {
    cambc::vec2 pos;
    float strength = 0;
    std::int64_t range = 1;
    std::int64_t eta = 0;   // turns until inside attack range of the target
    std::int64_t hits = 0;  // hits that land before the bag is safe
};

// Rounds until the carried bag is beyond reach: a channelled carrier gives
// its channel_left; a runner needs its distance to the nearest altar plus
// the tribute ticks the channel still has to sit through there.
std::int64_t loot_window(const EnemyObs& e, std::int64_t stationary)
{
    if (stationary > 0) return stationary;
    std::int64_t escape = std::numeric_limits<std::int64_t>::max();
    for (const auto altar : tactic::altars())
        escape = std::min(escape, e.position.chebyshev_distance(altar));
    return escape + static_cast<std::int64_t>((e.gems + 9) / 10);
}

} // namespace

std::optional<exec::Action> tactic::gank(const Obs& obs)
{
    const auto& me = cambc::self();
    if (obs.self_health < obs.self_max_health * 0.45f) return std::nullopt;
    if (obs.self_gems >= own_bag_limit) return std::nullopt;

    // Standing on a reward tile mid-capture: leaving resets the clock, so
    // only attacks and casts are allowed — never a converge Move.
    const int here = group_of(obs.self_pos);
    bool capturing = false;
    if (const CampObs* c = seen_camp(obs, here); c
        && std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
        capturing = true;

    // Candidates are vision-gated by construction — obs.enemies only lists
    // what this champion sees, and bags are only readable on visible units.
    struct Cand {
        std::size_t idx;
        std::int64_t window;
        bool stationary;
    };
    std::vector<Cand> cands;
    for (std::size_t i = 0; i < obs.enemies.size(); ++i)
    {
        const auto& e = obs.enemies[i];
        if (e.gems < bag_threshold) continue;
        std::int64_t stationary = 0;
        if (e.submitting)
        {
            stationary = e.channel_left;
        }
        else
        {
            try
            {
                const auto enemy = cambc::vision::champion_at(e.position);
                if (enemy.is_recalling()) stationary = enemy.remaining_channel_time();
            }
            catch (const cambc::error&) { }
        }
        cands.push_back({ i, loot_window(e, stationary), stationary > 0 });
    }

    const EnemyObs* best = nullptr;
    std::int64_t best_window = 0, best_my_eta = 0, best_my_hits = 0;
    float best_others = 0, best_my_spell = 0;
    int best_spell_slot = -1;
    bool best_holder = false;

    if (!cands.empty())
    {
        // Helpers: me plus every visible ally body. Ally ability cooldowns
        // are private, so allies contribute basic attacks only; an ally
        // holding a capture tile won't converge, so its share is fiction.
        std::vector<Helper> helpers{ { obs.self_pos, me.strength(), me.attack_range() } };
        for (const auto& ally : cambc::vision::champions(cambc::team::ally))
        {
            const auto pos = ally.position();
            if (pos == obs.self_pos) continue;
            if (const CampObs* c = seen_camp(obs, group_of(pos));
                c && std::holds_alternative<cambc::map::camp_group_reward_available>(c->state))
                continue;
            helpers.push_back({ pos, ally.strength(), ally.attack_range() });
        }

        // One successful cast per turn — my burst adds only the best ready
        // damaging ability, once.
        int spell_slot = -1;
        float spell_range = 0;
        if (me.can_cast())
        {
            const auto abilities = me.abilities(); // copy of the slot views — binds live owner handles
            for (std::uint32_t s = 0; s < 2; ++s)
            {
                const auto& ab = abilities[s];
                if (!ab.info() || !ab.is_ready() || !ab.can_target_enemies()
                    || !(ab.is_damage() || ab.is_cc()))
                    continue;
                const float r = std::visit(
                    [](const auto& v) -> float {
                        if constexpr (requires { v.range; }) return v.range;
                        return 0.f;
                    },
                    *ab.info());
                if (r > spell_range) { spell_range = r; spell_slot = static_cast<int>(s); }
            }
        }

        for (const auto& cand : cands)
        {
            const EnemyObs& e = obs.enemies[cand.idx];
            // A channelled carrier stands still for the whole window — every
            // helper hits once it walks into range. A runner only feeds hits
            // to bodies already on top of it: melee at eta ≤ 1 shadows the
            // rest of the way (same-speed pursuit never closes otherwise),
            // ranged lands a short volley before the runner exits the bubble.
            float total = 0;
            std::int64_t my_eta = 0, my_hits = 0;
            std::size_t holder = 0;
            for (std::size_t h = 0; h < helpers.size(); ++h)
            {
                Helper& helper = helpers[h];
                helper.eta = std::max<std::int64_t>(0,
                    helper.pos.chebyshev_distance(e.position) - helper.range);
                if (cand.stationary)
                    helper.hits = std::max<std::int64_t>(0, cand.window - helper.eta);
                else if (helper.range <= 1)
                    helper.hits = helper.eta <= 1
                        ? std::max<std::int64_t>(0, cand.window - helper.eta) : 0;
                else
                    helper.hits = helper.eta <= 4
                        ? std::max<std::int64_t>(0, std::min<std::int64_t>(cand.window - helper.eta, 3))
                        : 0;
                total += helper.strength * static_cast<float>(helper.hits);
                if (h == 0) { my_eta = helper.eta; my_hits = helper.hits; }
                if (helper.eta < helpers[holder].eta
                    || (helper.eta == helpers[holder].eta && helper.pos < helpers[holder].pos))
                    holder = h;
            }

            float my_spell = 0;
            if (spell_slot >= 0
                && obs.self_pos.chebyshev_distance(e.position) <= static_cast<std::int64_t>(spell_range))
            {
                try
                {
                    my_spell = me.abilities()[static_cast<std::uint32_t>(spell_slot)]
                                   .damage_amount(cambc::vision::champion_at(e.position));
                }
                catch (const cambc::error&) { my_spell = 0; }
            }
            total += my_spell;
            if (total < e.health) continue; // not provably lethal — pass

            if (!best || e.gems > best->gems
                || (e.gems == best->gems && e.position < best->position))
            {
                best = &e;
                best_window = cand.window;
                best_my_eta = my_eta;
                best_my_hits = my_hits;
                // Damage the target takes from everyone but me this window.
                best_others = total - helpers[0].strength * static_cast<float>(my_hits) - my_spell;
                best_my_spell = my_spell;
                best_spell_slot = spell_slot;
                best_holder = holder == 0;
            }
        }
    }

    if (best)
    {
        const EnemyObs& e = *best;
        // The designated bag-holder is the helper with the shortest ETA — a
        // deterministic pick every watcher computes identically. Alarm first
        // so allies converge while there's still a bag to steal.
        alarm_sighting(obs, e.position);
        const bool needed = best_others < e.health;

        const cambc::bitboard attack_tiles = me.can_attack() ? me.attack_tiles() : cambc::bitboard{};
        if (attack_tiles.is_set(e.position))
        {
            if (me.strength() >= e.health)
            {
                std::cout << "TACTIC gank: kill @r" << obs.round << " bag=" << e.gems << '\n';
                return exec::Attack{ e.position, exec::Attack::Kind::champion };
            }
            if (best_holder || needed)
            {
                std::cout << "TACTIC gank: hit @r" << obs.round << " bag=" << e.gems << '\n';
                return exec::Attack{ e.position, exec::Attack::Kind::champion };
            }
            // A closer ally pockets this one — no reason to swing, and no
            // converge: we're already inside the ring.
            return std::nullopt;
        }
        else if (best_spell_slot >= 0 && best_my_spell >= e.health)
        {
            std::cout << "TACTIC gank: kill @r" << obs.round << " bag=" << e.gems << '\n';
            return exec::Cast{ static_cast<std::uint8_t>(best_spell_slot), e.position, false };
        }
        if (!capturing && best_my_hits > 0)
        {
            // Converge: descend onto the attack ring so ranged helpers stop
            // at firing distance instead of hugging the target tile.
            cambc::bitboard ring;
            ring.draw_chebyshev(e.position, static_cast<float>(me.attack_range()));
            ring &= ~blocked_tiles();
            if (ring.is_empty()) ring.set(e.position);
            std::cout << "TACTIC gank: converge @r" << obs.round << " bag=" << e.gems << '\n';
            return exec::Move{ std::move(ring) };
        }
    }

    // Blind converge: another ally verified a kill this round and alarmed it.
    if (!capturing && obs.wire.sighting_valid && wire::is_current(obs.wire, obs.round)
        && wire::sighting_age(obs.wire, obs.round) <= 1
        && obs.self_pos.chebyshev_distance(obs.wire.sighting_pos) <= follow_range)
    {
        cambc::bitboard goals;
        goals.set(obs.wire.sighting_pos);
        std::cout << "TACTIC gank: converge @r" << obs.round << " (alarm)\n";
        return exec::Move{ std::move(goals) };
    }
    return std::nullopt;
}
