# Factory brief template

One brief = one GitHub issue comment (or one issue for a single-brief task).
The `Factory-Brief` fenced block is what merge-gate.sh parses — keep it exact.

```text
Factory-Brief
Goal: <one sentence — observable end state>
Contract: <interfaces other work builds against — file formats, module APIs,
  draft-memory layout, shared-memory protocol; "none" if leaf>
Files: <comma-separated path prefixes, e.g. bots/tarot/src/combat.*, bots/tarot/src/pathing/**>
Verify: <commands the worker must run green, e.g.
  ./cambc.exe build tarot && ./cambc.exe run tarot starter-cpp --map expanse --statistics /tmp/s.json>
Non-goals: <explicit exclusions>
Depends-on: <issue numbers or "none">
```

Rules the orchestrator enforces when writing briefs:

- `Files:` scopes are disjoint across parallel briefs. Two workers, one file =
  merge conflict, not parallelism. Per-bot scopes (`bots/<name>/**`) are the
  natural disjoint unit; shared headers and draft-memory layout are contracts.
- `Contract:` is written before any worker starts — it is the shared spec
  everything builds against (contracts land first). For cambc this is usually
  a module interface, the draft-memory format, or the shared-memory protocol.
- `Verify:` is real commands, not descriptions. Minimum bar is `build` plus at
  least one `run --statistics`; if the repo can't express the check, the brief
  isn't ready.
- Leaf work first, integration second: contract/module briefs merge before
  the consumer briefs that `Depends-on` them.
- One brief ≈ one sitting. If it fans out again, the brief is too big — split.

## Factory PR block

Worker PRs end their body with this block (merge-gate parses it):

```markdown
## Factory

Issue: <issue number>
Files: <same prefixes as the brief>
Verified: <commands + results, e.g. `cambc build tarot` → ok;
  `cambc run tarot starter-cpp --map expanse` → 312 gems tributed>
Submit-safe: <yes|no — required yes if the diff touches sdk/, maps/,
  cambc.toml, .github/, or factory/ paths>
```
