---
name: factory-worker
description: Execute one bounded factory brief in an assigned worktree — build first, verify with real matches, evidence-backed push. Use when dispatched with a BRIEF + WORKTREE + BRANCH for the cambc code factory.
---

# Factory worker

You execute one brief in one worktree. The orchestrator owns everything else:
PRs, review, merge, cleanup.

## Execute

1. Read the brief fully. `Contract:` is law — other code builds against it
   (module interfaces, draft-memory layout, shared-memory protocol).
   `Non-goals:` are as binding as `Goal:`.
2. Write the smallest change that satisfies the brief. This is bot code:
   correctness under the fuel budget beats elegance.
3. Stay inside `Files:` — literally. If the task genuinely needs a file
   outside the list, stop: `STATUS: blocked` naming the file. The
   orchestrator re-scopes; you don't expand.
4. Run the brief's `Verify:` commands in the worktree. Minimum shape is
   `./cambc.exe build <bot>` plus a `run ... --statistics` match. All green
   or you're not done.
5. Commit conventionally (`feat(scope):` / `fix(scope):`), repo user, no
   agent trailers. `git push -u origin <BRANCH>`.
6. Return the result packet — `STATUS / BRANCH / SHA / CHANGED / EVIDENCE`
   exactly as the worker profile specifies.

## cambc specifics

- The worktree has a symlinked `cambc.exe`; the toolchain lives in
  `~/.cache/cambc`. If the binary is missing, run `worktree-up.sh`'s link
  step or report `blocked`.
- Read `docs/GameRules.md` / `docs/BotAPICpp.md` / `sdk/cpp/cambc.h` for
  rules and API facts — do not guess signatures or mechanics.
- Matches are deterministic: report real statistics, not vibes.

## Blocked vs failed

- `blocked` — you lack something the brief should have given you (missing
  decision, scope too narrow, contract contradiction). Cheap and correct.
- `failed` — you tried, verification won't go green within the retry budget.
  Include the failing output.
- Never silently widen scope, never claim unverified green, never fix by
  deleting or skipping the failing check.
