#!/usr/bin/env bash
# Factory merge gate. Refuses — never advises.
# Usage: merge-gate.sh <pr-number> [--merge]
#
# PASS requires ALL of:
#   - a "## Factory" block in the PR body with Issue / Files / Verified / Submit-safe
#   - every changed file inside the brief's declared Files: patterns
#   - submission-sensitive diffs carry "Submit-safe: yes"
#   - no failing GitHub check-runs (informational only — verification is local;
#     the factory-verifier's match results are the real gate)
# --merge takes the run lock and enables auto-merge only after PASS.
set -euo pipefail

pr="${1:?usage: merge-gate.sh <pr-number> [--merge]}"
merge="${2:-}"
root="$(git rev-parse --show-toplevel)"

sensitive="sdk/ maps/ cambc.toml .github/ factory/"

veto() { echo "VETO: $*" >&2; exit 1; }

# --- 1. Factory block ---------------------------------------------------
body="$(gh pr view "$pr" --json body --jq '.body')"
field() { printf '%s\n' "$body" | grep -oP "^[[:space:]]*$1:[[:space:]]*\K.*" | head -1 | xargs; }

issue="$(field 'Issue')"     || issue=""
patterns="$(field 'Files')"  || patterns=""
verified="$(field 'Verified')" || verified=""
submit="$(field 'Submit-safe')" || submit=""

printf '%s\n' "$body" | grep -q '^## Factory' || veto "PR #$pr has no '## Factory' block"
[ -n "$issue" ]    || veto "Factory block missing Issue:"
[ -n "$patterns" ] || veto "Factory block missing Files:"
[ -n "$verified" ] || veto "Factory block missing Verified: (worker/verifier evidence required)"
[ -n "$submit" ]   || veto "Factory block missing Submit-safe:"

# --- 2. Reported checks (best-effort) ------------------------------------
# cambc verification is local; but if any check-runs exist on the head they
# must not be failing.
checks="$(gh pr checks "$pr" --json name,state --jq '.[] | "\(.name)\t\( .state)"' 2>/dev/null || true)"
if [ -n "$checks" ]; then
  while IFS=$'\t' read -r name state; do
    case "$state" in
      SUCCESS|NEUTRAL|SKIPPED) ;;
      PENDING|QUEUED|IN_PROGRESS|WAITING) veto "check '$name' still $state" ;;
      *) veto "check '$name' is $state" ;;
    esac
  done <<< "$checks"
fi

# --- 3. File scope ------------------------------------------------------
matches() { # <file> <patterns, comma-separated>
  local f="$1" p base
  IFS=',' read -ra ps <<< "$2"
  for p in "${ps[@]}"; do
    p="$(xargs <<< "$p")"
    case "$p" in
      */**) base="${p%\*\*}"; [[ "$f" == "$base"* ]] && return 0 ;;
      *) [[ "$f" == "$p" ]] && return 0 ;;
    esac
  done
  return 1
}

changed="$(gh pr diff "$pr" --name-only)"
[ -n "$changed" ] || veto "PR #$pr has no diff"
outside=""
while IFS= read -r f; do
  matches "$f" "$patterns" || outside="$outside  $f\n"
done <<< "$changed"
[ -z "$outside" ] || veto "files outside brief scope:\n$outside"

# --- 4. Submission-safety veto -------------------------------------------
hit=""
while IFS= read -r f; do
  for s in $sensitive; do [[ "$f" == "$s"* ]] && hit="$hit  $f\n"; done
done <<< "$changed"
if [ -n "$hit" ] && [ "$submit" != "yes" ]; then
  veto "submission-sensitive paths touched without 'Submit-safe: yes':\n$hit"
fi

# --- 5. Merge ------------------------------------------------------------
echo "PASS: PR #$pr (issue #$issue)"
if [ "$merge" = "--merge" ]; then
  mkdir -p "$root/factory/runs"
  exec 9>"$root/factory/runs/.merge.lock"
  flock -w 120 9 || veto "merge lock busy — another merge in flight"
  gh pr merge "$pr" --auto --squash
  echo "auto-merge enabled for PR #$pr"
fi
