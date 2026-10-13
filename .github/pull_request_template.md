## What changed

<!-- The problem in a sentence or two, then the approach. Keep scope tight. -->

## Evidence

<!-- For bot changes: build output + match statistics. Paste real numbers —
     e.g. `cambc run tarot starter-cpp --map expanse` → 312 v 40 gems. -->

## Factory

<!-- Required for factory-produced PRs — merge-gate.sh parses this block.
     Delete the section for hand-written PRs. -->

Issue: <!-- issue number -->
Files: <!-- comma-separated path prefixes, same as the brief -->
Verified: <!-- commands + results, one per line -->
Submit-safe: <!-- yes|no — required yes if the diff touches sdk/, maps/, cambc.toml, .github/, factory/ -->

## Checklist

- [ ] Small and focused — one concern
- [ ] `cambc build` is green
- [ ] Evidence is real commands with real output, not descriptions
- [ ] No committed replays/, matches/, binaries, or scratch files
