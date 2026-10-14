#!/usr/bin/env python3
"""Fetch public CAMBC platform match data: match groups (with official per-map statistics) and replay bundles.

usage:
  platform.py team <teamId> <outdir> [--pages N] [--no-replays]
  platform.py ladder <outdir>                      # ladder snapshot -> <outdir>/ladder.json
  platform.py find <name-substring>                # team ids by name
"""
import argparse, json, os, sys, time, urllib.parse, urllib.request

BASE = "https://2026a.battlecode.co.uk"


def get(path):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(BASE + "/api/v1" + path, timeout=30) as r:
                return json.load(r)
        except Exception:
            if attempt == 3:
                raise
            time.sleep(1.5 * (attempt + 1))


def team_groups(team, pages):
    groups, before = [], None
    for _ in range(pages):
        q = {"scope": "all", "team": team, **({"before": before} if before else {})}
        d = get("/match-groups?" + urllib.parse.urlencode(q))
        page = d.get("groups", [])
        groups += page
        if not d.get("more") or not page:
            break
        before = f"{page[-1]['createdAt']}_{page[-1]['id']}"
    return groups


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("team"); t.add_argument("team"); t.add_argument("out"); t.add_argument("--pages", type=int, default=200); t.add_argument("--no-replays", action="store_true")
    l = sub.add_parser("ladder"); l.add_argument("out")
    f = sub.add_parser("find"); f.add_argument("name")
    a = ap.parse_args()

    if a.cmd == "find":
        for t in get("/match-groups/options")["teams"]:
            if a.name.lower() in t["name"].lower():
                print(t["id"], t["name"])
        return
    if a.cmd == "ladder":
        os.makedirs(a.out, exist_ok=True)
        json.dump(get("/ladder"), open(os.path.join(a.out, "ladder.json"), "w"))
        print("ladder ->", os.path.join(a.out, "ladder.json"))
        return

    gdir, rdir = os.path.join(a.out, "groups"), os.path.join(a.out, "replays")
    os.makedirs(gdir, exist_ok=True); os.makedirs(rdir, exist_ok=True)
    groups = [g for g in team_groups(a.team, a.pages) if g.get("status") == "finished"]
    n_rep = 0
    for g in groups:
        p = os.path.join(gdir, g["id"] + ".json")
        if not os.path.exists(p):
            json.dump(get(f"/match-groups/{g['id']}"), open(p, "w"))
        if a.no_replays:
            continue
        for m in json.load(open(p))["matches"]:
            out = os.path.join(rdir, m["id"] + ".zip")
            if m.get("status") == "finished" and m.get("replayUrl") and not os.path.exists(out):
                urllib.request.urlretrieve(BASE + m["replayUrl"], out); n_rep += 1; time.sleep(0.1)
    print(f"{len(groups)} finished groups -> {gdir}; {n_rep} new replays -> {rdir}")


if __name__ == "__main__":
    sys.exit(main())
