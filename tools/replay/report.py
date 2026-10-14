#!/usr/bin/env python3
"""Behaviour metrics from extracted trajectories, ours vs opponent, split by result.

usage:
  report.py <trajdir> --side 0|1                      # local runs: our bot's team index (bot_a = 0)
  report.py <trajdir> --groups <dir> --team <teamId>  # platform data from platform.py; adds W/L split
  report.py <trajdir> --groups <dir> --team <teamId> --check   # decoded vs official statistics
"""
import argparse, glob, json, math, os, sys
from collections import defaultdict

CTYPE = ["ryth", "vesper", "vendetta", "cassi", "issac", "stellar", "prism", "marcella", "willow", "ren", "varg", "tavi"]
MOVE, ATTACK, CAST, YIELD, CHANNEL, SHOP = 4, 5, 6, 9, 11, 13
METRICS = ["firstCapture", "gems<=r100", "sg@100", "sg@200", "sg@300", "firstTribute", "tributes", "tribBag",
           "campCoHit", "cassiIssacDist", "nearestAlly", "allyWithin2", "enemyHalf", "idle",
           "deaths<=r150", "deaths", "champDmg", "killsEnemyHalf", "killsOwnHalf"]


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def features(tr, team):
    R = tr["rounds"]
    if len(R) < 5:
        return None
    C0 = R[0]["c"]
    me = [i for i, c in enumerate(C0) if c[0] == team]
    # round-0 positions are the spawn regions; the nearer spawn defines "own half"
    sp = {t: tuple(sum(v) / len(v) for v in zip(*[(c[3], c[4]) for c in C0 if c[0] == t])) for t in (0, 1)}
    own = lambda pos: math.dist(pos, sp[team]) < math.dist(pos, sp[1 - team])
    f, cnt = defaultdict(float), defaultdict(int)
    for chk in (100, 200, 300):
        f[f"sg@{chk}"] = next((r for r in R if r["r"] >= chk), R[-1])["sg"][team]
    tribs = [b["sg"][team] - a["sg"][team] for a, b in zip(R, R[1:]) if b["sg"][team] > a["sg"][team]]
    f["firstTribute"] = next((b["r"] for a, b in zip(R, R[1:]) if b["sg"][team] > a["sg"][team]), 9999)
    f["tributes"], f["tribBag"] = len(tribs), sum(tribs) / max(1, len(tribs))
    f["firstCapture"] = 9999
    for a, b in zip(R, R[1:]):
        for i in me:
            gained = b["c"][i][7] - a["c"][i][7]
            if gained > 0 and b["c"][i][2]:
                f["firstCapture"] = min(f["firstCapture"], b["r"])
                if b["r"] <= 100:
                    f["gems<=r100"] += gained
        for i in me:
            if a["c"][i][2] and not b["c"][i][2]:
                f["deaths"] += 1; f["deaths<=r150"] += b["r"] <= 150
        for i, c in enumerate(a["c"]):
            if c[0] != team and c[2] and not b["c"][i][2] and c[3] >= 0:
                f["killsEnemyHalf" if not own((c[3], c[4])) else "killsOwnHalf"] += 1
    for r in R[10:]:
        cs = r["c"]
        alive = [i for i in me if cs[i][2] and cs[i][3] >= 0]
        for i in alive:
            pos = (cs[i][3], cs[i][4])
            f["enemyHalf"] += not own(pos); cnt["enemyHalf"] += 1
            others = [cheb(pos, (cs[j][3], cs[j][4])) for j in alive if j != i]
            if others:
                f["nearestAlly"] += min(others); f["allyWithin2"] += min(others) <= 2
                cnt["nearestAlly"] += 1; cnt["allyWithin2"] += 1
        types = {CTYPE[cs[i][1]]: i for i in alive}
        if "cassi" in types and "issac" in types:
            a, b = cs[types["cassi"]], cs[types["issac"]]
            f["cassiIssacDist"] += cheb((a[3], a[4]), (b[3], b[4])); cnt["cassiIssacDist"] += 1
    camp_hitters, acted = defaultdict(set), defaultdict(set)
    for a in tr["actions"]:
        if a[1] not in me:
            continue
        if a[2] in (MOVE, ATTACK, CAST, CHANNEL, SHOP):
            acted[a[1]].add(a[0])
        for h in a[9]:
            if a[2] in (ATTACK, CAST) and h[0] == "m":
                camp_hitters[(a[0], h[1])].add(a[1])
            if a[2] in (ATTACK, CAST) and h[0] == "c" and h[1] not in me:
                f["champDmg"] += h[2]
    f["campCoHit"] = sum(len(v) >= 2 for v in camp_hitters.values()) / max(1, len(camp_hitters))
    alive_rounds = sum(1 for r in R for i in me if r["c"][i][2])
    f["idle"] = 1 - sum(len(v) for v in acted.values()) / max(1, alive_rounds)
    for k, n in cnt.items():
        f[k] /= max(1, n)
    if not cnt["cassiIssacDist"]:
        f["cassiIssacDist"] = float("nan")
    return f


