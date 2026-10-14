// Replay(s) -> trajectory JSON: one snapshot per round (+ the final state, since the winning
// tribute lands in the last round and no further round starts) and the champion action log.
// Step kinds (phase 10 = champion action), inferred from state diffs and cross-checked against
// official statistics: 4 move, 5 basic attack, 6 ability cast (incl. dashes), 9 yield,
// 11 channel start (recall/tribute), 12 ward, 13 shop, 20 consumable use.
// usage: node extract.mjs <replay.repl26a|bundle.zip|dir> <outdir>
import fs from 'node:fs';
import path from 'node:path';
import { ensureAssets, openEngine, openReplay, replayBytes } from './decode.mjs';

const snapshot = (s, final = false) => ({
  r: s.round, ...(final ? { final: true } : {}), sg: s.submittedGems.map(Number),
  c: s.champions.map((c) => [c.team, c.type, c.alive ? 1 : 0, c.position?.x ?? -1, c.position?.y ?? -1,
    Math.round(c.health), Math.round(c.maxHealth), c.gems, c.gold, c.kills]),
  g: s.groups.map((g) => [g.rewardAvailable ? 1 : 0, g.captureTeam]),
});

export function extract(E, file) {
  const R = openReplay(E, replayBytes(file));
  const rounds = [], actions = [];
  let lastRound = -1, prev = null;
  try {
    for (let p = 0; p <= R.size; p++) {
      R.seek(p);
      const s = R.state(), st = s.step;
      if (s.round !== lastRound && s.champions.length) { rounds.push(snapshot(s)); lastRound = s.round; }
      if (st.phase === 10 && st.actor.kind === 1 && prev) {
        const a = st.actor.index, c1 = s.champions[a], hit = [];
        s.champions.forEach((c, i) => { const o = prev.champions[i]; if (o && c.health < o.health) hit.push(['c', i, Math.round(o.health - c.health)]); });
        s.camps.forEach((c, i) => { const o = prev.camps[i]; if (o && c.health < o.health) hit.push(['m', i, Math.round(o.health - c.health)]); });
        actions.push([st.round, a, st.kind, st.from?.x ?? -1, st.from?.y ?? -1, st.to?.x ?? -1, st.to?.y ?? -1, c1?.gems ?? 0, c1?.gold ?? 0, hit]);
      }
      prev = s;
    }
    const last = R.state();
    rounds.push(snapshot(last, true));
    return { rounds, actions, camps: last.camps.map((c) => [c.type, c.position?.x ?? -1, c.position?.y ?? -1]), outcome: last.outcome, finalRound: last.round, drafts: last.drafts };
  } finally { R.close(); }
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [input, outDir] = process.argv.slice(2);
  if (!input || !outDir) { console.error('usage: node extract.mjs <replay|dir> <outdir>'); process.exit(2); }
  const files = fs.statSync(input).isDirectory()
    ? fs.readdirSync(input).filter((f) => /\.(zip|repl26a)$/.test(f)).map((f) => path.join(input, f))
    : [input];
  if (!files.length) { console.error('no replays found'); process.exit(1); }
  ensureAssets(fs.readFileSync(files[0]).subarray(0, 2).toString('latin1') === 'PK' ? (() => { const t = path.join(outDir, '.probe.repl26a'); fs.mkdirSync(outDir, { recursive: true }); fs.writeFileSync(t, replayBytes(files[0])); return t; })() : files[0]);
  fs.mkdirSync(outDir, { recursive: true });
  const E = await openEngine();
  let done = 0;
  for (const f of files) {
    const out = path.join(outDir, path.basename(f).replace(/\.(zip|repl26a)$/, '.json'));
    if (fs.existsSync(out)) continue;
    try { fs.writeFileSync(out, JSON.stringify(extract(E, f))); done++; } catch (e) { console.error('FAIL', f, e.message); }
  }
  fs.rmSync(path.join(outDir, '.probe.repl26a'), { force: true });
  console.log(`extracted ${done}/${files.length} -> ${outDir}`);
}
