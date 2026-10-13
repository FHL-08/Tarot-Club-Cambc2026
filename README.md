# Tarot Club — CAMBC 2026

Team Tarot Club's bots for `cambc2026a` (Cambridge Battle Code). C++ bots
compiled to WASI, fighting for gems on the competition map pool.

## Quick start

```bash
./cambc.exe init        # install toolchain + SDK + maps (idempotent)
./cambc.exe new <name> --lang cpp
./cambc.exe build <name>
./cambc.exe run <name> starter-cpp --map expanse --statistics /tmp/s.json
```

- **Docs**: `docs/GameRules.md` (rules + champions), `docs/BotAPICpp.md` (C++ API).
- **Agents**: start at `AGENTS.md`; skills live in `.agents/skills/`.
- **Factory**: issues labeled `factory:ready` get decomposed, built in
  worktrees, verified by real matches, and merged through
  `factory/scripts/merge-gate.sh`. See `factory/brief-template.md`.
- **Docs MCP**: `.mcp.json` exposes `cambc-docs` (rules/API/champion lookup).
