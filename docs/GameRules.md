# CAMBC Game Rules

Event: cambc2026a. Exported from the current handbook, including all published champion specifications. Interactive diagrams are represented as text.

## The game

### Overview

The map is populated with groups of monsters, called camps, that carry 2 valuable resources, gems  and gold

Illustration: Sample map.

In the game, you control a team of “champions” which are a set of combat units you will use to fight the monsters.

Using code, you must clear the monsters away, obtaining their resources.

The gems dropped are the main objective of the game.

By taking gems collected from clearing camps, and tributing/offering them at altars, you obtain victory points.

The most gems tributed by a team wins the game.

Additionally to gems, camps can drop gold. Gold is a resource used to improve the combat power of your “champions” by giving them more damage, or health.

### How to win

The first team to tribute 1,000 gems wins immediately. At the 2,000-round limit, compare tributed gems, then lifetime gold earned, then champion kills. If all tie, Team B wins.

The objective of the game is to tribute as many gems as possible! The game ends after a team tributes 1000 gems or round 2000 passes, whichever comes first.

#### Tiebreak notes

- The gold tiebreak counts all gold earned, not what is left to spend.
- If tributed gems, gold earned and kills all tie, the team that acted second in round one wins (Team B). The starting team still alternates each round.
- A local runner may stop before round 2,000.

### Obtaining Resources

Defeat every monster in a camp group, capture its region to receive gold and gems, then tribute carried gems at an altar. Spend gold to improve your champion.

The general flow of the game is to send your champions to defeat camps on the map, collect the gems they drop, then tribute the gems at altars.

Camps can also drop gold, which can be used on the champions' spawn tiles to purchase items making them stronger.

## Drafting

Before the match begins, you must select your team of “champions”.

### Choosing champions

Before the match begins, you must select your team of “champions”.

The map is fully visible in the drafting phase. By looking at the map layout, and where the monsters/camps are, your code can make an informed decision on the best champions to tackle the job.

Depending on the map, each team will select between 3 and 5 champions.

Champions are picked one by one.

For fairness, both teams select champions simultaneously, revealing their picks as they go.

Champions are unique, so you may only pick one of each, though you can select the same champions as your opponent, see the table below:

Selection rules

| Choice | Allowed |
| --- | --- |
| Not selected by your team | ✓ |
| Already selected by your team | ✗ |
| Selected by the opposing team | ✓ |
| Cassi & Issac (one selection) | ✓ |

### Shared draft memory

In addition to picking your champions, the drafting phase allows you to think carefully about your strategy and communicate it to your bots (code controlling the champions).

This is achieved by writing your opening plan into the 1024 byte draft memory, which can be read by your champion bots later.

The draft phase has additional fuel (compute) and can see the entire map layout, allowing for extra planning ahead.

### After the draft

After the draft, the selected champions are placed on each team's corresponding spawn tiles at full health.

### Missing a pick

If a draft bot yields, exhausts its fuel, or crashes without making a pick, the remaining selections are randomly selected by the game.

## A champion turn

Each turn a champion has a few options.

Every turn a champion can move one tile, in any 8 directions

Make a basic attack, dealing damage equal to their strength to a target in range

And use one of their two champion abilities, to activate a more powerful effect

If you are a ranged champion, you may NOT move, and basic attack on the same turn. This same rule applies for damaging abilities that are ranged as well.

Melee champions can walk and attack in the same turn. Ranged champions normally choose walking or a basic attack: either consumes the other allowance. Attack-reset abilities can restore the attack allowance.

In addition to the basic combat, a champion has a few other options to interact.

Each champion carries a ward, that grants them persistent vision of an area with a range of 6 tiles. They may leave this ward behind to keep watch of an area anywhere within 2 tiles of them, and is able to relocate them every turn.

When a champion is low on health, or simply needs to travel to their spawn tiles, they can activate a recall. This cancellable action takes 5 turns, and will teleport the champion straight to their spawn tiles.

