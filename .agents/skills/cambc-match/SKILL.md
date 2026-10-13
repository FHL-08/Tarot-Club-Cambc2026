---
name: cambc-match
description: Build bots, run matches, batch manifests, read statistics JSON, and open replays with the cambc CLI. Use for any match execution, verification run, or replay inspection.
---

# cambc matches

The engine is `./cambc.exe` at repo root (gitignored local binary). The
toolchain (WASI SDK + cmake + ninja) lives in `~/.cache/cambc`, installed
by `./cambc.exe init`. In a factory worktree, `cambc.exe` is a symlink to
the main checkout's binary — `worktree-up.sh` sets it.

## Build

```bash
./cambc.exe build <bot>            # bots/<bot>/ → WASI module
./cambc.exe build <bot> --output <path>   # stable module path for submit
```

## Run

```bash
./cambc.exe run <bot_a> <bot_b> \
  --map <name|path> \               # bare name resolves under maps/
  --rounds <N> \                    # optional cap
  --fuel <quantum> \                # override fuel/turn
  -o replays/<dir> \                # replay bundle dir (default replays/)
  --statistics <out.json>           # final stats JSON (needs completed game)
```

- Matches are deterministic in fuel — same inputs, same result. A match
  outcome is reproducible evidence; record the exact command + stats.
- `--statistics` needs a COMPLETED game. A `--rounds` cap that truncates
  the match errors with "Statistics require a game outcome". For behavior
  verification, run to the end (a full mirror match is ~1s) or assert on
  replay/stdout, not stats JSON.
- Read the stats JSON for verdicts: `submittedGems`, `earnedGold`,
  `collectedGems`, `kills`, `deaths`, `botFaults`, per-champion
  damage/healing/items. Paste real numbers into `Verified:` evidence,
  never "won the match".
- `--view` opens the visualiser — humans only; agents use `--statistics`.

## Batch

```bash
./cambc.exe run-batch <manifest>    # four-column text manifest of matches
```

Use for sweep verification: the same bot across several maps, or A/B
against multiple baselines. Write the manifest under `/tmp` — never
commit match manifests or outputs (`matches/`, `replays/` are gitignored).

## Project plumbing

- `cambc.toml` pins `sdk_version` + enabled languages — refreshing SDK is
  `./cambc.exe init` (safe to re-run; it refreshes sdk/, maps/, toolchain).
- `./cambc.exe new <name> --lang cpp` scaffolds from `sdk/templates/cpp`.
- `./cambc.exe maps` refreshes the published map pool into `maps/`.
- `./cambc.exe bundle|submit` touches the platform account — only when
  the user asks, never inside a factory run.
