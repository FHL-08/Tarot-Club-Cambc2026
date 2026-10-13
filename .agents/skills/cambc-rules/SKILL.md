---
name: cambc-rules
description: Answer game-rules questions for cambc2026a — turn order, channels, capture, tribute, vision, items — from docs/GameRules.md as the fact of record. Use whenever a change depends on how the game actually works.
---

# cambc rules

`docs/GameRules.md` is the exported handbook and the fact of record. Answer
from it; do not reason from League/DOTA intuition. If the docs-MCP server is
connected, use `get_rule`/`search_docs`; otherwise grep the file.

## Sections that matter

- **Winning**: first to 1,000 tributed gems, else at round 2,000 compare
  tributed gems → lifetime gold → kills → Team B. Carried gems are not score.
- **Draft**: 3–5 champions, simultaneous reveals, unique per team (mirrors
  allowed). 1024-byte draft memory persists read-only into the match.
- **Turn order**: round start (camp respawns, spell/ward updates, captures,
  camp actions) → champion turns alternating teams, starting team swaps each
  round; within a team, priority then draft order. `set_priority` applies
  next round. Dead champions still act (to shop).
- **Turn allowances**: one move (8-dir, 1 tile), one basic attack, one
  successful cast across both slots. Melee range 1, ranged range 3; ranged
  cannot move + basic attack same turn (attack-reset abilities restore it).
  Ability cooldowns: 8 owner turn ends per slot.
- **Movement**: walk 1; dashes up to 2 steps along legal paths over holes;
  teleport crystals 3 tiles ignoring terrain after a 3-tick channel.
  Landing on a hidden occupant returns `false` (allowance preserved, tile
  revealed) — already-known occupied raises `cambc::error`.
- **Camps**: monsters sleep until damaged; two damage-free rounds → sleep.
  All attack range 3, vision 8, see through bushes, never chase. Capture =
  occupy a dead group's spawn tiles uncontested through capture phases for a
  5-round timer; reward split among present allies, lowest ID gets remainder.
- **Tribute**: stand within Chebyshev 1 of an altar; channel =
  `max(1, ceil(gems/10))` owner turn ends, starting turn counts, completes at
  next turn start after zero. Captures resolve before channels — same-round
  captured gems count. Stun/death/altar-lock interrupt; damage does not.
- **Channels**: recall 5 ticks (3 with Home), tribute per above, teleport
  crystal 3 ticks. Recall dies to damage/stun; tribute and teleport do not.
- **Vision**: raycast symmetric shadowcasting, range 8. Walls and bushes
  block; standing in a bush sees through that bush cluster only. Holes
  don't block sight. Vision is per-champion — allies share nothing except
  through the 8-byte shared memory. One active ward per champion,
  placeable within 2 tiles, replaceable once per round.
- **Death**: killer takes all carried gems + 50g. Camp kills destroy gems.
  Respawn after 60 owner turn-ends on a free spawn tile, full health,
  abilities reset. Cassi & Issac die together.
- **Items**: shards 30g, add the champion's growth for the chosen stat
  (Strength/Health/Focus). 5 matching shards → emblem with a passive.
  Consumables 10g (altar lock, teleport crystal), one of each carried.
  Tavi shops anywhere / sells at full price; Health on spawn heals.

## Monsters

Wolf 1350/20 rng3 → 90g+22gems r40 · Elk 2400/60 rng3 → 160g+36gems r60 ·
Gem cluster 500 → 24gems r50 · Gold cluster 500 → 35g r50 ·
Monolith 20000/200 rng5 → 300g+200gems r150.

## Gotchas people get wrong

- A 1-tick tribute finishes at your NEXT turn start, not at turn end.
- Ranged "excludes walking" abilities also lock the move+attack rule.
- An unseen enemy blocks a legal-looking move — `false`, not an exception.
- Emblem passives don't stack; shard stats do.
- Refunded gold (Tavi sells) is not "earned gold" for the tiebreak.
