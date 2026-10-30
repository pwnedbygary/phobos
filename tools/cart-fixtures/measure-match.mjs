// Ballpark for the on-the-fly fuzzy label match (design-note data point), on the REAL bundled index.
//
//   node tools/cart-fixtures/measure-match.mjs
//
// Mirrors LabelMatch.match's fuzzy path: per query, compute bigrams; scan the system's keys inside the
// |len diff| <= max(5, 30%) window; dice >= 0.88. Node numbers only - the Kotlin timing test runs in the booked
// build and its numbers go in the note.
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const data = JSON.parse(gunzipSync(readFileSync(join(here, 'cartridge/data/labels.json.gz'))).toString('utf8'));

const bigrams = (s) => {
  const out = new Set();
  for (let i = 0; i < s.length - 1; i++) out.add(s.slice(i, i + 2));
  return out;
};
const dice = (a, b) => {
  let n = 0;
  for (const x of a) if (b.has(x)) n++;
  return (2 * n) / (a.size + b.size);
};
const mutate = (k) => (k.length < 4 ? k + 'x' : k.slice(0, k.length >> 1) + k.slice((k.length >> 1) + 1));

const systems = ['gb', 'gbc', 'gba', 'nes', 'snes', 'n64', 'genesis', 'psx'].filter((s) => data.index[s]);
const cases = [];
for (const sys of systems) {
  const keys = Object.keys(data.index[sys]);
  for (let i = 0; i < 25 && keys.length; i++) cases.push([sys, mutate(keys[(i * 977) % keys.length])]);
}

const run = () => {
  const times = [];
  for (const [sys, name] of cases) {
    const t0 = process.hrtime.bigint();
    const k = name.replace(/[^a-z0-9]/g, '');
    const g = bigrams(k);
    let best = null;
    let bestScore = 0;
    for (const key of Object.keys(data.index[sys])) {
      if (Math.abs(key.length - k.length) > Math.max(5, k.length * 0.3)) continue;
      const d = dice(g, bigrams(key));
      if (d > bestScore) { bestScore = d; best = key; }
    }
    if (best && bestScore < 0.88) best = null;
    times.push(Number(process.hrtime.bigint() - t0) / 1000);
  }
  times.sort((a, b) => a - b);
  const p = (q) => times[Math.min(times.length - 1, Math.floor((times.length - 1) * q))];
  const total = times.reduce((a, b) => a + b, 0);
  console.log(`perf(node): ${times.length} cards, median ${p(0.5).toFixed(1)} us, p95 ${p(0.95).toFixed(1)} us, ` +
    `screenful(24) ${(total * 24 / times.length).toFixed(0)} us`);
};

run();
run();
