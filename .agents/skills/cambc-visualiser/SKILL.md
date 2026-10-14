---
name: cambc-visualiser
description: Open a replay for the user in the CAMBC visualiser — a local match, a platform match id, or our latest platform game. Use whenever the user wants to watch a match or you want to show them one; never start `cambc.exe view` any other way.
---

# Showing a replay

The user keeps ONE browser tab on **http://127.0.0.1:7026/**. Every replay goes
to that address, replacing the previous one, so the tab never dies.

```bash
tools/viz.sh                         # latest local replay under replays/
tools/viz.sh path/to/match.repl26a   # a local replay or bundle (zip or .repl26a)
tools/viz.sh <platform-match-uuid>   # downloads the public bundle
tools/viz.sh last-platform [map]     # our team's most recent finished platform match
```

Then tell the user: "reload your http://127.0.0.1:7026/ tab". If they have no tab
open, give them that URL (or the `browser_preview` of `http://127.0.0.1:7026`).

## Rules

- Always run it from the main checkout (`/home/j30695fl/Desktop/battlecode_26`),
  never from a factory worktree. The server serves its replay from the
  checkout's `replays/viewer/`, and worktrees get deleted.
- Never `pkill`/`kill` cambc processes by name or pattern, and never start
  `cambc.exe view` on another port or without `setsid`. A pattern kill once took
  the user's server down, and a plain `&` child dies with the shell that started
  it. Either way the user ends up on Firefox's "Unable to connect" page.
- `viz.sh` refuses to touch port 7026 when anything other than `cambc.exe view`
  holds it. Report that to the user; don't force it.
- `BROWSER=true tools/viz.sh …` suppresses cambc's attempt to open a new browser
  tab (useful when the user already has the tab open).
