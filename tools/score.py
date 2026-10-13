#!/usr/bin/env python3
"""Build a bot, run one completed match, and gate on its statistics.

Usage:
    python3 tools/score.py <bot> <opponent> --map <name> [--assert <stat><op><N> ...]

Runs `./cambc.exe build <bot>` then
`./cambc.exe run <bot> <opponent> --map <map> --statistics /tmp/<tag>.json`,
prints a per-team stats table, evaluates each --assert gate, and prints
PASS or FAIL. Exit code 0 = PASS, 1 = FAIL, 2 = the match could not be
produced (build/engine/stats failure).

Assertion stats are per-team aggregates resolved against team A (the bot
under test) unless prefixed: `botFaults`, `submittedGems`, `collectedGems`,
`earnedGold`, `spentGold`, `kills`, `deaths`, `assists`, `gems`, `gold`.
Use `a.`/`bot.`/`teamA.` or `b.`/`opp.`/`teamB.` prefixes for explicit
team selection, `round` for the final round, and `outcome` for the winner
(`teamA`/`teamB`, == and != only). Operators: == != >= <= > <.

--statistics needs a COMPLETED match, so this tool never passes --rounds.
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CAMBC = PROJECT_ROOT / "cambc.exe"

# Per-team columns printed in the summary table.
SUMMARY_STATS = ("submittedGems", "earnedGold", "kills", "deaths", "botFaults")

# Stats aggregated from per-champion entries; botFaults/submittedGems are
# team-level fields in the JSON, everything else sums over champions.
CHAMPION_STATS = (
    "submittedGems",
    "collectedGems",
    "earnedGold",
    "spentGold",
    "kills",
    "deaths",
    "assists",
    "gems",
    "gold",
)

ASSERT_RE = re.compile(r"^([A-Za-z][A-Za-z0-9_.]*?)\s*(==|!=|>=|<=|>|<)\s*(\S+)$")

OPS = {
    "==": lambda a, b: a == b,
    "!=": lambda a, b: a != b,
    ">=": lambda a, b: a >= b,
    "<=": lambda a, b: a <= b,
    ">": lambda a, b: a > b,
    "<": lambda a, b: a < b,
}

TEAM_PREFIXES = {
    "a": 0,
    "bot": 0,
    "teama": 0,
    "b": 1,
    "opp": 1,
    "opponent": 1,
    "teamb": 1,
}


def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(2)


def run(cmd):
    print(f"+ {' '.join(str(c) for c in cmd)}", flush=True)
    proc = subprocess.run(cmd, cwd=PROJECT_ROOT)
    if proc.returncode != 0:
        die(f"{Path(cmd[0]).name} {' '.join(str(c) for c in cmd[1:2])} failed "
            f"(exit {proc.returncode})")


def team_stats(stats, idx):
    champs = [c for c in stats.get("champions", []) if c.get("team") == idx]
    agg = {k: sum(c.get(k, 0) for c in champs) for k in CHAMPION_STATS}
    for t in stats.get("teams", []):
        if t.get("team") == idx:
            agg["submittedGems"] = t.get("submittedGems", agg["submittedGems"])
    faults = stats.get("botFaults", [])
    agg["botFaults"] = faults[idx] if idx < len(faults) else 0
    return agg


def parse_assertion(assertion):
    """Validate an --assert string up front; return (name, op, rhs_text)."""
    m = ASSERT_RE.match(assertion)
    if not m:
        die(f"malformed --assert '{assertion}' "
            f"(want <stat><op><N>, ops: == != >= <= > <)")
    name, op, raw_rhs = m.groups()
    stat = name
    if "." in name:
        prefix, _, stat = name.partition(".")
        if prefix.lower() not in TEAM_PREFIXES:
            die(f"unknown team prefix in --assert '{name}' "
                f"(use a./bot./teamA. or b./opp./teamB.)")
    if stat.lower() == "outcome":
        if op not in ("==", "!="):
            die(f"--assert '{assertion}': 'outcome' is a string; "
                f"only == and != apply")
    else:
        known = {k.lower() for k in CHAMPION_STATS} | {"botfaults", "round"}
        if stat.lower() not in known:
            die(f"unknown stat '{stat}' in --assert '{name}' "
                f"(have: {', '.join(CHAMPION_STATS)}, botFaults, "
                f"round, outcome)")
        try:
            float(raw_rhs)
        except ValueError:
            die(f"--assert '{assertion}': stat '{stat}' is numeric "
                f"but '{raw_rhs}' is not")
    return name, op, raw_rhs


def check(assertion, aggregates, stats):
    """Evaluate a pre-validated assertion; return (ok, detail)."""
    name, op, raw_rhs = parse_assertion(assertion)
    team_idx, stat = 0, name
    if "." in name:
        prefix, _, stat = name.partition(".")
        team_idx = TEAM_PREFIXES[prefix.lower()]
    label = f"team{'AB'[team_idx]}.{stat}"
    if stat.lower() == "outcome":
        value = stats.get("outcome")
        return OPS[op](str(value), raw_rhs), f"{label} = {value!r}"
    if stat.lower() == "round":
        value = stats.get("round")
    else:
        agg = aggregates[team_idx]
        key = stat if stat in agg else {k.lower(): k for k in agg}[stat.lower()]
        label = f"team{'AB'[team_idx]}.{key}"
        value = agg[key]
    return OPS[op](value, float(raw_rhs)), f"{label} = {value}"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="Assertion stats: " + ", ".join(CHAMPION_STATS)
        + ", botFaults, round, outcome. Bare names resolve to team A "
        "(<bot>); prefix with b./opp./teamB. for the opponent.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("bot", help="bot under test (team A)")
    parser.add_argument("opponent", help="opponent bot (team B)")
    parser.add_argument("--map", required=True, dest="map_name",
                        help="map name under maps/ or a .map26a path")
    parser.add_argument("--assert", dest="assertions", action="append",
                        nargs="+", metavar="STAT_OP_N", default=[],
                        help="gate assertions like 'botFaults==0' or "
                        "'b.submittedGems<1000'; repeatable")
    args = parser.parse_args()
    assertions = [a for group in args.assertions for a in group]
    for a in assertions:
        parse_assertion(a)

    if not CAMBC.exists():
        die(f"{CAMBC} not found (broken symlink in this checkout?)")

    run([CAMBC, "build", args.bot])

    tag = re.sub(r"[^A-Za-z0-9_-]", "-",
                 f"score-{args.bot}-vs-{args.opponent}-"
                 f"{Path(args.map_name).stem}-{int(time.time() * 1000)}")
    stats_path = Path(tempfile.gettempdir()) / f"{tag}.json"
    run([CAMBC, "run", args.bot, args.opponent, "--map", args.map_name,
         "--statistics", str(stats_path)])

    try:
        stats = json.loads(stats_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        die(f"cannot read statistics from {stats_path}: {e}")

    aggregates = [team_stats(stats, i) for i in (0, 1)]
    bots = [args.bot, args.opponent]

    print(f"\n{args.bot} vs {args.opponent} on {args.map_name} — "
          f"outcome {stats.get('outcome')}, round {stats.get('round')} "
          f"[{stats_path}]")
    header = ["team", "bot"] + list(SUMMARY_STATS)
    rows = [
        ["AB"[i], bots[i]]
        + [str(aggregates[i][s]) for s in SUMMARY_STATS]
        for i in (0, 1)
    ]
    widths = [max(len(r[c]) for r in [header] + rows)
              for c in range(len(header))]
    for r in [header] + rows:
        print("  ".join(r[c].ljust(widths[c])
                        for c in range(len(r))).rstrip())

    results = [check(a, aggregates, stats) for a in assertions]
    for a, (ok, detail) in zip(assertions, results):
        print(f"  {'ok  ' if ok else 'FAIL'} {a:<24} ({detail})")

    passed = all(ok for ok, _ in results)
    print("PASS" if passed else "FAIL")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