On the spawn tiles, they may shop for items using gold an unlimited amount.

When carrying the all important gems, and are directly next to an altar, a feature of the map, a champion may tribute gems, a cancellable action that takes a number of turns, equal to the number of gems being tributed divided by 10

Additionally, you can use consumable items without limit (though you can only carry one of each at once).

#### Everything you can do

Each turn normally allows one ordinary move, one basic attack, and one successful ability cast across both slots. Melee attack range is 1 tile; ranged attack range is 3. Movement and attacks share an allowance for ranged champions. Abilities have an 8-turn-end cooldown per slot. Some abilities exclude walking or reset attacks. Wards require their trinket charge; consumables require inventory. Shop on your spawn or while dead (subject to champion exceptions). Starting a channel prevents other actions until it finishes or is cancelled. Recall and tribute each have one start and one cancellation per turn. Priority changes affect the next round.

#### The exact turn order

The game engine runs each round's actions through phases, see the below graphic to understand the order of operations:

At round start: camp respawns, existing spell/ward updates, camp captures, then camp actions. Champion turns follow, alternating teams; the starting team swaps each round. Each champion starts by resolving zero timers: respawn, ward recharge, buff/shield expiry, then channels. At turn end, timers decrement without triggering these effects; spawn recovery still happens at turn end. Recall arrives at turn start, so staying on spawn heals at that same turn's end.

##### Who goes first?

- **Teams alternate**: The team that starts swaps every round.
- **Priority, then draft order**: Within a team, higher priority acts first.
- **set_priority**: Takes effect from the next round and persists.
- **Dead champions still act**: They get a turn to shop.

**Order matters**

A capture can add gems before a tribute completes. Camps attack before your next turn.

## Maps & movement

Find a free tile in range. Do not path onto the target itself.

### Map features

The map is a grid, between 30*30 to 100*100, built from 5 different tiles as shown below:



| Feature | Description |
| --- | --- |
| Wall | Blocks line of sight, cannot be moved onto nor can it be dashed over. TP Crystal can reach walls. |
| Holes | Can be seen over, cannot be moved onto but can be dashed over. |
| Bush | Blocks line of sight, can be moved onto. If you are within a bush, you can see out of the bush. |
| Altar | Cannot be moved onto, used to submit gems. |

The map also is populated with camps/monsters as outlined in the next section.

### Ways to move

To traverse the map, a champion can move 1 step every turn with their basic move action. Dashes move up to 2 steps, including diagonals, along a legal dash path. Teleport crystals reach up to 3 tiles horizontally and vertically, including diagonals, and ignore terrain between you and the destination.

Ordinary movement reaches an adjacent tile in any of eight directions. Melee champions can also attack; ranged champions ordinarily choose one. Dashes use a legal path of up to 2 steps and may cross holes; teleport crystals have range 3 and ignore intervening terrain. Landing cannot be on a wall, hole, altar, enemy spawn or occupied tile.

Ask the SDK for your movement mask rather than recomputing walking yourself; it already accounts for the units you can see.

An unseen enemy in a bush can block a destination included in your observed movement mask. Attempting the step returns false, reveals that tile for the rest of the round and preserves your movement allowance. Choose another free tile and retry. A known occupied destination raises an error.

**Hidden enemies still block**

If an otherwise legal destination has an unseen occupant, the action returns `false` without throwing. The blocked tile is revealed to your champion for the rest of the round, and the attempt does not consume your movement allowance, ability cooldown or consumable, so you can try another destination. A destination already known to be occupied raises an error (`cambc::error` in C++). A successful action returns `true`. These rules also apply to dashes and teleport crystals; for a crystal, `true` means its channel started successfully.

## Camps & capture

The main way to obtain resources is to defeat camps or monsters, on the map. They can spawn alone or in a group (any monsters that are cardinally connected).

The main way to obtain resources is to defeat camps or monsters, on the map. They can spawn alone or in a group (any monsters that are cardinally connected).

### Pick your target



