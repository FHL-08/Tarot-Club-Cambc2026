#!/usr/bin/env python3
"""cambc-docs — stdio MCP server over the exported game handbook.

Indexes docs/GameRules.md and docs/BotAPICpp.md by markdown headings and
answers structured lookups, so agents can pull exact sections instead of
reading 8,000-line files.

Tools:
  list_sections(doc)            heading index for a doc ("rules" | "api")
  get_rule(topic)               GameRules.md section matching a heading/topic
  api_lookup(symbol)            BotAPICpp.md entries for a type or method
  champion(name)                roster entry (kit + stats) from GameRules.md
  search_docs(query)            ranked section hits across both docs
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = {
    "rules": ROOT / "docs" / "GameRules.md",
    "api": ROOT / "docs" / "BotAPICpp.md",
}

PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "cambc-docs", "version": "0.1.0"}


def index_doc(path):
    """Split a markdown file into (level, title, body) sections."""
    text = path.read_text(encoding="utf-8", errors="replace")
    sections = []
    cur_title, cur_level, cur_lines = None, 0, []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
        m = re.match(r"^(#{1,4})\s+(.*)", line) if not in_fence else None
        if m:
            if cur_title is not None:
                sections.append((cur_level, cur_title, "\n".join(cur_lines).strip()))
            cur_level, cur_title, cur_lines = len(m.group(1)), m.group(2).strip(), []
        elif cur_title is not None:
            cur_lines.append(line)
    if cur_title is not None:
        sections.append((cur_level, cur_title, "\n".join(cur_lines).strip()))
    return sections


INDEX = {name: index_doc(path) for name, path in DOCS.items() if path.exists()}


def score(query, text):
    terms = [t for t in re.split(r"\W+", query.lower()) if t]
    hay = text.lower()
    return sum(hay.count(t) for t in terms)


def subtree(secs, i, limit=12000):
    """Render section i plus its nested child sections."""
    level, title, body = secs[i]
    parts = [f"{'#' * level} {title}\n\n{body}".strip()]
    for l, t, b in secs[i + 1:]:
        if l <= level:
            break
        parts.append(f"{'#' * l} {t}\n\n{b}".strip())
    out = "\n\n".join(parts)
    return out[:limit] + ("\n…[truncated]" if len(out) > limit else "")


def tool_list_sections(doc):
    secs = INDEX.get(doc)
    if secs is None:
        return f"unknown doc {doc!r}; expected one of {sorted(INDEX)}"
    return "\n".join(f"{'#' * l} {t}" for l, t, _ in secs)


def tool_get_rule(topic):
    secs = INDEX.get("rules", [])
    hits = [(score(topic, t), i) for i, (l, t, b) in enumerate(secs)]
    hits = [h for h in hits if h[0] > 0]
    if not hits:
        return f"no rules section matching {topic!r}. Try list_sections('rules')."
    hits.sort(key=lambda h: -h[0])
    return subtree(secs, hits[0][1])


def tool_api_lookup(symbol):
    secs = INDEX.get("api", [])
    sym = symbol.strip().lower()
    sym = re.sub(r"^(cambc|std)::", "", sym)
    hits = [i for i, (l, t, b) in enumerate(secs)
            if t.lower() == sym or t.lower() == f"cambc::{sym}"]
    if not hits:
        hits = [i for i, (l, t, b) in enumerate(secs)
                if sym and sym in t.lower()]
    if not hits:
        return (f"no API entry matching {symbol!r}. "
                "Try list_sections('api') or a shorter symbol name.")
    return "\n\n---\n\n".join(subtree(secs, i) for i in hits[:3])


def tool_champion(name):
    secs = INDEX.get("rules", [])
    n = name.strip().lower()
    hits = [i for i, (l, t, b) in enumerate(secs)
            if l <= 2 and n in t.lower()]
    if not hits:
        return f"no champion entry matching {name!r}. Roster: Cassi & Issac, Ryth, Vesper Vale, Prism, Stellar, Vendetta, Marcella, Tavi, Willow, Ren, Varg."
    return "\n\n---\n\n".join(subtree(secs, i) for i in hits[:2])


def tool_search_docs(query):
    out = []
    for doc, secs in INDEX.items():
        for l, t, b in secs:
            s = score(query, f"{t} {b[:400]}")
            if s:
                out.append((s, doc, l, t))
    if not out:
        return "no sections matched."
    out.sort(key=lambda x: -x[0])
    return "\n".join(f"[{s:>3}] {doc}: {'#' * l} {t}" for s, doc, l, t in out[:15])


TOOLS = {
    "list_sections": {
        "description": "List the markdown heading index of a doc ('rules' = GameRules.md, 'api' = BotAPICpp.md).",
        "inputSchema": {"type": "object", "properties": {
            "doc": {"type": "string", "enum": ["rules", "api"]}},
            "required": ["doc"]},
        "fn": lambda a: tool_list_sections(a["doc"]),
    },
    "get_rule": {
        "description": "Return the GameRules.md section best matching a topic (e.g. 'tribute', 'channels', 'camps & capture').",
        "inputSchema": {"type": "object", "properties": {
            "topic": {"type": "string"}}, "required": ["topic"]},
        "fn": lambda a: tool_get_rule(a["topic"]),
    },
    "api_lookup": {
        "description": "Return BotAPICpp.md reference entries for a cambc type or method (e.g. 'champion', 'distance_field', 'use').",
        "inputSchema": {"type": "object", "properties": {
            "symbol": {"type": "string"}}, "required": ["symbol"]},
        "fn": lambda a: tool_api_lookup(a["symbol"]),
    },
    "champion": {
        "description": "Return a champion's roster entry — kit, stats, growths — from GameRules.md.",
        "inputSchema": {"type": "object", "properties": {
            "name": {"type": "string"}}, "required": ["name"]},
        "fn": lambda a: tool_champion(a["name"]),
    },
    "search_docs": {
        "description": "Keyword-search section headings+bodies across both docs; returns ranked heading list.",
        "inputSchema": {"type": "object", "properties": {
            "query": {"type": "string"}}, "required": ["query"]},
        "fn": lambda a: tool_search_docs(a["query"]),
    },
}


def respond(rid, result=None, error=None):
    msg = {"jsonrpc": "2.0", "id": rid}
    if error is not None:
        msg["error"] = {"code": -32603, "message": str(error)}
    else:
        msg["result"] = result
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue
        method, rid = req.get("method"), req.get("id")

        if method == "initialize":
            respond(rid, {"protocolVersion": PROTOCOL_VERSION,
                          "capabilities": {"tools": {}},
                          "serverInfo": SERVER_INFO})
        elif method == "ping":
            respond(rid, {})
        elif method == "tools/list":
            respond(rid, {"tools": [
                {"name": n, "description": t["description"],
                 "inputSchema": t["inputSchema"]}
                for n, t in TOOLS.items()]})
        elif method == "tools/call":
            params = req.get("params") or {}
            tool = TOOLS.get(params.get("name"))
            if tool is None:
                respond(rid, error=f"unknown tool {params.get('name')!r}")
                continue
            try:
                text = tool["fn"](params.get("arguments") or {})
                respond(rid, {"content": [{"type": "text", "text": text}]})
            except Exception as exc:  # surface tool errors as MCP errors
                respond(rid, error=f"{type(exc).__name__}: {exc}")
        elif rid is not None:  # known-shape request we don't implement
            respond(rid, error=f"unsupported method {method!r}")
        # notifications (rid is None) are dropped silently


if __name__ == "__main__":
    main()
