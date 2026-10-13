# Tarot Club — CAMBC 2026

This repo holds Tarot Club's bots for `cambc2026a` (Cambridge Battle Code). Bots
are written in C++, compiled to WASI modules by the `cambc` toolchain, and fight
matches against other bots on the competition maps.

Think of this repo less like an app and more like a racing stable: the product is
a set of champion bots that must keep winning under a deterministic engine.

## A small glossary

Use this language when talking about the game and the repo:

- **you** — the agent reading this file and changing this repo.
- **we, the team** — Tarot Club, the people directing you.
- **bot** — a WASI program under `bots/<name>/` controlling a team of champions.
- **champion** — one combat unit a bot controls (drafted before the match).
- **camp** — a cardinally-connected group of monsters that drops gold and gems.
- **gem** — the objective resource; tributed at altars for score.
- **gold** — spent on spawn tiles (or while dead) for shards, emblems, consumables.
- **altar** — map feature where carried gems are tributed via a channel.
- **tribute** — the channel that converts carried gems into permanent team score.
- **ward** — a free per-champion vision source, placeable within 2 tiles.
- **draft** — the pre-match phase where champions are picked and a 1024-byte
  plan is written to draft memory.
- **fuel** — the deterministic compute budget per turn (Wasmtime fuel). 100k per
  turn for C++, 10x on the first draft turn, 2x on each champion's first turn.
- **the docs** — `docs/GameRules.md` (rules + champion roster) and
  `docs/BotAPICpp.md` (the C++ SDK reference). Answer questions from them, not
  from memory.

## Repo layout

- `bots/<name>/` — one directory per bot. `bots/starter-cpp` is the engine's
  reference scaffold and the default sparring partner; keep it buildable.
- `sdk/` — vendored cambc SDK (headers, language bindings, templates). Refreshed
  by `cambc init`; treat it as upstream — do not hand-edit.
- `maps/` — the published competition map pool (9 `.map26a` files).
- `docs/` — the exported game handbook. Facts of record for rules and API.
- `factory/` — the code factory: brief template, scripts, local run ledgers.
- `tools/docs-mcp/` — stdio MCP server that answers rules/API lookups over docs/.
- `cambc.toml` — project config (sdk version, enabled languages).
- `cambc.exe` — the engine CLI. Gitignored: it is a 90MB local binary, not source.

## Commands that matter

The toolchain (WASI SDK, cmake, ninja) lives in `~/.cache/cambc` — installed by
`./cambc.exe init`, refreshed by re-running it.

- `./cambc.exe new <name> --lang cpp` — scaffold a bot into `bots/<name>/`.
- `./cambc.exe build <name>` — compile the bot to a WASI module.
- `./cambc.exe run <bot_a> <bot_b> --map <name> --statistics <out.json>` —
  run a match; `--statistics` exports final stats JSON. `--rounds N` caps the
  match; `-o` sets the replay dir.
- `./cambc.exe run-batch` — run a manifest of matches.
- `./cambc.exe view` — open the latest replay in the visualiser.
- `./cambc.exe bundle <bot>` / `submit <bot>` — package / upload a submission.
- `./cambc.exe maps` — refresh the map pool.

Matches are **deterministic in fuel, not wall-clock**. Same bots, same map, same
seed behavior — a match result is reproducible evidence. That is what makes the
factory's verification model work.

## Conventions

- C++ only for shipped bots. Other language starters exist for reference.
- One concern per PR. If a change description says "also", split it.
- Conventional commits: `feat(draft): ...`, `fix(combat): ...`, `refactor(pathing): ...`.
- Commit as the repo's configured user. No agent/tool trailers.
- Never commit `replays/`, `matches/`, build dirs, or `cambc.exe`.
- Facts of record: rules → `docs/GameRules.md`, API → `docs/BotAPICpp.md`,
  SDK → `sdk/cpp/cambc.h`. When docs and code disagree, the SDK wins; note the
  discrepancy in the PR.

## Verifying

The smallest proof that a bot change works is a build plus a completed
match (a full mirror match is ~1s):

```bash
./cambc.exe build <bot>
./cambc.exe run <bot> starter-cpp --map expanse --statistics /tmp/stats.json
```

`--statistics` only works on finished games — a `--rounds` cap that
truncates the match yields no stats. Run to completion for evidence.

- Bot logic changes ship with a match result that demonstrates the behavior
  (e.g. gems tributed, camps cleared, survival rate vs `starter-cpp`).
- A change that can't build is failed, not blocked. A change that builds but
  can't show a match is unfinished, not done.
- Do not claim verification you didn't run. `--statistics` output is the
  evidence; paste the real numbers.

## Pull requests

- Never open a PR unless asked (or dispatched by the factory).
- Body: the problem in a sentence or two, then the approach. Factory PRs end
  with the `## Factory` block — see `factory/brief-template.md`.
- One concern per PR. Small diffs beat clever ones.

## Factory mode

This repo is built by an orchestrated fleet of agents ("the factory"). The
operating contract lives in `.agents/skills/factory-orchestrator` and
`.agents/skills/factory-worker`; scripts in `factory/scripts/`; agent profiles
in `.devin/agents/`.

- Work is decomposed into briefs (`factory/brief-template.md`): contract-first,
  disjoint `Files:` scopes, real `Verify:` commands. Briefs live on GitHub
  issues with `factory:*` labels, never in the repo.
- Workers run in separate git worktrees created by `worktree-up.sh` — never in
  the main checkout concurrently. They push branches; the orchestrator opens
  PRs.
- Nothing reaches `main` except through `merge-gate.sh`: a `## Factory` block,
  diff inside brief scope, `Verified:` evidence present, submission-sensitive
  paths (`sdk/`, `maps/`, `cambc.toml`, `.github/`, `factory/`) vetoed without
  `Submit-safe: yes`.
- Verification is local. There is no engine in CI — the verifier subagent
  builds bots and runs matches in a clean detached worktree and reports real
  results. A green PR without verifier output does not merge.
