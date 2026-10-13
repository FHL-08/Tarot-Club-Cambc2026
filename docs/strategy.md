# Tarot Club — Base Strategy

Status: ratified by strategy tournament (20 candidate lenses, adversarial review).
This document is the contract for everything the factory builds. Bot codename:
**FOUNDRY** (`bots/foundry`).

## The thesis

Camps are deterministic periodic producers and the whole map is visible at
draft with 10× fuel — so the game is a **compile-time scheduling problem**,
not a turn-by-turn decision problem. FOUNDRY compiles a near-optimal cyclic
harvest plan into the 1024-byte draft memory at draft, then each champion
executes its route as a small finite-state machine. Runtime compute is spent
on two things only: executing the plan and exploiting deviations we can see
the enemy make (channels, carriers, contested captures).

We win by throughput first (schedule dominance over reactive bots), denial
second (tactics as interrupt modules), and never by accident.

## Why this beat the alternatives

| Rejected lens | Verdict |
| --- | --- |
| Pure reactive utility (argmax per turn) | Dithering + re-derives what draft already solved; kept as the deviation fallback |
| Altar lockdown / tribute denial as base | Dribble-banks (≤10 gems = 1-tick channels) break it; altars outnumber bodies. Kept as a tactic |
| Death-ball / assassin comp | Can't force fights on evaders; loses the tempo race on large maps. Kept as a tactic (gank read) |
| Opponent modeling (HIGHWAYMAN) | Linchpin (vision-free enemy telemetry) is unverified; revisit once probed |
| Tiebreak turtle (ESCROW) | Tiebreaks are consolation — a pipeline that hits 1000 first never needs them |
| Champion hyper-carry builds (FUSELINE, HOARDLIGHT, FIELDFORGE, GEMINI) | Correct as role modules inside the plan; wrong as the architecture |

## The mechanics this exploits (in priority order)

1. **Free omniscience**: `map::camp_groups()` is callable at draft — positions,
   `gem_value`/`gold_value`, `spawn_tiles`, `max_respawn_timer`. Respawn round
   is `capture_round + period`; the capturer never needs to watch.
2. **Host-side compute arbitrage**: `bitboard` + `distance_field` ops are host
   imports — spatial compute outside the 100k fuel meter. A whole-map
   multi-field evaluation costs ~1% of a turn.
3. **Capture-overlap tribute**: tribute duration locks at channel *start* but
   pays out the bag at *completion*, and captures resolve before channels
   complete in the same round. Standing on an altar-adjacent camp the round
   its capture lands, a 1-tick channel sweeps the whole reward.
4. **Passive capture, free actions**: holding spawn tiles for the 5-round
   capture costs no actions — attack/nuke an adjacent group through it.
5. **Occupied tiles delay respawn**: step off your own capture at timer zero;
   parking a body on an enemy camp's tiles starves their cycle.
6. **Interrupt asymmetry**: damage never breaks tribute — only stun, death,
   or a 10g altar lock. Small bags (≤10 gems) are near-atomic.
7. **Bag theft**: killer inherits the whole carried bag +50g. Fat enemy
   carriers are piñatas; `gem_count()`/`is_submitting_gems()`/
   `remaining_channel_time()` are readable on visible enemies.
8. **Nearest-aggro tanking**: camps strike the nearest champion and never
   chase; a range-1 escort absorbs all retaliation while a range-3+ carry
   free-farms.
9. **Deterministic consensus**: identical pure functions over shared inputs
   (draft memory + 8B blackboard) = coordination without a protocol. Ally
   death coverage needs zero communication when the relief rule is shared.

## Architecture

```
DRAFT (compile time — full map, 10x fuel)
  atlas.cpp     map → CampTable {gems, gold, hp, period, tiles},
                POI distance matrix (host-side distance_field)
  planner.cpp   partition camps → per-champion cyclic routes
                (EDD/pinwheel; service time per kit; altar stops spliced
                at bag thresholds; capture-overlap tiles flagged;
                pitstops at predicted HP crossings)
  loadout.cpp   pick order + per-role build queues
                → all three write one Plan blob into draft_memory

RUNTIME (each champion — O(#guards) fuel)
  plan.h        decode draft blob → my route (sole parser seam)
  exec.cpp      FSM: TRAVEL → CLEAR → CAPTURE → HAUL → BANK → PITSTOP
  nav.h         distance_field descent + hidden-blocker false-retry
  ledger.cpp    ready_at[camp] = capture_round + period (self exact,
                ward-sniped enemy timers, unseen = alive)
  replan.cpp    deviation handler — deterministic relief rule all bots
                compute identically (nearest ready-at on dead ally's route)
  tactic/       pluggable interdiction modules — each returns an action
                or nullptr, evaluated before the route step:
      deny.cpp      contest enemy captures; park on their respawn tiles
      gank.cpp      kill/bag-steal visible fat carriers (burst math first)
      lock.cpp      drop altar locks on enemy tribute lines
      peel.cpp      stun/cancel enemy channels in vision
  econ.cpp      build-order cursor + tribute threshold policy
  wire.h        8-byte shared memory codec (claims, contested bits,
                last enemy sighting, alarm altar)
```

