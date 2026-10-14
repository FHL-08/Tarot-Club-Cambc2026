#!/usr/bin/env bash
# Pull the official visualiser engine (WASM) and its snapshot-decoder module from a
# local `cambc.exe view` server into tools/replay/.cache. Nothing third-party is committed:
# the cache is rebuilt from whatever cambc version is installed.
# usage: tools/replay/assets.sh <any .repl26a replay>
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cache="$here/.cache"
replay="${1:?usage: assets.sh <replay.repl26a>}"
port="${CAMBC_VIEW_PORT:-7099}"
mkdir -p "$cache"

"$root/cambc.exe" view --port "$port" "$replay" >"$cache/view.log" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
for _ in $(seq 1 40); do curl -sf "http://127.0.0.1:$port/" -o "$cache/index.html" && break; sleep 0.25; done
[ -s "$cache/index.html" ] || { echo "cambc view did not come up on :$port" >&2; exit 1; }

base="http://127.0.0.1:$port/assets"
fetch() { curl -sf "$base/$1" -o "$cache/$1"; }
for js in $(grep -oE 'assets/(PanelChrome|rolldown-runtime|index)-[A-Za-z0-9_-]+\.js' "$cache/index.html" | sed 's#assets/##' | sort -u); do
  fetch "$js"
done
wasm="$(grep -ohE 'v0_0_1-[A-Za-z0-9_-]+\.wasm' "$cache"/index-*.js | head -1)"
[ -n "$wasm" ] || { echo "engine wasm name not found in visualiser bundle" >&2; exit 1; }
fetch "$wasm"
ls "$cache"/PanelChrome-*.js "$cache"/rolldown-runtime-*.js "$cache/$wasm" >/dev/null
echo "visualiser assets cached in $cache"
