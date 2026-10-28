'use strict';
// Artwork from the libretro-thumbnails project (no account or API key).
// Directory indexes are cached, names matched exactly → normalised → fuzzy.
const fs = require('fs');
const fsp = fs.promises;
const path = require('path');
const zlib = require('zlib');
const { matchKey, parseTags } = require('./titles');
const { labelRect, isRoundScan, REGIONS: LABEL_REGIONS, SPECS: LABEL_SPECS } = require('./labelcrop');

const BASE = 'https://thumbnails.libretro.com';
const KINDS = {
  box: { dir: 'Named_Boxarts', width: 520, format: 'jpg' },
  snap: { dir: 'Named_Snaps', width: 640, format: 'jpg' },
  title: { dir: 'Named_Titles', width: 640, format: 'jpg' },
  logo: { dir: 'Named_Logos', width: 480, format: 'png' },
  // Real cartridge / disc label scans (LaunchBox Games DB), cropped to the sticker.
  label: { width: 1024, format: 'jpg' },
};

// Which model a system's label scans are cut for ('disc' = whole printed face).
function labelShape(sys) {
  const m = sys.media || {};
  if (m.type === 'disc') return 'disc';
  const k = sys.id === 'gbc' ? 'gbc' : m.shape;
  return LABEL_SPECS[k] ? k : null;
}

// LaunchBox region codes in order of preference for a game's own region.
const REGION_ORDER = {
  US: ['US', 'W', 'AU', 'EU', '', 'JP'], EU: ['EU', 'AU', 'W', 'US', '', 'JP'], JP: ['JP', 'AS', 'W', 'US', '', 'EU'],
  W: ['W', 'US', 'EU', 'AU', '', 'JP'],
};
const INDEX_TTL = 14 * 24 * 3600 * 1000;
const RETRY_MISS_AFTER = 7 * 24 * 3600 * 1000;
// Errors that mean the picture really isn't there (or can't be used). Anything
// else (timeouts, rate limits, no connection) is tried again next time
// instead of being remembered as missing.
const isGone = (e) => /\b(404|410)\b|not an image|does not match|decode/i.test(String((e && e.message) || e));
const ARCADE_DAT = 'https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/fbneo-split/FBNeo%20-%20Arcade%20Games.dat';

