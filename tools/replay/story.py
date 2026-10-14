#!/usr/bin/env python3
"""Storyboard one trajectory: per window, each champion's tile, hp, bag, gold and what it hit.

usage: story.py <traj.json> --side 0|1 [--upto 200] [--step 10]
"""
import argparse, json
from collections import Counter, defaultdict

CTYPE = ["ryth", "vesper", "vendetta", "cassi", "issac", "stellar", "prism", "marcella", "willow", "ren", "varg", "tavi"]
CAMP = ["wolf", "elk", "gemC", "goldC", "mono"]
KIND = {11: "chan", 12: "ward", 13: "shop", 20: "lock"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("traj"); ap.add_argument("--side", type=int, required=True)
    ap.add_argument("--upto", type=int, default=200); ap.add_argument("--step", type=int, default=10)
    a = ap.parse_args()
    tr = json.load(open(a.traj))
    R, camps, C0 = tr["rounds"], tr["camps"], tr["rounds"][0]["c"]
    name = lambda i: ("US " if C0[i][0] == a.side else "OPP") + ":" + CTYPE[C0[i][1]]
    hits = defaultdict(Counter)
    for x in tr["actions"]:
        w = x[0] // a.step
        for h in x[9]:
            tgt = CAMP[camps[h[1]][0]] if h[0] == "m" else "x" + CTYPE[C0[h[1]][1]]
            hits[(w, x[1])][tgt] += 1
        if x[2] in KIND:
            hits[(w, x[1])][KIND[x[2]]] += 1
    print(f"final r{tr['finalRound']}  banked US {R[-1]['sg'][a.side]}  OPP {R[-1]['sg'][1 - a.side]}")
    for r in R:
        if r["r"] % a.step or r["r"] > a.upto:
            continue
        print(f"r{r['r']:<4} banked US {r['sg'][a.side]:4}  OPP {r['sg'][1 - a.side]:4}")
        for i, c in enumerate(r["c"]):
            if not c[2]:
                print(f"     {name(i):16} DEAD"); continue
            h = ", ".join(f"{k}x{v}" for k, v in hits[(r['r'] // a.step, i)].most_common(3))
            print(f"     {name(i):16} ({c[3]:2},{c[4]:2}) hp{c[5]:5}/{c[6]:<5} bag{c[7]:3} gold{c[8]:4}  {h}")


if __name__ == "__main__":
    main()
