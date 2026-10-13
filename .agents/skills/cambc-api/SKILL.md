---
name: cambc-api
description: Navigate the cambc C++ SDK — look up signatures and semantics in docs/BotAPICpp.md and sdk/cpp/cambc.h before writing or reviewing bot code. Use for any API question, signature lookup, or "does the SDK give me X" check.
---

# cambc API

Two sources, checked in this order:

1. `sdk/cpp/cambc.h` — the header that compiles. Signature truth.
2. `docs/BotAPICpp.md` — the exported reference, organised as
   `## cambc::<type>` sections with `### <method>` entries: signature,
   description, inputs, and a usage fragment.

If the docs-MCP server is connected, `api_lookup <symbol>` returns the
section directly; otherwise `grep -n "^## \|^### " docs/BotAPICpp.md` to
find the section, then read it.

## Map of the API surface

- `cambc` (top level): `champions`, `end_turn`, `fuel_remaining`,
  `global_submitted_gems`, `is_draft`, `opponent`, `resign`,
  `round_number`, `self`, `to_string` helpers.
- `cambc::champion` — the unit handle you drive: position, stats, items,
  `move_towards`, attack/ability/ward/recall/tribute/shop actions,
  `set_priority`.
- `cambc::ability` + `ability_*` payloads — `use` variants per targeting
  mode, `is_*` kind checks, `damage_amount`/`heal_amount`/`shield_amount`,
  `valid_targets`/`valid_tiles`, `mask_into`, `cooldown`/`is_ready`.
- `cambc::map` — terrain queries (`walls`, `holes`, `altars`,
  `spawn_region`), `camp_group` for camp bookkeeping.
- `cambc::vision` — visibility masks (`champion_tiles`, `camp_tiles`,
  `teleport_tiles`), shadowcast helpers.
- `cambc::bitboard` — tile-set operations (`draw_chebyshev`, `positions`,
  unions/intersections); movement fields seed from it.
- `cambc::distance_field` — gradient pathing toward goal bitboards (see
  `bots/starter-cpp` for the canonical usage).
- `cambc::draft`, `draft_memory_buffer` (1024 B, write in draft, read in
  match), `shared_memory_buffer` (8 B, read/write all match).
- `cambc::item_type`, `item_info`, `consumable*`, `altar_lock`,
  `teleport`, `ward`.
- `cambc::camp`, `cambc::unit`, `cambc::vec2` (Chebyshev distance),
  `cambc::enum_map`, `cambc::champion_state`, `cambc::buff_*` (stun,
  shield, restrictions, enhanced attack, `buff_collection`).
- `cambc::error` — thrown for illegal actions on KNOWN-illegal inputs;
  hidden-occupant blocks return `false` instead.
- `cambc::debug` — logging/drawing into the replay.

## Rules of engagement

- Never invent a signature. If the header and doc disagree, the header
  wins; flag the discrepancy.
- Check preconditions before calling actions (vision for targets, range,
  allowance, cooldown). Illegal-on-known-state throws; action results
  return `false` for hidden blockers.
- `bots/starter-cpp` is a working reference — `bot.cpp` shows movement
  masks + distance fields, `draft.cpp` shows drafting.
- Watch fuel: `fuel_remaining()` + `end_turn()` when low. Expensive scans
  belong in the draft (10x fuel) or on a distance field, not per-turn.
