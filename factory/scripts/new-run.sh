#!/usr/bin/env bash
# Open an append-only ledger for a factory run.
# Usage: new-run.sh <issue-number> <slug>  → creates factory/runs/<issue>-<slug>/
set -euo pipefail

issue="${1:?usage: new-run.sh <issue> <slug>}"
slug="${2:?usage: new-run.sh <issue> <slug>}"
root="$(git rev-parse --show-toplevel)"
run="$root/factory/runs/${issue}-${slug}"

mkdir -p "$run"
cat > "$run/run.json" <<EOF
{
  "issue": $issue,
  "slug": "$slug",
  "opened": "$(date -u +%FT%TZ)",
  "phase": "intake",
  "workers": [],
  "attempts": 0,
  "verdicts": [],
  "closed": null
}
EOF
printf '# Run %s — issue #%s\n\nappend-only; newest at bottom\n\n' "$slug" "$issue" > "$run/LOG.md"
printf '%s\t%s\t%s\n' "$(date -u +%FT%TZ)" "$issue" "$slug" >> "$root/factory/runs/index.log"

echo "$run"
