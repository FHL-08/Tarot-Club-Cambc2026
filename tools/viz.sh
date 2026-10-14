#!/usr/bin/env bash
# Open a replay in the visualiser at a FIXED address, replacing whatever the
# visualiser was showing. The user keeps one tab on http://127.0.0.1:7026/ and
# just reloads it; every agent goes through this script so that address never
# dies or moves.
#
# usage: tools/viz.sh [ replay.repl26a | bundle.zip | <platform match id> | latest | last-platform [map] ]
#   (no argument = latest local replay under replays/)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
port="${CAMBC_VIEW_PORT:-7026}"
team="${CAMBC_TEAM_ID:-c38052e1-f3a0-44f8-a433-ef47a9d1181c}"
api="https://2026a.battlecode.co.uk/api/v1"
dest="$root/replays/viewer"
log="${TMPDIR:-/tmp}/cambc-view-$port.log"
mkdir -p "$dest"

die() { echo "viz: $*" >&2; exit 1; }

# Platform bundles and `cambc run -o` bundles are zips (the latter named .repl26a);
# the visualiser wants the bare replay, so unpack whenever the zip signature shows.
stage() {
  local src="$1" out="$dest/current.repl26a"
  [ -f "$src" ] || die "no such replay: $src"
  if [ "$(head -c 2 "$src")" = "PK" ]; then unzip -p "$src" replay.repl26a > "$out"
  else cp "$src" "$out"; fi
  [ -s "$out" ] || die "could not extract a replay from $src"
  echo "$out"
}

fetch_match() {
  local id="$1" zip="$dest/$1.zip"
  curl -sf "$api/matches/$id/bundle" -o "$zip" || die "platform bundle $id not found"
  stage "$zip"
}

last_platform() {
  local map="${1:-}"
  python3 - "$api" "$team" "$map" <<'EOF'
import json, sys, urllib.request
api, team, want = sys.argv[1], sys.argv[2], sys.argv[3]
get = lambda p: json.load(urllib.request.urlopen(api + p, timeout=30))
for g in get(f"/match-groups?scope=all&team={team}")["groups"]:
    if g.get("status") != "finished":
        continue
    for m in sorted(get(f"/match-groups/{g['id']}")["matches"], key=lambda m: m["createdAt"], reverse=True):
        if m.get("status") == "finished" and (not want or m["map"] == want):
            print(m["id"]); sys.exit(0)
sys.exit(1)
EOF
}

arg="${1:-latest}"
case "$arg" in
  latest)
    src="$(ls -t "$root"/replays/*.repl26a "$root"/replays/*/*.repl26a 2>/dev/null | grep -v '/viewer/' | head -1)" \
      || true
    [ -n "$src" ] || die "no local replays under replays/"
    replay="$(stage "$src")" ;;
  last-platform)
    id="$(last_platform "${2:-}")" || die "no finished platform match found${2:+ on ${2}}"
    echo "viz: platform match $id"
    replay="$(fetch_match "$id")" ;;
  *.repl26a|*.zip) replay="$(stage "$arg")" ;;
  *) [[ "$arg" =~ ^[0-9a-f-]{36}$ ]] || die "unrecognised argument: $arg"
     replay="$(fetch_match "$arg")" ;;
esac

# Stop the current visualiser on this port — only after confirming the
# listener really is `cambc.exe view`; anything else on the port is refused,
# never killed (killing by name/pattern once took the user's server down).
pid="$(ss -H -ltnp "sport = :$port" 2>/dev/null | grep -oE 'pid=[0-9]+' | head -1 | cut -d= -f2 || true)"
if [ -n "$pid" ]; then
  cmd="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
  [[ "$cmd" == *cambc.exe*view* ]] || die "port $port is held by something else (pid $pid: $cmd) — not touching it"
  kill "$pid"
  for _ in $(seq 1 40); do ss -H -ltn "sport = :$port" | grep -q . || break; sleep 0.25; done
  ss -H -ltn "sport = :$port" | grep -q . && die "old visualiser (pid $pid) did not release port $port"
fi

# setsid: the server must outlive the shell that launched it — a child left in
# the caller's session dies with that session, which is what kept leaving the
# user's tab on "Unable to connect".
cd "$root"
# cambc view would open its own tab immediately (before it is serving) and
# again on every restart; suppress it and open one tab ourselves once live.
BROWSER=true setsid nohup ./cambc.exe view --port "$port" "$replay" > "$log" 2>&1 < /dev/null &
for _ in $(seq 1 40); do
  code="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/" || true)"
  [ "$code" = 200 ] && break
  sleep 0.25
done
[ "${code:-}" = 200 ] || die "visualiser did not come up on :$port (see $log)"
url="$(grep -oE 'http://127\.0\.0\.1:[0-9]+/[^ ]*' "$log" | head -1)"
url="${url:-http://127.0.0.1:$port/}"
echo "viz: serving $(basename "$replay") — $url"
if [ -z "${VIZ_NO_BROWSER:-}" ]; then
  setsid xdg-open "$url" >/dev/null 2>&1 < /dev/null || echo "viz: could not open a browser — open $url yourself"
fi
echo "viz: opened a tab on $url (VIZ_NO_BROWSER=1 to skip); older 7026 tabs just need a reload"
