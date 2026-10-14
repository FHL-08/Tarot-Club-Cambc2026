// Headless replay decoder: the visualiser's engine WASM replays the match deterministically,
// and its own snapshot decoders (PanelChrome module) turn the 184,696-byte state snapshot into
// objects. Export names in that bundle are minified, so decoders are located by behaviour,
// not by name — a cambc upgrade that renames them still works, one that changes them fails loudly.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const cache = path.join(here, '.cache');
const pick = (re) => fs.existsSync(cache) && fs.readdirSync(cache).find((f) => re.test(f));

export function ensureAssets(anyReplay) {
  if (pick(/^PanelChrome-.*\.js$/) && pick(/^v0_0_1-.*\.wasm$/)) return;
  execFileSync(path.join(here, 'assets.sh'), [anyReplay], { stdio: 'inherit' });
}

export async function openEngine() {
  const { WASI } = await import('node:wasi');
  const PC = await import(pathToFileURL(path.join(cache, pick(/^PanelChrome-.*\.js$/))).href);
  const wasi = new WASI({ version: 'preview1', args: [], env: {}, returnOnExit: true });
  const mod = await WebAssembly.compile(fs.readFileSync(path.join(cache, pick(/^v0_0_1-.*\.wasm$/))));
  const inst = await WebAssembly.instantiate(mod, { wasi_snapshot_preview1: wasi.wasiImport });
  wasi.initialize(inst);
  const X = inst.exports;
  const view = () => new DataView(X.memory.buffer);
  const outBuf = X.Engine_Malloc(64), args = X.Engine_Malloc(128);
  const cstr = (p) => { const b = new Uint8Array(X.memory.buffer); let e = p; while (b[e]) e++; return Buffer.from(b.subarray(p, e)).toString(); };
  const check = (rc) => { if (rc !== 0) throw new Error(`engine error ${rc}: ${cstr(X.GetLastErrorMsg())}`); };
  const vals = Object.values(PC);
  const spectator = vals.find((v) => v && typeof v === 'object' && v.team === 2 && v.champion === 0xffffffff);
  const analytics = vals.find((v) => typeof v === 'function' && String(v).includes('Invalid analytics snapshot count'));
  const headerCandidates = vals.filter((v) => typeof v === 'function' && v.length === 2 && /^function \w+\(e,t\)\{return \w+\(e,t,\w+,\d+\)\}$/.test(String(v)));
  if (!spectator || !analytics || !headerCandidates.length) throw new Error('visualiser decoder layout changed: rerun assets.sh or update decode.mjs');
  return { X, view, outBuf, args, check, spectator, analytics, headerCandidates };
}

export function openReplay(E, bytes) {
  const { X, view, outBuf, args, check, spectator, analytics } = E;
  const p = X.Engine_Malloc(bytes.length);
  new Uint8Array(X.memory.buffer, p, bytes.length).set(bytes);
  check(X.Game_FromReplayFileBytes(p, bytes.length, outBuf));
  X.Engine_Free(p);
  const handle = view().getBigUint64(outBuf, true);
  const snap = X.Engine_Malloc(X.Game_Visualiser_SnapshotSize());
  check(X.Game_Runner_SetCheckpointsEnabled(handle, 1));
  check(X.Game_Visualiser_Timeline_GetMain(handle, outBuf));
  const main = view().getBigUint64(outBuf, true);
  check(X.Game_Visualiser_Timeline_Info(handle, main, args, args + 8, args + 16));
  const size = view().getUint32(args, true);
  const persp = () => { const t = args + 48, v = view(); v.setUint32(t, spectator.team, true); v.setUint32(t + 4, spectator.champion, true); return t; };
  const decodeHeader = () => {
    for (const h of E.headerCandidates) {
      try { const hdr = h(view(), snap); if (typeof hdr?.round === 'number' && Array.isArray(hdr?.drafts)) { E.headerCandidates = [h]; return hdr; } } catch {}
    }
    throw new Error('no header decoder matched the snapshot');
  };
  return {
    size,
    seek(pos) { check(X.Game_Visualiser_Timeline_Seek(handle, main, pos, 0)); },
    state() { check(X.Game_Visualiser_ReadState(handle, persp(), snap)); return analytics(view(), snap, decodeHeader()); },
    close() { X.Engine_Free(snap); X.Game_Destroy(handle); },
  };
}

// Platform bundles are .zip and `cambc run -o` bundles are zips named .repl26a (replay plus
// team stdout), so detect the zip signature rather than trusting the extension.
export function replayBytes(file) {
  const head = fs.readFileSync(file).subarray(0, 2).toString('latin1');
  if (head === 'PK') return execFileSync('unzip', ['-p', file, 'replay.repl26a'], { maxBuffer: 1 << 28 });
  return fs.readFileSync(file);
}
