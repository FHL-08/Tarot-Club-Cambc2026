// Per-champion vision extraction: what each champ could see each round.
// Reads the replay through Game_Visualiser_ReadState with a {team, champion}
// perspective (the same call behind the visualiser's "View X vision"), so the
// output is the enemy's own information set — enemies without position data
// were not visible to that champ.
// usage: node vision_extract.mjs <replay.repl26a|zip|dir> <outdir>
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

export function extractVision(E, file) {
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
  const rounds = [];
  try {
    let lastRound = -1;
    for (let pos = 0; pos <= size; pos++) {
      check(X.Game_Visualiser_Timeline_Seek(handle, main, pos, 0));
      const spec = PR.read(2, 0xffffffff);
      if (!spec.champions.length || spec.round === lastRound) continue;
      lastRound = spec.round;
      // per-champ perspectives: for each alive champ, what do THEY see
      const views = [];
      for (let i = 0; i < spec.champions.length; i++) {
        const c = spec.champions[i];
        if (!c.alive) { views.push(null); continue; }
        const s = PR.read(c.team, c.entity?.index ?? i);
        views.push({
          self: champRow(spec.champions[i]),
          // which champs they can see: index -> position present
          see: s.champions.map((cc) => (cc && cc.position ? [cc.position.x, cc.position.y, cc.team, cc.type, Math.round(cc.health)] : null)),
          camps: s.camps ? s.camps.map((g) => (g && g.position ? [g.position.x, g.position.y, g.captureTeam ?? -1] : null)) : null,
        });
      }
      rounds.push({ r: spec.round, views });
    }
  } finally { PR.free(); X.Game_Destroy(handle); }
  return { rounds };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [input, outDir] = process.argv.slice(2);
  if (!input || !outDir) { console.error('usage: node vision_extract.mjs <replay|dir> <outdir>'); process.exit(2); }
  const files = fs.statSync(input).isDirectory()
    ? fs.readdirSync(input).filter((f) => /\.(zip|repl26a)$/.test(f)).map((f) => path.join(input, f))
    : [input];
  if (!files.length) { console.error('no replays found'); process.exit(1); }
  ensureAssets(files[0]);
  fs.mkdirSync(outDir, { recursive: true });
  let done = 0;
  for (const f of files) {
    const out = path.join(outDir, path.basename(f).replace(/\.(zip|repl26a)$/, '.vision.json'));
    if (fs.existsSync(out)) continue;
    try { const E = await openEngine(replayBytes(f)); fs.writeFileSync(out, JSON.stringify(extractVision(E, f))); done++; } catch (e) { console.error('FAIL', f, e.message); }
  }
  console.log(`vision-extracted ${done}/${files.length} -> ${outDir}`);
}