| Monster | Health | Strength | Range | Gold | Gems | Respawn |
| --- | --- | --- | --- | --- | --- | --- |
| Wolf | 1,350 | 20 | 3 | 90 | 22 | 40 rounds |
| Elk | 2,400 | 60 | 3 | 160 | 36 | 60 rounds |
| Gem cluster | 500 | 0 | 0 | 0 | 24 | 50 rounds |
| Gold cluster | 500 | 0 | 0 | 35 | 0 | 50 rounds |
| Monolith | 20,000 | 200 | 5 | 300 | 200 | 150 rounds |

The map is populated with a variety of monsters. They vary in health, strength (damage dealt per turn) and the rewards they drop.

The monsters are friendly until they are hit! When a champion strikes a monster, it will awaken attacking the nearest champion every turn, until it either dies or does not take damage for 2 turns.

### After defeating a camp, make sure to capture!

After every monster in a group dies, occupy at least one of its spawn tiles uncontested. At a capture phase your team starts a five-round timer; later capture phases count it down. Leaving the region empty or admitting an enemy resets progress. More allies do not speed it up. Rewards are shared among allies present when it finishes; the lowest ID receives any integer remainder. The group respawns together after rewards are claimed, using the longest cooldown of its members. Any occupied spawn tile delays the entire group.

After defeating an entire group (cardinally connected monsters), you can then capture the rewards by standing on one of the tiles a monster was underneath, and wait 5 turns (the capture timer). This will award the waiting champion the gold and gems from the camp. If there are multiple champions, the reward is split evenly among them.

### What happens next?

After a camp reward is captured, its respawn timer begins. After the timer has finished (ticking down by 1 each round), the camp will respawn in the same location as before, full health, ready to drop rewards again.

#### Capture and monster details

- Rewards combine every monster in the group.
- Allies on the region split the reward; the lowest ID gets the remainder.
- Extra allies do not speed capture. Empty or contested regions reset it.
- All monsters have attack range 3 and vision 8, see through bushes and do not chase.
- Damage wakes the group; two rounds without an in-range champion let it sleep.

## Gems & altars

The main objective of the game is to obtain gems by capturing camps, or stealing them from enemies. To actually progress towards winning the game however, the gems must first be tributed at altars.

The main objective of the game is to obtain gems by capturing camps, or stealing them from enemies. To actually progress towards winning the game however, the gems must first be tributed at altars.

### Carried or tributed

Carried gems can be stolen on death and do not count towards victory. Tributed gems are permanent team score.

### Tribute at an altar

Tributing requires a channel to secure the gems. The amount of time this takes scales with the number of gems you are carrying, namely the amount divided by 10, rounded up.

Stand within Chebyshev distance 1 of an altar with at least one gem and start a tribute. Duration is max(1, ceil(starting gems / 10)) champion turn ends: 1–10 gems take 1 countdown tick, 11–20 take 2. Timers decrement at turn end, including the starting turn, and complete at the owner's next turn start after reaching zero. Completion tributes the entire current bag. Damage does not interrupt it; stun, death or an altar lock does.

Tribute duration is max(1, ceil(carried gems / 10)) owner turn ends. The starting turn counts; completion waits until your next turn start after zero.

#### Timing details

- The channel ticks at the end of its champion’s turn, including the turn it starts. A timer that reaches zero stays pending until the start of that champion’s next turn. A one-tick tribute therefore completes at your next turn start, not at the end of the turn you start it.
- Its length uses your bag when you start. Completion tributes your whole bag, including gems gained since.
- Captures resolve before channels, so gems captured this round are included.

### Altar locks

Altar locks are a consumable you can purchase for 10g, and carry one at a time. You can place them on altars if you are within range, cancelling enemy channels, or simply delaying them by leaving them there. It takes 3 basic attacks to clear the lock, which is also triggered by the tributing action in the engine.

An altar lock blocks opposing tributes but allows its owner's team to tribute. Placing it cancels existing tribute channels at that altar. Three basic attacks remove it.

