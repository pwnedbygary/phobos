// Fixtures for the app-side label ports (art/LabelMatch.kt, art/LabelCrop.kt).
//
// Sources: the verbatim Cartridge copies in ./cartridge/ (scraper.js, labelcrop.js, titles.js), all hashed in
// tools/cart-fixtures/README.md, and the ONE bundled label index at android/app/src/main/assets/carts/labels.json.gz
// (1,921,198 B, sha256 f7f1e8bb...). Nothing is fetched.
//
//   node tools/cart-fixtures/make-label-fixtures.mjs
//
// Writes android/app/src/test/resources/carts/labelcrop-fixtures.json and .../labelmatch-fixtures.json.
import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const here = dirname(fileURLToPath(import.meta.url));
const { Scraper, labelShape } = require('./cartridge/scraper.js');
const { labelRect, SPECS } = require('./cartridge/labelcrop.js');
const { matchKey } = require('./cartridge/titles.js');

const outDir = join(here, '../../android/app/src/test/resources/carts');
mkdirSync(outDir, { recursive: true });

// ---- label crop: synthetic bitmaps, deterministic in JS and Kotlin ----
function bitmap(W, H, bg, rect, fg) {
  const bmp = Buffer.alloc(W * H * 4);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 4;
      const inside = x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3];
      const c = inside ? fg : bg;
      bmp[i] = c[0]; bmp[i + 1] = c[1]; bmp[i + 2] = c[2]; bmp[i + 3] = c[3];
    }
  }
  return bmp;
}

const CROP_CASES = [
  { shape: 'gb', w: 60, h: 90, bg: [40, 40, 40, 255], rect: [8, 26, 52, 66], fg: [190, 190, 190, 255] },
  { shape: 'gbc', w: 60, h: 90, bg: [40, 40, 40, 255], rect: [8, 26, 52, 66], fg: [200, 180, 120, 255] },
  { shape: 'gba', w: 120, h: 70, bg: [30, 30, 30, 255], rect: [16, 16, 104, 56], fg: [210, 210, 210, 255] },
  { shape: 'gb', w: 60, h: 60, bg: [40, 40, 40, 255], rect: [4, 4, 56, 56], fg: [190, 190, 190, 255] },
  { shape: 'snes', w: 100, h: 70, bg: [35, 35, 35, 255], rect: [20, 8, 80, 60], fg: [180, 180, 180, 255] },
  { shape: 'nes', w: 100, h: 90, bg: [35, 35, 35, 255], rect: [30, 10, 80, 70], fg: [180, 180, 180, 255] },
  { shape: 'disc', w: 100, h: 100, bg: [30, 30, 30, 255], rect: [20, 30, 70, 80], fg: [180, 180, 180, 255] },
  { shape: 'gb', w: 60, h: 90, bg: [40, 40, 40, 0], rect: [8, 26, 52, 66], fg: [190, 190, 190, 255] },
  { shape: '3ds', w: 90, h: 80, bg: [40, 40, 40, 255], rect: [8, 4, 82, 70], fg: [190, 190, 190, 255] },
  { shape: 'switch', w: 120, h: 70, bg: [40, 40, 40, 255], rect: [10, 6, 110, 60], fg: [190, 190, 190, 255] },
];

const cropCases = CROP_CASES.map((c) => {
  const bmp = bitmap(c.w, c.h, c.bg, c.rect, c.fg);
  const expected = labelRect(bmp, c.w, c.h, c.shape);
  return { ...c, expected };
});
writeFileSync(join(outDir, 'labelcrop-fixtures.json'), JSON.stringify({ specs: SPECS, cases: cropCases }, null, 2) + '\n');

// ---- label match: cases drawn from the real bundled index ----
const scraper = new Scraper({ mediaDir: outDir, cacheDir: outDir, fetch: async () => { throw new Error('no fetch'); }, nativeImage: null });
const real = JSON.parse(gunzipSync(readFileSync(join(here, '../../android/app/src/main/assets/carts/labels.json.gz'))).toString('utf8'));
scraper.labelData = real; // the one bundled copy is the source; nothing is read from ./cartridge/data
scraper.labelKeys = {}; // the per-system fuzzy cache the loader would have initialised
if (!real || !real.base) throw new Error('index failed to load');

