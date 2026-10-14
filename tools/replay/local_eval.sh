#!/usr/bin/env bash
# Run <bot> vs <opp> on each map from BOTH sides (team B moves second each round and wins
# measurably more on the platform, so one-sided runs are biased), decode the replays, and
# print the same behaviour metrics used for platform scouting.
# usage: tools/replay/local_eval.sh <bot> <opp> [map ...]   (default: all maps in maps/)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bot="${1:?usage: local_eval.sh <bot> <opp> [map ...]}"; opp="${2:?}"; shift 2
maps=("$@")
[ ${#maps[@]} -gt 0 ] || maps=($(ls "$root/maps" | sed -n 's/\.map26a$//p'))
work="$(mktemp -d "${TMPDIR:-/tmp}/cambc-eval-XXXX")"
cd "$root"
./cambc.exe build "$bot" >/dev/null
./cambc.exe build "$opp" >/dev/null
for m in "${maps[@]}"; do
  for side in 0 1; do
    if [ "$side" = 0 ]; then a="$bot"; b="$opp"; else a="$opp"; b="$bot"; fi
    mkdir -p "$work/raw$side/$m"
    ./cambc.exe run "$a" "$b" --map "$m" -o "$work/raw$side/$m" >/dev/null 2>&1
    mkdir -p "$work/rep$side"
    cp "$(ls -t "$work/raw$side/$m"/*.repl26a | head -1)" "$work/rep$side/$m.repl26a"
  done
done
for side in 0 1; do
  node "$here/extract.mjs" "$work/rep$side" "$work/traj$side" 2>/dev/null | tail -1
done
echo "### $bot as team A"; python3 "$here/report.py" "$work/traj0" --side 0
echo "### $bot as team B"; python3 "$here/report.py" "$work/traj1" --side 1
echo "trajectories: $work/traj0 $work/traj1"
