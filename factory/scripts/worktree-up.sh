#!/usr/bin/env bash
# Create a worker worktree for a factory task.
# Usage: worktree-up.sh <name> [base-ref]   → $CAMBC_WORKTREE_ROOT/cambc-<name>
# (default /var/tmp/cambc-worktrees-$USER) on branch factory/<name>.
# Worktrees live outside the home tree on purpose: agent file reads in a worktree
# open IDE documents, and worktrees inside the IDE's watched scope leave stale
# save-conflict tabs once torn down. /var/tmp sits outside that scope and
# survives reboots; override with CAMBC_WORKTREE_ROOT if needed.
set -euo pipefail

name="${1:?usage: worktree-up.sh <name> [base-ref]}"
base="${2:-origin/main}"
root="$(git rev-parse --show-toplevel)"
wt_root="${CAMBC_WORKTREE_ROOT:-/var/tmp/cambc-worktrees-$(id -un)}"
dir="${wt_root}/cambc-${name}"
branch="factory/${name}"
mkdir -p "$wt_root"

git fetch origin main --quiet
git worktree add "$dir" -b "$branch" "$base" >/dev/null

# The engine binary is gitignored — a fresh worktree has no cambc.exe.
# Link the main checkout's binary so build/run work inside the worktree.
if [ -x "$root/cambc.exe" ]; then
  ln -sf "$root/cambc.exe" "$dir/cambc.exe"
fi

printf 'worktree: %s\nbranch:   %s\nbase:    %s\n' "$dir" "$branch" "$base"