const shapes = { gb: 'gb', gbc: 'gbc', gba: 'gba', n64: 'n64', snes: 'snes', nes: 'nes', ds: 'ds', switch: 'switch', psx: 'disc', ps2: 'disc', gc: 'disc' };
const systems = Object.keys(real.index).filter((id) => shapes[id] && Object.keys(real.index[id]).length > 0);
if (systems.length < 2) throw new Error('not enough systems in the index');

// Deterministic picks: the first usable cart system, a second one, and a disc system if present.
const cartSystems = systems.filter((id) => shapes[id] !== 'disc');
const discSystems = systems.filter((id) => shapes[id] === 'disc');
const picked = [];
if (cartSystems[0]) picked.push(cartSystems[0]);
if (cartSystems[1]) picked.push(cartSystems[1]);
if (discSystems[0]) picked.push(discSystems[0]);

const subset = {};
const matchCases = [];
const usedKeys = new Set();

function addKey(sysId, key) {
  if (!usedKeys.has(sysId + '\n' + key)) {
    usedKeys.add(sysId + '\n' + key);
    (subset[sysId] ??= {})[key] = real.index[sysId][key];
  }
}

function sysObj(id) {
  const shape = shapes[id];
  return shape === 'disc' ? { id, media: { type: 'disc' } } : { id, media: { shape } };
}

function bigrams(s) {
  const out = new Set();
  for (let i = 0; i < s.length - 1; i++) out.add(s.slice(i, i + 2));
  return out;
}
function dice(a, b) {
  if (!a.size || !b.size) return 0;
  let shared = 0;
  for (const g of a) if (b.has(g)) shared++;
  return (2 * shared) / (a.size + b.size);
}

/** The key scraper.matchLabel actually uses for this file: the exact matchKey, else its fuzzy winner. */
function matchedKey(sysId, file) {
  const idx = real.index[sysId];
  const k = matchKey(file);
  if (!k) return null;
  if (idx[k]) return k;
  const g = bigrams(k);
  let best = null;
  let bestScore = 0;
  for (const key of Object.keys(idx)) {
    if (Math.abs(key.length - k.length) > Math.max(5, k.length * 0.3)) continue;
    const d = dice(g, bigrams(key));
    if (d > bestScore) { bestScore = d; best = key; }
  }
  return best && bestScore >= 0.88 ? best : null;
}

function record(sysId, file, region, label) {
  const sys = sysObj(sysId);
  const hit = scraper.matchLabel(sys, file, region);
  const shape = labelShape(sys);
  if (hit) {
    const key = matchedKey(sysId, file);
    if (!key) throw new Error(`matched but no key for ${file}`);
    addKey(sysId, key);
  }
  matchCases.push({ label, system: sys, file, region, expected: hit ? { urls: hit.urls, shape: hit.shape } : null });
}

let n = 0;
for (const sysId of picked) {
  const keys = Object.keys(real.index[sysId]).slice(0, 40);
  // Prefer keys that can be their own file names (matchKey(key) === key), stable choice.
  const candidates = keys.filter((k) => k.length > 3 && matchKey(k) === k).slice(0, 2);
  for (const key of candidates) {
    record(sysId, key, 'US', `${sysId}-${n++}`);
    record(sysId, key, 'JP', `${sysId}-${n++}-jp`);
  }
  if (candidates[0]) {
    // Fuzzy: an extra letter keeps the key out of the exact map but close in bigrams.
    const q = candidates[0] + ' X';
    const hit = scraper.matchLabel(sysObj(sysId), q, 'US');
    if (hit) record(sysId, q, 'US', `${sysId}-${n++}-fuzzy`);
  }
}

// A game no key can match, and a system absent from the index (if one exists here).
matchCases.push({ label: 'no-match', system: { id: picked[0], media: { shape: shapes[picked[0]] } }, file: 'Not A Real Game (USA)', region: 'US', expected: null });
const absent = 'gbc2';
if (!real.index[absent] && shapes.gbc) {
  matchCases.push({ label: 'no-system-in-index', system: { id: absent, media: { shape: 'gbc' } }, file: 'Any (USA)', region: 'US', expected: null });
}

writeFileSync(join(outDir, 'labelmatch-fixtures.json'),
  JSON.stringify({ base: real.base, index: subset, cases: matchCases }, null, 2) + '\n');
console.log(`labelcrop: ${cropCases.length} cases; labelmatch: ${matchCases.length} cases over ${Object.keys(subset).length} systems`);
console.log(`index base: ${real.base}`);