Seams are data: the `Plan` byte layout and `ready_at` table are the
interfaces. Any tactic module can be replaced; any champion role module can
be swapped behind `tick(Me, Obs) -> Action`.

## Draft policy

- **Picks** (priority order, react to enemy reveals): Vendetta (camp-kill
  resets = fastest cycler) → Cassi & Issac (two bodies per pick; Cassi's
  walk-compatible r4 stun is the best channel interrupt in the roster) →
  Varg (splash on cardinally-connected packs; shield tanks elk/monolith) →
  Tavi (zero recall downtime, field shop) → Vesper Vale (+20% gold engine;
  sole-capturer funnel). 3-pick maps: Vendetta + C&I + Varg.
- **Counter-drafting**: enemy stun-stack → Marcella (stun-immune banker);
  enemy C&I → focus the squishy twin (one kill takes both).
- **Solo captures**: helpers vacate spawn tiles before the tick — rewards
  split by presence, so one capturer takes all (Vesper takes gold camps for
  the 1.4× funnel).
- **Asymmetry for mirrors**: derive route assignments from team id; full
  ties gift Team B, so build an asymmetric breakpoint into the plan.

## Shared memory protocol (8 bytes)

```
[0] frame parity (round & 0xFF)
[1-2] last enemy sighting: pos x,y packed + round tag
[3] contested camp idx | alarm altar idx
[4] claim mask lo (camp bits 0-7)
[5] claim mask hi | relief claim bits
[6] tribute alarm (altar idx | enemy channel flag)
[7] checksum
```

Ally turns are sequential within a round → single-writer-per-slot is safe.
Stale-parity entries expire next round.

## Champion-build queues (defaults)

- Vendetta: Monster str ×5 → Jungle str → Power str
- Varg: Jungle str ×5 → Monster str → Power str
- Tavi: Jungle str → Treasure str → Monster str → Power focus (sells Treasure
  ~r1600 → Power, 100% refund)
- Vesper: Treasure str ×5 → Monster str → Power str
- Cassi: Power focus ×5; Issac: Jungle/Durability health
- Everyone on spawn: carry altar lock + teleport crystal when routed past
  enemy-side altars

## Test conditions

The factory's `Verify:` floor is `build` + full `run --statistics`. A change
merges only through these gates (staged — earlier gates run first):

**L0 · Build** — `./cambc.exe build foundry` clean; `botFaults == 0` in stats.

**L1 · Machine invariants** (asserted in bot via debug counters / stdout):
- no turn where a champion is idle while an owned, alive, uncontested camp
  exists on its route
- no uncontested capture exceeding 5 rounds + 1 turn
- champion steps off a captured group's spawn tiles before its respawn round
- zero `bot error` lines; no fuel suspension after turn 1

**L2 · Determinism** — same command twice → byte-identical statistics JSON.
Mirror match completes without faults.

**L3 · Performance floor vs starter-cpp** (`run-batch` over all 9 maps):
- win or lead tributed gems in ≥7/9 matches
- `submittedGems ≥ 400` by round 500 on `expanse`/`packs`
- median `deaths ≤ 2`; `earnedGold ≥ 1.3×` opponent on funnel builds

**L4 · Robustness drills** (fixture enemies as written):
- vs a denial dummy that stands on our capture tiles: still tribute ≥200 by
  round 500 (graceful degradation, not collapse)
- vs starter on `monolith`: the 200-gem push fires only when the plan gates
  it (alive-count + DPS breakpoint)
- carry-death audit: any death carrying ≥100 gems → halve bag cap, retest

## What this is NOT

- Not a learned/search policy — fuel forbids it; the field analysis showed
  in-guest search is dominated by compile-time scheduling anyway.
- Not rigid: route following is the default; `replan` + tactics are live.
- Not final: tactics modules are where we iterate post-baseline (each tactic
  is a factory brief; the plan format is the contract they all obey).
