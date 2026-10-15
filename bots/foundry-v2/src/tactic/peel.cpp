// tactic/peel — channel interruption (factory issue #9). Damage never
// breaks a tribute channel (GameRules §Channels): only stun, death or an
// altar lock do — recall additionally breaks on any damage at all. Peel
// spends this champion's interrupt on the best channel in vision, ranked by
// remaining_channel_time × gem_count: a ready cc cast (Cassi, Ryth, Tavi,
// Ren, Prism — detected via ability kind, not roster), a primed enhanced
// attack (Marcella), a plain attack on a recaller, or an altar lock on the
// channel's altar. What we can't reach this turn is raised on the wire so a
// closer ally can take it.
#include "tactic.h"
#include "support.h"
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct ChannelTarget {
    cambc::vec2 pos;
    std::int32_t left = 0;
    std::uint32_t gems = 0;
    bool tribute = false; // false = recall
    std::int64_t score = 0;
};

// The champion's live interrupt kit; Obs carries no cooldown state, so this
// is an engine read by contract (exec.h — "engine reads only").
struct Interrupts {
    int cc_slot = -1;      // a ready crowd-control ability
    float cc_range = 0;
    bool cc_ground = false;  // ground-targeted (Ryth) vs unit-targeted
    bool primed = false;     // enhanced-attack buff up — next hit stuns
    int prime_slot = -1;     // ready attack_reset that grants the buff
    bool lock = false;       // carrying an altar lock
};

Interrupts interrupts()
{
    Interrupts kit;
    const auto& me = cambc::self();
    kit.primed = me.buffs().has_enhanced_attack();
    kit.lock = me.consumables()[cambc::consumable_type::altar_lock].count() > 0;
    if (!me.can_cast()) return kit;
    const auto abilities = me.abilities(); // copy of the slot views — binds live owner handles
    for (std::uint32_t s = 0; s < 2; ++s)
    {
        const auto& ab = abilities[s];
        if (!ab.info() || !ab.is_ready()) continue;
        if (ab.is_cc())
        {
            kit.cc_slot = static_cast<int>(s);
            kit.cc_range = ab.get<cambc::ability_cc>().range;
            kit.cc_ground = ab.can_target_ground() && !ab.can_target_enemies();
        }
        else if (ab.is_attack_reset() && ab.get<cambc::ability_attack_reset>().enhanced_attack
                 && ab.can_target_self())
        {
            kit.prime_slot = static_cast<int>(s);
        }
    }
    return kit;
}

} // namespace

std::optional<exec::Action> tactic::peel(const Obs& obs)
{
    if (obs.enemies.empty()) return std::nullopt;
    const auto& me = cambc::self();

    std::vector<ChannelTarget> targets;
    for (const auto& e : obs.enemies)
    {
        ChannelTarget t;
        t.pos = e.position;
        t.gems = e.gems;
        if (e.submitting)
        {
            t.tribute = true;
            t.left = e.channel_left;
        }
        else
        {
            // Obs snapshots tribute only — recall state is a live query on
            // the visible handle; a stale tile is simply skipped.
            try
            {
                const auto enemy = cambc::vision::champion_at(e.position);
                if (!enemy.is_recalling()) continue;
                t.left = enemy.remaining_channel_time();
            }
            catch (const cambc::error&) { continue; }
        }
        t.score = static_cast<std::int64_t>(t.left) * t.gems;
        targets.push_back(t);
    }
    if (targets.empty()) return std::nullopt;
    std::sort(targets.begin(), targets.end(), [](const ChannelTarget& a, const ChannelTarget& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.pos < b.pos;
    });

    const Interrupts kit = interrupts();
    const cambc::bitboard attack_tiles = me.can_attack() ? me.attack_tiles() : cambc::bitboard{};

    for (const auto& t : targets)
    {
        // Recall breaks on ANY damage — a basic attack is the free cancel.
        if (!t.tribute && attack_tiles.is_set(t.pos))
        {
            std::cout << "TACTIC peel: cancelled recall @r" << obs.round << '\n';
            return exec::Attack{ t.pos, exec::Attack::Kind::champion };
        }
        if (kit.cc_slot >= 0
            && obs.self_pos.chebyshev_distance(t.pos) <= static_cast<std::int64_t>(kit.cc_range))
        {
            std::cout << "TACTIC peel: cancelled " << (t.tribute ? "tribute" : "recall")
                      << " @r" << obs.round << '\n';
            return exec::Cast{ static_cast<std::uint8_t>(kit.cc_slot), t.pos, kit.cc_ground };
        }
        // A primed enhanced attack stuns whatever it hits — either channel.
        if (kit.primed && attack_tiles.is_set(t.pos))
        {
            std::cout << "TACTIC peel: cancelled " << (t.tribute ? "tribute" : "recall")
                      << " @r" << obs.round << '\n';
            return exec::Attack{ t.pos, exec::Attack::Kind::champion };
        }
        // Marcella priming now stuns with next turn's attack — only useful
        // while the channel outlives the wait.
        if (kit.prime_slot >= 0 && t.left >= 2 && attack_tiles.is_set(t.pos))
        {
            std::cout << "TACTIC peel: primed @r" << obs.round << '\n';
            return exec::Cast{ static_cast<std::uint8_t>(kit.prime_slot), obs.self_pos, false };
        }
        // A lock placed on the tribute altar cancels the channel outright
        // and holds the altar afterwards — the peel the enemy can't dispel.
        if (t.tribute && kit.lock)
        {
            for (const auto altar : altars())
            {
                if (t.pos.chebyshev_distance(altar) > cambc::constants::submit_gems_range
                    || obs.self_pos.chebyshev_distance(altar) > 2
                    || cambc::vision::has_altar_lock(altar))
                    continue;
                std::cout << "TACTIC peel: cancelled tribute @r" << obs.round << '\n';
                return exec::UseItem{ cambc::consumable_type::altar_lock, altar };
            }
        }
    }

    // Unreachable this turn — flag the best tribute channel on the wire.
    // The sorted order means a high-scoring recaller must not mask a lower
    // tribute: scan for the first tribute, not just the top pick.
    for (const auto& t : targets)
    {
        if (!t.tribute) continue;
        bool raised = false;
        for (const auto altar : altars())
            if (t.pos.chebyshev_distance(altar) <= cambc::constants::submit_gems_range)
            {
                alarm_channel(obs, altar_index(altar));
                raised = true;
                break;
            }
        if (raised) break; // alarm once — the highest-scored visible tribute
    }
    return std::nullopt;
}