#### Tributing against a lock

Attempts to tribute against a locked altar are implicitly converted to auto attacks against the altar lock.

## Combat & abilities

Basic attacks use Strength. Spells use their own effects and Focus scaling.

### Basic attacks

Every turn champions can attack using their basic attack, melees can hit targets within 1 tile, and ranged within 3. Depending on whether you are the melee or ranged class will determine whether you can move and attack on the same turn or not:

Basic attacks deal Strength before modifiers and require a visible target. Ranges use Chebyshev distance, so diagonals count as one tile. Melee range is 1; ranged range is 3.

### Abilities scale with focus

Abilities scale with focus by taking the champion's focus value, and multiplying by the ability multiplier.

For example, Stellar starts with 100 focus, and has an ability dealing 2x focus damage. Therefore she can cast the ability on her turn dealing a burst of 200 damage, starting its 8 turn cooldown. After 8 turns, she can cast the ability again.

Each champion has two ability slots but only one successful cast per turn. Casting starts an 8-turn-end cooldown for that slot. Spell effects scale with Focus as specified in the champion's kit.

Ability details such as the range, targeting system, and whether you can cast and move are detailed on each champion's individual page

#### Targets

- Unit spells need a valid visible target. Ground spells affect an area.
- Self-targeted spells use the self-cast action.

### Channels

Channels are actions that take several turns to complete, such as recall, tribute and teleport. Their timers tick down at your turn end, then execute at your next turn start when the timer is zero. The below table shows the channel actions in the game, and how and when they can be cancelled.

Channels count down at owner turn ends and execute at the next owner turn start once zero. Recall takes 5 countdown ticks (3 with Home) and is interrupted by damage, stun or death. Tribute takes ceil(gems / 10) owner turn ends, at least 1: damage does not stop it, but stun, death or a new altar lock does. Teleport crystals channel for 3 owner turn ends: damage does not stop them; enemy stuns and death do. Recall and tribute can be cancelled manually; teleport cannot.

## Items & gold

Items in the game are purchased on the spawn tiles (or while dead) using gold. You can purchase shards, increasing permanent stats for your champion based on their internal growth values:

### Shards and emblems

Items in the game are purchased on the spawn tiles (or while dead) using gold. You can purchase shards, increasing permanent stats for your champion based on their internal growth values:

- Strength, increasing your basic attack damage
- Health, increasing your maximum health
- Focus, increasing the power of your abilities

Shards cost 30 gold and after purchasing 5, combine into an emblem providing a unique bonus based on which shard type you purchased.

Try building an emblem below:

Shards cost 30 gold and add their champion's growth for the chosen stat. Five matching shards automatically combine into the corresponding emblem. Only the completed emblem grants its passive. Families have Strength, Health and Focus variants; use the family table for stat points and passives.

### Emblem families

Every family has Strength, Health and Focus versions. 5 shards automatically combine to an emblem (150 gold total). Shards give stats; only the finished emblem gives the passive.

| Family | Counts as | Passive |
| --- | --- | --- |
| Monster | 5 shards of the chosen stat | 10% more damage to camps. |
| Power | 6 shards of the chosen stat | None; one extra shard’s worth instead. |
| Durability | 5 shards of the chosen stat | 10% less incoming damage. |
| Treasure | 5 shards of the chosen stat | 20% more gold gained. |
| Jungle | 5 shards of the chosen stat | 30% less damage from camps. |
| Home | 5 shards of the chosen stat | 50% shorter recall: five turn ends become three, rounded up. |
| Bruiser | 5 of the chosen stat, 1 of each other | None; a spread of stats instead. |

### Consumables

The shop also has some consumable items, each costing 10 gold. You may carry only one of each at the same time, with the same purchase rules as shards/emblems.

10 gold each. Carry at most one of each. Buy them on your spawn or while dead.

