#!/usr/bin/env bash
# Remove a factory worktree and its branch. Refuses to drop unmerged work unless --force.
# Usage: worktree-down.sh <name|path> [--force]
set -euo pipefail

target="${1:?usage: worktree-down.sh <name|path> [--force]}"
force="${2:-}"

wt_root="${CAMBC_WORKTREE_ROOT:-/var/tmp/cambc-worktrees-$(id -un)}"
if [ -d "$target" ]; then dir="$target"
else dir="${wt_root}/cambc-${target}"; fi
branch="$(git -C "$dir" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"

git worktree remove "$dir" ${force:+--force}

case "$branch" in
  factory/*)
    if [ -n "$force" ]; then git branch -D "$branch" >/dev/null
    else git branch -d "$branch" >/dev/null 2>&1 || echo "kept unmerged branch $branch (use --force to drop)" >&2
    fi ;;
esac
echo "removed $dir"
