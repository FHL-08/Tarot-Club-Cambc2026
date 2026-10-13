#!/usr/bin/env bash
# Create a clean DETACHED worktree at a pushed sha for independent verification.
# Usage: verify-worktree.sh <sha>  → prints worktree path (caller runs checks, then worktree-down.sh)
set -euo pipefail

sha="${1:?usage: verify-worktree.sh <sha>}"
root="$(git rev-parse --show-toplevel)"
wt_root="${CAMBC_WORKTREE_ROOT:-/var/tmp/cambc-worktrees-$(id -un)}"
dir="${wt_root}/cambc-verify-${sha:0:8}"
mkdir -p "$wt_root"

git fetch origin --quiet
if ! git cat-file -e "$sha^{commit}" 2>/dev/null; then
  echo "sha $sha not found locally after fetch" >&2
  exit 1
fi
git worktree add --detach "$dir" "$sha" >/dev/null

# Same engine-binary link as worktree-up.sh — verification runs real matches.
if [ -x "$root/cambc.exe" ]; then
  ln -sf "$root/cambc.exe" "$dir/cambc.exe"
fi

printf '%s\n' "$dir"
