// One-pass imitation dataset extractor: single engine walk emits, per round,
// the trajectory snapshot, the action log, AND each alive champ's own
// information set ({team, champion} perspective) — the (visible-state -> action)
// pairs used for policy training.
// usage: node dataset_extract.mjs <replay.repl26a|zip|dir> <outdir>
import fs from 'node:fs';
import path from 'node:path';
import { ensureAssets, openEngine, openReplay, replayBytes } from './decode.mjs';

function makePReader(E, handle) {
  const { X, view, outBuf, args, check, analytics } = E;
  const snap = X.Engine_Malloc(X.Game_Visualiser_SnapshotSize());
  const decodeHeader = () => {
    for (const h of E.headerCandidates) {
      try { const r = h(view(), snap); if (r && typeof r.round === 'number' && Array.isArray(r.drafts)) { E.headerCandidates = [h]; return r; } } catch {}
    }
    throw new Error('no header decoder');
  };
  const read = (team, champ) => {
    const t = args + 48, v = view();
    v.setUint32(t, team >>> 0, true); v.setUint32(t + 4, champ >>> 0, true);
    check(X.Game_Visualiser_ReadState(handle, t, snap));
    return analytics(view(), snap, decodeHeader());
  };
  return { read, free: () => X.Engine_Free(snap) };
}

const champRow = (c) => (c && c.position ? [c.team, c.type, c.alive ? 1 : 0,
  c.position.x, c.position.y, Math.round(c.health), Math.round(c.maxHealth),
  c.gems ?? 0, c.gold ?? 0, c.kills ?? 0] : [c?.team ?? -1, c?.type ?? -1, c?.alive ? 1 : 0]);

export function extractDataset(E, file) {
  const bytes = replayBytes(file);
  const { X, view, outBuf, args, check } = E;
  const p = X.Engine_Malloc(bytes.length);
  new Uint8Array(X.memory.buffer, p, bytes.length).set(bytes);
  check(X.Game_FromReplayFileBytes(p, bytes.length, outBuf));
  X.Engine_Free(p);
  const handle = view().getBigUint64(outBuf, true);
  check(X.Game_Runner_SetCheckpointsEnabled(handle, 1));
  check(X.Game_Visualiser_Timeline_GetMain(handle, outBuf));
  const main = view().getBigUint64(outBuf, true);
  check(X.Game_Visualiser_Timeline_Info(handle, main, args, args + 8, args + 16));
  const size = view().getUint32(args, true);
  const PR = makePReader(E, handle);
  const rounds = [], actions = [];
  let lastRound = -1, prev = null;
  try {
    for (let pos = 0; pos <= size; pos++) {
      check(X.Game_Visualiser_Timeline_Seek(handle, main, pos, 0));
      const spec = PR.read(2, 0xffffffff); // spectator
      const st = spec.step;
      if (spec.round !== lastRound && spec.champions.length) {
        // new round: snapshot + per-champ views
        const views = [];
        for (let i = 0; i < spec.champions.length; i++) {
          const c = spec.champions[i];
          if (!c.alive) { views.push(null); continue; }
          const s = PR.read(c.team, c.entity?.index ?? i);
          const selfC = s.champions[i];
          views.push({
            self: champRow(c),
            items: (selfC?.items || []).map(Number),
            consumables: (selfC?.consumables || []).map(Number),
            abilityTargets: (selfC?.abilityTargets || []).map((t) => (t == null ? -1 : Number(t))),
            stunned: selfC?.stunSource ? 1 : 0,
            see: s.champions.map((cc) => (cc && cc.position ? [cc.position.x, cc.position.y, cc.team, cc.type, Math.round(cc.health), Math.round(cc.maxHealth)] : null)),
            camps: s.camps ? s.camps.map((g) => (g && g.position ? [g.position.x, g.position.y, g.captureTeam ?? -1, g.rewardAvailable ? 1 : 0] : null)) : null,
            wards: s.wards ? s.wards.map((w) => (w && w.position ? [w.position.x, w.position.y, w.team ?? -1] : null)).filter(Boolean) : [],
            spells: s.spells ? s.spells.map((sp) => (sp && sp.position ? [sp.position.x, sp.position.y, sp.type ?? -1] : null)).filter(Boolean) : [],
          });
        }
        rounds.push({
          r: spec.round,
          sg: spec.submittedGems.map(Number),
          c: spec.champions.map((c) => [c.team, c.type, c.alive ? 1 : 0, c.position?.x ?? -1, c.position?.y ?? -1, Math.round(c.health), Math.round(c.maxHealth), c.gems, c.gold, c.kills]),
          g: spec.groups.map((g) => [g.rewardAvailable ? 1 : 0, g.captureTeam]),
          views,
        });
        lastRound = spec.round;
      }
      if (st.phase === 10 && st.actor.kind === 1 && prev) {
        const a = st.actor.index, c1 = spec.champions[a], hit = [];
        spec.champions.forEach((c, i) => { const o = prev.champions[i]; if (o && c.health < o.health) hit.push(['c', i, Math.round(o.health - c.health)]); });
        spec.camps.forEach((c, i) => { const o = prev.camps[i]; if (o && c.health < o.health) hit.push(['m', i, Math.round(o.health - c.health)]); });
        actions.push([st.round, a, st.kind, st.from?.x ?? -1, st.from?.y ?? -1, st.to?.x ?? -1, st.to?.y ?? -1, c1?.gems ?? 0, c1?.gold ?? 0, hit]);
      }
      prev = spec;
    }
    const last = PR.read(2, 0xffffffff);
    return { rounds, actions, camps: last.camps.map((c) => [c.type, c.position?.x ?? -1, c.position?.y ?? -1]), outcome: last.outcome, finalRound: last.round, drafts: last.drafts };
  } finally { PR.free(); X.Game_Destroy(handle); }
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [input, outDir] = process.argv.slice(2);
  if (!input || !outDir) { console.error('usage: node dataset_extract.mjs <replay|dir> <outdir>'); process.exit(2); }
  const files = fs.statSync(input).isDirectory()
    ? fs.readdirSync(input).filter((f) => /\.(zip|repl26a)$/.test(f)).map((f) => path.join(input, f))
    : [input];
  if (!files.length) { console.error('no replays found'); process.exit(1); }
  ensureAssets(files[0]);
  fs.mkdirSync(outDir, { recursive: true });
  let done = 0;
  for (const f of files) {
    const out = path.join(outDir, path.basename(f).replace(/\.(zip|repl26a)$/, '.ds.json'));
    if (fs.existsSync(out)) continue;
    try { const E = await openEngine(replayBytes(f)); fs.writeFileSync(out, JSON.stringify(extractDataset(E, f))); done++; } catch (e) { console.error('FAIL', f, e.message); }
  }
  console.log(`dataset-extracted ${done}/${files.length} -> ${outDir}`);
}
