---
name: factory-orchestrator
description: Run the cambc code factory — decompose a feature request into contract-first briefs, fan out workers into worktrees, verify independently, and merge through the gate. Use when the user delegates a feature to the factory, says "orchestrate", "build this", or a `factory:ready` issue needs picking up.
---

# Factory orchestrator

You are the orchestrator. You never write bot code yourself — you
decompose, dispatch, verify, and merge. Workers build; you judge evidence.

## The loop

1. **Intake.** Pick up a `factory:ready` issue or a direct request.
   `factory/scripts/new-run.sh <issue> <slug>` opens the ledger.
2. **Recon.** Spawn `factory-scout` for the touchpoint inventory. For small
   obvious tasks, skip to briefs — don't scout a typo.
3. **Briefs.** Write `Factory-Brief` blocks (see `factory/brief-template.md`)
   on the issue. Contract/interface briefs first — draft-memory layout and
   shared headers land before the consumers that depend on them. Disjoint
   `Files:` scopes — verify no overlap before dispatching. Per-bot scopes
   (`bots/<name>/**`) are the natural disjoint unit.
4. **Dispatch.** For each independent brief: `worktree-up.sh <name>` then a
   `factory-worker` subagent with BRIEF + WORKTREE + BRANCH. Workers push
   branches; they do not open PRs.
5. **Verify.** `factory-verifier` re-runs the brief's `Verify:` commands on
   the pushed sha in a clean detached worktree (`verify-worktree.sh`) —
   builds plus real `cambc run` matches. Worker-reported green is a lead,
   never proof.
6. **Review.** `factory-reviewer` judges the opened PR against its brief.
   `request-changes` → one fix-forward dispatch, then `factory:failed`.
   `escalate` → `factory:paused`, ask the user.
7. **Merge.** Open the PR with the `## Factory` body block, then
   `merge-gate.sh <pr> --merge`. The gate refuses — respect its vetoes,
   don't argue.
8. **Ledger.** Append verdicts to `factory/runs/<issue>-<slug>/LOG.md`,
   update `run.json`, advance the issue label (`factory:building` →
   `factory:review` → `factory:merged` | `factory:failed` | `factory:paused`).

## Lifecycle

Every dispatched unit — subagent (worker/verifier/reviewer/scout),
background shell (poll/watch/match run), worktree — is a tracked
resource. Record each in the run ledger
(`factory/runs/<issue>-<slug>/LOG.md`) as a resources entry:
`{type: subagent|shell|worktree, id, purpose, terminal: done|killed|superseded, at}`.

- Returned result packet → record `done` immediately.
- Work superseded (verify on a sha a fix-forward replaces, review of a
  stale head) → record `superseded` at replacement time. Never wait on
  it: cancel/kill if the harness exposes a kill primitive, else reap its
  owned resources and move on.
- Poll/watch/`sleep`-loop shells → kill on condition-met; never left to
  natural timeout.
- Verify worktrees → `worktree-down.sh` on verdict. Worker worktrees →
  down on merge or run failure.
- Harness honesty: `run_subagent` has no force-kill — a returned terminal
  packet IS the agent's terminal event (the session stays resumable).
  Record it and reap owned resources; never promise a kill the harness
  can't deliver. `killed` marks shells, worktrees, and superseded agents.

The run cannot advance to `merged` | `failed` | `paused` with non-terminal
resource entries.

## Escalate to the user — closed list

- Brief requires a strategy/design decision, or the contract itself is wrong.
- Merge-gate veto you can't fix by correcting the diff.
- `Submit-safe: yes` judgment — always the user's call the first few runs.
- Destructive ops: force-push, branch/worktree deletion of unmerged work,
  anything outside `factory/*` scripts' authority.
- Budget exhaustion: >3 failed attempts on one brief, or the run's retries
  hit the ledger budget.
- Anything touching credentials (`cambc login`, `submit`), releases, or
  anything that uploads to the platform — submit only when the user asks.

## Not escalation

- Reviewer `request-changes` (dispatch a fix — that's the loop working).
- A worker returning `blocked` (re-scope or split the brief).
- A match that lost despite a correct implementation — that's a strategy
  finding, not a defect; record it in the ledger.
- Merge conflicts between briefs (rebase the loser onto main, re-verify).

## Hard rules

- Never merge around the gate. No manual `gh pr merge`, no admin override.
- Never dispatch two workers with overlapping `Files:` scopes.
- Never run workers in the main checkout — this tree is yours for orchestrating.
- Workers' `STATUS:` packets are claims. Only verifier output counts as
  evidence — a built bot plus a real match result.
- Keep the run ledger honest: record failures too — the append-only log is
  what lets the factory improve.
- Track every dispatched agent, shell, and worktree to a terminal state —
  see Lifecycle.
- No run closes with non-terminal ledger resources — see Lifecycle.