| Tool | Use |
| --- | --- |
| Altar lock | Place on an altar within two tiles. Blocks enemy tributes until removed by three basic attacks. |
| Teleport crystal | Teleport crystal allows you to access otherwise unreachable areas by teleporting you 3 tiles, ignoring terrain. However, the teleport only activates after a channel of 3 turns. |

#### Extra item rules

- **Stats stack**: Every shard counts, including those inside duplicate emblems.
- **Emblem passives don’t**: A second emblem of a family adds no extra passive.
- **Gold bonuses add**: Treasure with Vesper Vale: 20% + 20% = 40%.
- **Spawn purchases heal**: Buying Health on your spawn heals the amount gained; Tavi’s field purchases do not.
- **Only Tavi can sell**: On spawn, for the full purchase value. Refunds are not earned gold.

## Vision & wards

Each champion sees its own patch of the map. Walls and bushes block the view.

### How vision is calculated

Vision in the game is raycast, you can try it with the interactive vision tool below. To be more precise, the system uses symmetric shadow casting based on Albert Ford's algorithm.

[Albert Ford: Symmetric Shadowcasting — explanation and interactive example](https://www.albertford.com/shadowcasting/)

### What you can see

Champions normally see up to 8 tiles using symmetric shadowcasting. Walls and bushes occlude vision; the connected bush cluster you stand in is transparent to your own vision. Your ward adds its vision; teammates' views are not shared.

- You see up to 8 tiles away in every direction, including diagonally, if nothing blocks the line of sight.
- Walls block sight. So do bushes: from outside, you cannot see into a bush or past it.
- Standing in a bush, you see across that whole bush and out of it, but not into other bushes.
- Holes cannot be stood on, but they do not block sight

### Wards

Each champion has a free ward, that can be placed to expand their personal vision. You may freely replace it, once per round, but note that you can/will only ever have one active.

- **One active ward**: A new ward replaces the old one.
- **Permanent**: Wards do not expire or take damage.

### Vision is not shared among allies

Vision per champion is only their own, plus their own ward. You cannot see what your ally champions see, nor their wards. However, if you spot something of note, you can use the shared memory to try to communicate the information to your team.

## Death, recall & recovery

Recall to heal safely. Die, and you lose your carried gems and sit out 60 rounds.

### When a champion dies

The killer receives all carried gems and 50 base gold; assists award no gold. Camp kills destroy carried gems. Dead champions retain shopping turns and respawn at their next turn start after 60 owner turn-end ticks, when a spawn tile is available, with full health and abilities reset.

When a champion dies they respawn on their spawn tiles after 60 rounds, awarding their killer all the gems they were carrying in addition to 50 gold.

If a camp kills you, the carried gems are lost.

Cassi and Issac die together, awarding the killer 50 gold, and all the gems that both of them were carrying.

### Recalling

Recalling is a channeled action taking 5 turns, that returns your champion to the spawn from anywhere on the map. Spawn tiles allow you to shop, and also will heal you to full health so recalling when you are low, or have a lot of gold is usually a good idea.

Recall takes 5 owner turn ends, or 3 with Home; the starting turn counts. Completion happens at the next owner turn start after zero. Damage or stun interrupts it. You retain carried gems. After arriving at turn start, remain on spawn until that turn ends to heal fully.

- Recall arrives at turn start. Stay on spawn until the end of that turn to heal to full.
- Any damage cancels it, and so does a stun. Start it somewhere safe.
- You keep your carried gems.

## Fuel

Bots have a computation limit enforced via Wasmtime fuel. Fuel roughly corresponds to CPU instructions, though it isn't perfectly 1:1. Using fuel rather than time means that execution is deterministic and machine-independent. Since some languages are more instruction efficient than others, there are different fuel limits for different languages.

Bots have a computation limit enforced via Wasmtime fuel. Fuel roughly corresponds to CPU instructions, though it isn't perfectly 1:1. Using fuel rather than time means that execution is deterministic and machine-independent. Since some languages are more instruction efficient than others, there are different fuel limits for different languages.

By default, a C++ or Rust bot gets 100k fuel per turn. OCaml gets 5 times the C++ limit, and Python gets 25 times the C++ limit.

When a bot runs out of fuel, its execution is suspended and the next bot in the turn order gets to go. The next round, this bot's turn will resume from the point of suspension and continues.

We provide bots with 10x fuel for the first turn of draft, to facilitate precomputation, and 2x fuel for the first turn of play, giving bots room to initialise and precompute. Unused fuel at the end of a turn is not added to the next turn's fuel budget.

## Shared memory

Your team’s two scratchpads: 1,024 bytes saved in the draft, and 8 bytes shared all match.

Each champion has its own bot instance, but there is a team memory buffer that is shared by your team, which cannot be read by the opponent.

Each team has 8 bytes of shared memory, that is readable and writable throughout the match.

In addition, there is the 1024 bytes of draft memory, that is writable during the draft phase, that becomes read-only for your champions during the match itself.

## Champion roster

### Choose a champion



### Compare starting stats

Before equipment and passives. Strength powers basic attacks, Focus powers spells and Health is your maximum health. Growth is what each matching shard adds.

### Shared by every champion

- **Vision 8**: Every champion sees 8 tiles around itself.
- **Walk 1**: One tile per turn, in any direction.
- **8-round cooldowns**: On both ability slots.
- **One cast per turn**: Shared by both slots. “Excludes walking” spells can’t share a turn with a walk.

## Cassi & Issac

One draft choice creates a ranged spellcaster and a melee fighter. They act separately, but if either dies, both die.

### Two bodies, one linked fate

Cassi and Issac have separate positions, bags, abilities and bot instances. Each receives its own turn.

**Shared passive**

If either Cassi or Issac dies, the other dies too. Both lose their carried gems and together grant one kill reward.

### Cassi · ranged

**Cassi**

#### Slot 0: Single-target damage

2 × Focus damage.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

#### Slot 1: Stun

One-round stun with no direct damage.

Target / range: Enemy or camp · 4. Walking: Compatible. Cooldown: 8 owner turn ends.

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 700 | 71.25 |
| Strength | 30 | 3.5 |
| Focus | 70 | 15 |

Attack type: Ranged. Basic attack range: 3 tiles.

### Issac · melee

**Issac**

#### Slot 0: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Dash

Move up to 2 steps, including diagonals.

Target / range: Ground · 2 tiles. Walking: Compatible. Cooldown: 8 owner turn ends.

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 900 | 95 |
| Strength | 40 | 4.75 |
| Focus | 100 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.

## Ryth

A melee champion with an area stun and an attack reset. Health also adds Strength.

### Kit

**Ryth**

**Passive:** Ryth gains Strength equal to 1% of bonus health.

#### Slot 0: Area stun

1 × Focus damage and a one-round stun in radius one.

Target / range: Ground · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

#### Slot 1: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 1200 | 100 |
| Strength | 50 | 5 |
| Focus | 100 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.

## Vesper Vale

A ranged champion with strong basic attacks, bonus gold and a short dash.

### Kit

**Vesper Vale**

**Passive:** Vesper Vale gains 20% more gold. A Treasure emblem adds another 20 percentage points, for a combined 40% bonus.

#### Slot 0: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Dash

Move up to 2 steps, including diagonals.

Target / range: Ground · 2 tiles. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 900 | 100 |
| Strength | 45 | 5 |
| Focus | 100 | 10 |

Attack type: Ranged. Basic attack range: 3 tiles.

## Prism

A ranged spellcaster who gains Focus from carried gems, trading safe tributes for temporary strength.

### Kit

**Prism**

**Passive:** Prism gains 0.2 Focus per carried gem. Ten carried gems add 2 Focus. Tributing or losing those gems removes that bonus.

#### Slot 0: Damage and stun

1.5 × Focus damage and a one-round stun.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

#### Slot 1: Area damage

1 × Focus damage in radius one.

Target / range: Ground · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 700 | 75 |
| Strength | 35 | 3.5 |
| Focus | 80 | 10 |

Attack type: Ranged. Basic attack range: 3 tiles.

## Stellar

A ranged spellcaster who chooses between concentrated damage and an area hit.

### Kit

**Stellar**

#### Slot 0: Single-target damage

2 × Focus damage.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

#### Slot 1: Area damage

1.4 × Focus damage in radius one.

Target / range: Ground · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 750 | 75 |
| Strength | 35 | 3.5 |
| Focus | 100 | 10 |

Attack type: Ranged. Basic attack range: 3 tiles.

## Vendetta

A melee attacker whose kills refresh spell cooldowns, with a reset and dash for follow-up plays.

### Kit

**Vendetta**

**Passive:** When Vendetta gets a kill, she resets all her ability cooldowns. This applies to killing camp monsters too.

#### Slot 0: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Dash

Move up to 2 steps, including diagonals.

Target / range: Ground · 2 tiles. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 800 | 100 |
| Strength | 70 | 5 |
| Focus | 100 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.

## Marcella

A melee champion immune to stuns, with a stunning enhanced attack and a ranged damage spell.

### Kit

**Marcella**

**Passive:** Immune to stuns

#### Slot 0: Enhanced attack reset

Resets your basic attack cooldown; the next enhanced attack stuns for one round.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Single-target damage

2 × Focus damage.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 1200 | 100 |
| Strength | 45 | 5 |
| Focus | 50 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.

## Tavi

A ranged champion who can buy equipment in the field and threaten a target with a powerful stunning spell.

### Kit

**Tavi**

**Passive:** Tavi can buy shards from anywhere on the map, note he cannot buy consumables unless he is on his spawn tiles (or dead). Additionally, Tavi can sell equipment at spawn for its full purchase value. Note, Health bought away from spawn raises maximum health without healing.

#### Slot 0: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Damage and stun

2.2 × Focus damage and a one-round stun.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 750 | 75 |
| Strength | 35 | 5 |
| Focus | 100 | 10 |

Attack type: Ranged. Basic attack range: 3 tiles.

## Willow

A ranged champion who can heal an ally or reset a basic attack, with modest stat growth.

### Kit

**Willow**

#### Slot 0: Ally heal

Restores up to 3 × Focus health to another allied champion.

Target / range: Ally · 4. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 700 | 50 |
| Strength | 45 | 3 |
| Focus | 105 | 5 |

Attack type: Ranged. Basic attack range: 3 tiles.

## Ren

A melee champion with retaliation damage, a targeted stun and a short dash.

### Kit

**Ren**

**Passive:** Retaliates against an attacker for 0.2 × Focus damage when damaged. Ren can still trigger retaliation as he dies.

#### Slot 0: Damage and stun

1.25 × Focus damage and a one-round stun.

Target / range: Enemy or camp · 4. Walking: Excludes walking. Cooldown: 8 owner turn ends.

#### Slot 1: Dash

Move up to 2 steps, including diagonals.

Target / range: Ground · 2 tiles. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 900 | 100 |
| Strength | 40 | 5 |
| Focus | 100 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.

## Varg

A melee champion with splash basic attacks, an attack reset and a temporary self-shield.

### Kit

**Varg**

**Passive:** Basic attacks splash damage equal to Strength onto other valid enemies and camps within radius one of the target. The primary target is not hit twice by the splash.

#### Slot 0: Attack reset

Resets your basic attack cooldown.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

#### Slot 1: Self-shield

Grants a 2.5 × Focus shield lasting five turns.

Target / range: Self. Walking: Compatible. Cooldown: 8 owner turn ends.

### Starting stats

| Stat | Base | Growth per matching shard |
| --- | --- | --- |
| Health | 1000 | 100 |
| Strength | 50 | 5 |
| Focus | 100 | 10 |

Attack type: Melee. Basic attack range: 1 tiles.