def load_meta(groups, team):
    meta = {}
    for p in glob.glob(os.path.join(groups, "*.json")):
        for m in json.load(open(p))["matches"]:
            if m.get("status") != "finished":
                continue
            s = {x["side"]: x for x in m["sides"]}
            us = 0 if s["a"]["teamId"] == team else 1
            meta[m["id"]] = dict(us=us, win=s["ab"[us]]["result"] == "win", opp=s["ab"[1 - us]]["teamName"], map=m["map"], stats=m.get("statistics"))
    return meta


def mean(xs):
    xs = [x for x in xs if not (isinstance(x, float) and math.isnan(x))]
    return sum(xs) / len(xs) if xs else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trajdir"); ap.add_argument("--side", type=int); ap.add_argument("--groups"); ap.add_argument("--team")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    meta = load_meta(a.groups, a.team) if a.groups else {}
    rows = []
    for p in sorted(glob.glob(os.path.join(a.trajdir, "*.json"))):
        mid = os.path.basename(p)[:-5]
        m = meta.get(mid) or ({"us": a.side, "win": None} if a.side is not None else None)
        if m is None:
            continue
        tr = json.load(open(p))
        if a.check:
            st = m["stats"]
            last = tr["rounds"][-1]
            ok_sg = last["sg"] == [t["submittedGems"] for t in st["teams"]]
            ok_k = [c[9] for c in last["c"]] == [c["kills"] for c in sorted(st["champions"], key=lambda c: c["champion"])]
            rows.append((ok_sg, ok_k))
            continue
        fu, fo = features(tr, m["us"]), features(tr, 1 - m["us"])
        if fu and fo:
            if m["win"] is None:
                # local runs: the winner is whoever banked more
                m = {**m, "win": tr["rounds"][-1]["sg"][m["us"]] > tr["rounds"][-1]["sg"][1 - m["us"]]}
            rows.append((m, fu, fo))
    if a.check:
        n = len(rows)
        print(f"check: final submitted gems match {sum(r[0] for r in rows)}/{n}; per-champion kills match {sum(r[1] for r in rows)}/{n}")
        return 0 if all(r[0] and r[1] for r in rows) and n else 1
    for lab, sel in [("ALL", lambda m: True), ("LOSSES", lambda m: not m["win"]), ("WINS", lambda m: m["win"])]:
        S = [(fu, fo) for m, fu, fo in rows if sel(m)]
        if not S:
            continue
        print(f"\n== {lab} (games={len(S)})     {'US':>9} {'OPP':>9}")
        for k in METRICS:
            print(f"  {k:15} {mean([x[k] for x, _ in S]):9.2f} {mean([y[k] for _, y in S]):9.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
