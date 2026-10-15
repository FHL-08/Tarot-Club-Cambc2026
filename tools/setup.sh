#!/usr/bin/env bash
# Fresh-machine setup: checksum-verified cambc.exe + WASI toolchain + smoke match.
# Idempotent; safe to re-run after a cambc release. No secrets, no login.
set -euo pipefail

cd "$(dirname "$0")/.."

command -v python3 >/dev/null || { echo "error: python3 is required (replay tools + version parsing)"; exit 1; }
if ! command -v node >/dev/null || [ "$(node --version | sed 's/^v//; s/\..*//')" -lt 20 ]; then
    echo "error: node >= 20 is required (tools/replay uses node:wasi); got: $(node --version 2>/dev/null || echo none)"
    exit 1
fi

case "$(uname -m)" in
    x86_64|amd64) platform=linux-x64 ;;
    aarch64|arm64) platform=linux-arm64 ;;
    *) echo "error: unsupported architecture $(uname -m)"; exit 1 ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 - "$platform" > "$tmp/dist.env" <<'PY'
import json, sys, urllib.request
platform = sys.argv[1]
with urllib.request.urlopen("https://2026a.battlecode.co.uk/api/v1/distribution") as r:
    dist = json.load(r)
for p in dist["platforms"]:
    if p["platform"] == platform:
        pub = p["published"]
        if pub is None:
            sys.exit(f"error: no published cambc build for {platform}")
        for k in ("version", "sha256", "downloadUrl"):
            print(f"{k.upper()}={pub[k]}")
        break
PY
version=$(grep '^VERSION=' "$tmp/dist.env" | cut -d= -f2)
sha=$(grep '^SHA256=' "$tmp/dist.env" | cut -d= -f2)
url=$(grep '^DOWNLOADURL=' "$tmp/dist.env" | cut -d= -f2)

current=$(./cambc.exe --version 2>/dev/null | awk '{print $NF}' || true)
if [ "$current" = "$version" ]; then
    echo "cambc.exe already at $version — skipping download"
else
    echo "downloading cambc $version for $platform"
    curl -fsSL "$url" -o "$tmp/cambc.exe"
    echo "$sha  $tmp/cambc.exe" | sha256sum -c -
    chmod +x "$tmp/cambc.exe"
    mv "$tmp/cambc.exe" ./cambc.exe
    echo "installed cambc.exe $version"
fi

sdk_version=$(grep '^sdk_version' cambc.toml | sed 's/.*"\(.*\)"/\1/')
if [ "$sdk_version" != "$version" ]; then
    echo "warning: cambc.toml sdk_version=$sdk_version differs from engine $version — refresh vendored sdk/ via PR"
fi

# Toolchain lives in ~/.cache/cambc (init takes a scratch dir so tracked
# sdk/ and maps/ are untouched). Skip when it can already build.
if ls ~/.cache/cambc/wasi-sdk-* >/dev/null 2>&1 && ./cambc.exe build starter-cpp >/dev/null 2>&1; then
    echo "toolchain present and starter-cpp builds — skipping init"
else
    echo "installing toolchain via cambc init"
    ./cambc.exe init --yes "$tmp/init"
fi

./cambc.exe build foundry
./cambc.exe build starter-cpp
./cambc.exe run foundry starter-cpp --map expanse --statistics "$tmp/smoke.json"
python3 - "$tmp/smoke.json" <<'PY'
import json, sys
s = json.load(open(sys.argv[1]))
gems = [t["submittedGems"] for t in s["teams"]]
print("smoke: gems", gems, "botFaults", s["botFaults"], "outcome", s["outcome"])
if s["botFaults"] != [0, 0]:
    sys.exit("error: smoke match had bot faults")
PY

echo "cambc submit needs a platform login: run ./cambc.exe login once (7-day session)."