function sanitize(name) {
  return name.replace(/[&*/:`<>?\\|"]/g, '_');
}

function bigrams(s) {
  const out = new Set();
  for (let i = 0; i < s.length - 1; i++) out.add(s.slice(i, i + 2));
  return out;
}

function dice(a, b) {
  if (!a.size || !b.size) return 0;
  let n = 0;
  for (const x of a) if (b.has(x)) n++;
  return (2 * n) / (a.size + b.size);
}

class Scraper {
  constructor({ mediaDir, cacheDir, fetch, nativeImage, log = () => {} }) {
    this.mediaDir = mediaDir;
    this.cacheDir = cacheDir;
    this.fetch = fetch;
    this.nativeImage = nativeImage;
    this.log = log;
    this.indexes = new Map();
    this.cancelled = false;
    this.arcadeNames = null;
  }

  labels() {
    if (this.labelData === undefined) {
      try {
        const raw = zlib.gunzipSync(fs.readFileSync(path.join(__dirname, 'data', 'labels.json.gz')));
        this.labelData = JSON.parse(raw.toString('utf8'));
        this.labelKeys = {};
      } catch (e) {
        this.log(`label index failed: ${e.message}`);
        this.labelData = null;
      }
    }
    return this.labelData;
  }

  // Best label scan for a game: exact title key, then fuzzy; real scans
  // before fan-made ones; the game's own region first.
  matchLabel(sys, base, region) {
    const data = this.labels();
    const idx = data && data.index[sys.id];
    const shape = labelShape(sys);
    if (!idx || !shape) return null;
    const k = matchKey(base);
    if (!k) return null;
    let list = idx[k];
    if (!list) {
      if (!this.labelKeys[sys.id]) this.labelKeys[sys.id] = Object.keys(idx).map((key) => [key, bigrams(key)]);
      const g = bigrams(k);
      let best = null;
      let bestScore = 0;
      for (const [key, kg] of this.labelKeys[sys.id]) {
        if (Math.abs(key.length - k.length) > Math.max(5, k.length * 0.3)) continue;
        const d = dice(g, kg);
        if (d > bestScore) { bestScore = d; best = key; }
      }
      if (best && bestScore >= 0.88) list = idx[best];
    }
    if (!list) return null;
    const allowed = LABEL_REGIONS[shape];
    const order = REGION_ORDER[region] || REGION_ORDER.US;
    const wantType = shape === 'disc' || shape === 'umd' ? 'd' : 'c';
    const scored = list
      .filter(([, r, t]) => (!allowed || allowed.includes(r)) && (t === wantType || t === `f${wantType}`))
      .map(([file, r, t]) => {
        const ri = order.indexOf(r);
        return { file, score: (t === wantType ? 0 : 100) + (ri < 0 ? 50 : ri * 10) };
      })
      .sort((a, b) => a.score - b.score);
    return scored.length ? { urls: scored.slice(0, 3).map((x) => data.base + x.file), shape } : null;
  }

  async downloadLabel(url, dest, shape) {
    const res = await this.fetch(url);
    if (!res.ok) throw new Error(String(res.status));
    const buf = Buffer.from(await res.arrayBuffer());
    const NI = this.nativeImage;
    if (!NI) throw new Error('no image support');
    let img = NI.createFromBuffer(buf);
    if (img.isEmpty()) throw new Error('not an image');
    if (img.getSize().width > 1400) img = img.resize({ width: 1400, quality: 'best' });
    const { width: W, height: H } = img.getSize();
    const bmp = img.toBitmap();
    const r = labelRect(bmp, W, H, shape);
    if (!r) throw new Error('scan does not match the cart shape');
    // Copy rows by hand (nativeImage.crop is unreliable off the main display).
    const out = Buffer.alloc(r.w * r.h * 4);
    for (let y = 0; y < r.h; y++) bmp.copy(out, y * r.w * 4, ((r.y + y) * W + r.x) * 4, ((r.y + y) * W + r.x + r.w) * 4);
    // UMD scans that show only the disc: cut it round (transparent outside)
    // so Cartridge can seat it in the caddy's window.
    const round = shape === 'umd' && isRoundScan(bmp, W, H, r);
    if (round) {
      const cx = r.w / 2; const cy = r.h / 2; const rad = Math.min(r.w, r.h) / 2;
      for (let y = 0; y < r.h; y++) {
        for (let x = 0; x < r.w; x++) {
          if ((x - cx) ** 2 + (y - cy) ** 2 > rad * rad) out[(y * r.w + x) * 4 + 3] = 0;
        }
      }
    } else {
      for (let i = 3; i < out.length; i += 4) out[i] = 255;
    }
    let label = NI.createFromBitmap(out, { width: r.w, height: r.h });
    const max = KINDS.label.width;
    if (r.w > max || r.h > max) label = label.resize(r.w >= r.h ? { width: max, quality: 'best' } : { height: max, quality: 'best' });
    const file = `${dest}.${round ? 'png' : 'jpg'}`;
    await fsp.mkdir(path.dirname(file), { recursive: true });
    await fsp.writeFile(file, round ? label.toPNG() : label.toJPEG(88));
    return file;
  }

  index(system, kind) {
    const key = `${system}__${kind}`;
    if (!this.indexes.has(key)) this.indexes.set(key, this.loadIndex(system, kind, key));
    return this.indexes.get(key);
  }

  async loadIndex(system, kind, key) {
    const file = path.join(this.cacheDir, 'thumbindex', `${key.replace(/[^a-z0-9_-]+/gi, '_')}.json`);
    let names = null;
    try {
      const st = await fsp.stat(file);
      if (Date.now() - st.mtimeMs < INDEX_TTL) names = JSON.parse(await fsp.readFile(file, 'utf8'));
    } catch (_) { /* not cached */ }

    if (!names) {
      const url = `${BASE}/${encodeURIComponent(system)}/${KINDS[kind].dir}/`;
      try {
        const res = await this.fetch(url);
        if (!res.ok) throw new Error(String(res.status));
        const html = await res.text();
        names = [];
        for (const m of html.matchAll(/href="([^"?/][^"]*?)\.png"/g)) {
          try { names.push(decodeURIComponent(m[1])); } catch (_) { /* skip */ }
        }
        await fsp.mkdir(path.dirname(file), { recursive: true });
        await fsp.writeFile(file, JSON.stringify(names));
      } catch (e) {
        this.log(`index ${system}/${kind} failed: ${e.message}`);
        if (isGone(e)) {
          // The folder really isn't there: remember that (for INDEX_TTL) like any other list.
          names = [];
          await fsp.mkdir(path.dirname(file), { recursive: true }).catch(() => {});
          await fsp.writeFile(file, '[]').catch(() => {});
        } else {
          // Try the list again in a minute; until then no game counts as missing.
          setTimeout(() => this.indexes.delete(key), 60 * 1000);
          return { names: [], exact: new Set(), byKey: new Map(), grams: null, failed: true };
        }
      }
    }

    const exact = new Set(names);
    const byKey = new Map();
    for (const n of names) {
      const k = matchKey(n);
      if (!byKey.has(k)) byKey.set(k, []);
      byKey.get(k).push(n);
    }
    return { names, exact, byKey, grams: null };
  }

  pickBest(cands, regionPref) {
    const score = (n) => {
      const tags = [...n.matchAll(/\(([^)]*)\)/g)].map((m) => m[1]);
      const info = parseTags(tags);
      const r = regionPref.indexOf(info.region);
      return (r < 0 ? 50 : r * 10) + info.flags.length * 3 + tags.length;
    };
    return [...cands].sort((a, b) => score(a) - score(b))[0];
  }

  match(idx, base, regionPref) {
    if (!idx.names.length) return null;
    const s = sanitize(base);
    if (idx.exact.has(s)) return s;
    const k = matchKey(base);
    if (!k) return null;
    const direct = idx.byKey.get(k);
    if (direct) return this.pickBest(direct, regionPref);

    if (!idx.grams) idx.grams = [...idx.byKey.keys()].map((key) => [key, bigrams(key)]);
    const g = bigrams(k);
    let best = null;
    let bestScore = 0;
    for (const [key, kg] of idx.grams) {
      if (Math.abs(key.length - k.length) > Math.max(6, k.length * 0.4)) continue;
      const d = dice(g, kg);
      if (d > bestScore) { bestScore = d; best = key; }
    }
    if (best && bestScore >= 0.84) return this.pickBest(idx.byKey.get(best), regionPref);
    return null;
  }

  async arcadeMap() {
    if (this.arcadeNames) return this.arcadeNames;
    const file = path.join(this.cacheDir, 'fbneo-names.json');
    try {
      const st = await fsp.stat(file);
      if (Date.now() - st.mtimeMs < INDEX_TTL * 4) {
        this.arcadeNames = JSON.parse(await fsp.readFile(file, 'utf8'));
        return this.arcadeNames;
      }
    } catch (_) { /* fetch */ }
    const map = {};
    try {
      const res = await this.fetch(ARCADE_DAT);
      const txt = await res.text();
      const re = /game \(\s*name "([^"]+)"(?:\s*releaseyear "(\d+)")?(?:\s*publisher "([^"]*)")?[\s\S]*?rom \( name (\S+?)\.zip/g;
      let m;
      while ((m = re.exec(txt))) map[m[4].toLowerCase()] = { name: m[1], year: m[2] || null, publisher: m[3] || null };
      await fsp.mkdir(path.dirname(file), { recursive: true });
      await fsp.writeFile(file, JSON.stringify(map));
    } catch (e) {
      this.log(`arcade dat failed: ${e.message}`);
    }
    this.arcadeNames = map;
    return map;
  }

  async download(url, dest, kind) {
    const res = await this.fetch(url);
    if (!res.ok) throw new Error(String(res.status));
    const buf = Buffer.from(await res.arrayBuffer());
    const spec = KINDS[kind];
    let out = buf;
    let color = null;
    if (this.nativeImage) {
      let img = this.nativeImage.createFromBuffer(buf);
      if (!img.isEmpty()) {
        const { width } = img.getSize();
        if (width > spec.width) img = img.resize({ width: spec.width, quality: 'good' });
        out = spec.format === 'jpg' ? img.toJPEG(86) : img.toPNG();
        if (kind === 'box') color = dominantColor(img, this.nativeImage);
      }
    }
    await fsp.mkdir(path.dirname(dest), { recursive: true });
    await fsp.writeFile(dest, out);
    return color;
  }

  // Fetch missing artwork for games. onGame(id, patch) receives updates.
  // retry: also try games that came up empty before (when you ask for it).
  async run(games, { kinds = ['box', 'snap', 'logo'], regionPref = ['US', 'W', 'EU', 'JP'], force = false, retry = false,
    systems, onProgress = () => {}, onGame = () => {} }) {
    this.cancelled = false;
    const jobs = games.filter((g) => systems[g.system] && (systems[g.system].thumbs || (kinds.includes('label') && labelShape(systems[g.system]))));
    let done = 0;
    const total = jobs.length;
    onProgress({ done, total, running: true });

    const needsArcade = jobs.some((g) => g.system === 'arcade' || g.system === 'neogeo');
    const arcade = needsArcade ? await this.arcadeMap() : {};

    const worker = async () => {
      while (jobs.length && !this.cancelled) {
        const g = jobs.shift();
        const sys = systems[g.system];
        const patch = { art: { ...(g.art || {}) }, artTried: { ...(g.artTried || {}) } };
        let changed = false;

        let lookupBase = g.base;
        if ((g.system === 'arcade' || g.system === 'neogeo') && arcade[g.base.toLowerCase()]) {
          const a = arcade[g.base.toLowerCase()];
          lookupBase = a.name;
          if (g.title !== a.name) {
            const t = a.name.replace(/\s*\([^)]*\)/g, '').replace(/\s+-\s+/, ': ').trim();
            patch.title = t;
            patch.year = a.year;
            patch.publisher = a.publisher;
            changed = true;
          }
        }

        for (const kind of kinds) {
          if (this.cancelled) break;
          if (!force && patch.art[kind]) continue;
          const tried = patch.artTried[kind];
          if (!force && !retry && tried && Date.now() - tried < RETRY_MISS_AFTER) continue;
          let got = false;
          let failed = false;
          if (kind === 'label') {
            const hit = labelShape(sys) ? this.matchLabel(sys, lookupBase, g.region) : null;
            const base = path.join(this.mediaDir, g.system, 'label', g.id);
            for (const url of (hit && hit.urls) || []) {
              try {
                const file = await this.downloadLabel(url, base, hit.shape);
                const rel = path.relative(this.mediaDir, file);
                patch.art.label = `${rel.split(path.sep).join('/')}?v=${Date.now().toString(36)}`;
                got = true;
                changed = true;
                break;
              } catch (e) {
                this.log(`label ${url} skipped: ${e.message}`);
                if (!isGone(e)) failed = true;
              }
            }
            if (!got) { patch.artTried[kind] = failed ? Date.now() - RETRY_MISS_AFTER + 3600e3 : Date.now(); changed = true; }
            continue;
          }
          if (!sys.thumbs) continue;
          const systemNames = [sys.thumbs, ...(sys.thumbsAlt || [])];
          for (const sysName of systemNames) {
            const idx = await this.index(sysName, kind);
            if (idx.failed) { failed = true; continue; }
            const name = this.match(idx, lookupBase, regionPref);
            if (!name) continue;
            const url = `${BASE}/${encodeURIComponent(sysName)}/${KINDS[kind].dir}/${encodeURIComponent(name)}.png`;
            const rel = path.join(g.system, kind, `${g.id}.${KINDS[kind].format}`);
            try {
              const color = await this.download(url, path.join(this.mediaDir, rel), kind);
              patch.art[kind] = `${rel.split(path.sep).join('/')}?v=${Date.now().toString(36)}`;
              if (color) patch.art.color = color;
              got = true;
              changed = true;
              break;
            } catch (e) {
              this.log(`download ${url} failed: ${e.message}`);
              if (!isGone(e)) failed = true;
            }
          }
          if (!got) {
            patch.artTried[kind] = failed ? Date.now() - RETRY_MISS_AFTER + 3600e3 : Date.now();
            changed = true;
          }
        }
        done++;
        if (changed) onGame(g.id, patch);
        onProgress({ done, total, running: true, current: g.title });
      }
    };

    await Promise.all(Array.from({ length: 6 }, worker));
    onProgress({ done, total, running: false });
  }

  cancel() { this.cancelled = true; }
}

// Pick a vivid representative colour from a tiny resample of the cover.
function dominantColor(img) {
  try {
    const small = img.resize({ width: 12, height: 16, quality: 'good' });
    const bmp = small.toBitmap(); // BGRA
    let best = null;
    let bestScore = -1;
    let r = 0; let g = 0; let b = 0; let n = 0;
    for (let i = 0; i < bmp.length; i += 4) {
      const B = bmp[i]; const G = bmp[i + 1]; const R = bmp[i + 2];
      const max = Math.max(R, G, B); const min = Math.min(R, G, B);
      const sat = max === 0 ? 0 : (max - min) / max;
      const lum = (R * 0.3 + G * 0.59 + B * 0.11) / 255;
      const score = sat * (1 - Math.abs(lum - 0.5) * 1.4);
      if (score > bestScore) { bestScore = score; best = [R, G, B]; }
      r += R; g += G; b += B; n++;
    }
    const pick = bestScore > 0.25 ? best : [r / n, g / n, b / n];
    return `#${pick.map((v) => Math.round(v).toString(16).padStart(2, '0')).join('')}`;
  } catch (_) {
    return null;
  }
}

module.exports = { Scraper, KINDS, sanitize, labelShape };
