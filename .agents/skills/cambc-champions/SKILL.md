---
name: cambc-champions
description: Champion roster reference for cambc2026a — kits, base stats, growths, pick considerations. Use when drafting logic, matchup reasoning, or any decision that needs "what does this champion do".
---

# cambc champions

Fact of record: `docs/GameRules.md` → "Champion roster" (kits + stat
tables). If docs-MCP is connected, `champion <name>` returns the entry.

## Roster at a glance

| Champion | Role | Range | Notable |
| --- | --- | --- | --- |
| Cassi & Issac | paired pick (ranged caster + melee fighter) | 3 / 1 | one draft slot → two bodies; either dying kills both |
| Ryth | melee | 1 | AoE stun + attack reset; gains Strength = 1% bonus health |
| Vesper Vale | ranged | 3 | +20% gold passive; attack reset + dash |
| Prism | ranged caster | 3 | +0.2 Focus per carried gem; damage+stun, AoE damage |
| Stellar | ranged caster | 3 | 2×Focus nuke or 1.4× AoE |
| Vendetta | melee | 1 | kills reset all ability cooldowns (camps count) |
| Marcella | melee | 1 | stun-immune; enhanced-attack stun + 2×Focus spell |
| Tavi | ranged | 3 | shops anywhere, sells at full price; 2.2×Focus + stun |
| Willow | ranged support | 3 | 3×Focus ally heal + attack reset; weak growths |
| Ren | melee | 1 | 0.2×Focus retaliation; damage+stun + dash |
| Varg | melee | 1 | splash basics (Str to adjacent enemies); reset + self-shield |

All champions: vision 8, walk 1 tile, two ability slots, 8-turn cooldowns,
one successful cast per turn. Base/growth stats are per-champion — read
the table in GameRules.md before reasoning about shard value.

## Draft considerations

- Picks are simultaneous reveals, 3–5 champions, unique per team but
  mirrors allowed. Cassi & Issac cost one pick for two bodies.
- The map is fully visible in draft and draft fuel is 10x — precompute
  camp targets, routes, and assignments into the 1024-byte draft memory.
- Matchup hooks: stun-immune Marcella vs stun-reliant kits; Vendetta
  chains resets off camp kills; Prism hoards gems for Focus but drops
  them on death; Willow extends fights.
