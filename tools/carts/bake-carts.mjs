#!/usr/bin/env node
// PR 4a generator: builds every cart and disc shape Phobos uses into one GLB each (named materials, welded
// geometry, the label as its own primitive), writes the procedural plastic and glitter normal maps as PNGs, turns
// studio.hdr into IBL KTX with Filament's cmgen and compiles the disc material with matc, then writes the manifest.
//
//   FILAMENT_BIN=/path/to/filament-v1.77.3/bin node bake-carts.mjs            write out/
//   FILAMENT_BIN=/path/to/filament-v1.77.3/bin node bake-carts.mjs --check    regenerate everything into
//                                                                             out-verify/ and compare all 25
//                                                                             files byte-for-byte with out/
// FILAMENT_BIN is REQUIRED in both modes: cmgen and matc are part of the bake and the manifest covers their
// outputs (ibl/*, materials/disc.filamat). Without it the run stops instead of producing a partial set.
//
// Outputs land in tools/carts/out/ - deliberately NOT in the app's assets: PR 4b moves and wires them.
// The Cartridge pipeline copy under cartridge/ is verbatim (see PROVENANCE.md); this script only adds the two
// provisional Famicom/Super Famicom specs and drives the builders.
import { register } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';

register(new URL('./hdr-url-loader.mjs', import.meta.url));
await import('./gltf-shim.mjs');
const xmldom = await import('@xmldom/xmldom');
const { DOMParser } = xmldom;
globalThis.DOMParser = DOMParser;
const { installDomShim } = await import('./dom-shim.mjs');
installDomShim(xmldom);

const THREE = await import('three');
const { mergeVertices } = await import('three/addons/utils/BufferGeometryUtils.js');
const { GLTFExporter } = await import('three/addons/exporters/GLTFExporter.js');
const pipe = await import('./cartridge/carts/pipeline.js');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CHECK = process.argv.includes('--check');
const OUT = path.join(HERE, CHECK ? 'out-verify' : 'out');

// Shapes Phobos presents, per report section 2 (Famicom/Super Famicom use the new provisional shells below).
// Cartridge has no PSP disc spec: the UMD is nearest to `minidisc` (both are small caddied discs); documented in
// README.md as the nearest-shape choice. Unmapped disc systems fall back to `cd` at run time.
const CARTS = ['famicom', 'sfc', 'gb', 'gbc', 'gba', 'n64', 'sms', 'md', 'small', 'aes', 'hucard', 'a26', 'pcb'];
const DISCS = ['cd', 'psx', 'minidisc'];
const ENGRAVINGS = [['gba', 'badge'], ['gba', 'lines']];

// PROVISIONAL shells, Hypothesis until measured. Dimensions are approximate public hardware figures: Famicom HVC
// shell 105.5 x 72 x 17 mm, Super Famicom SHVC shell 126 x 77 x 22 mm. The outlines adapt Cartridge's nes/snes
// silhouettes to those rectangles; the label styles reuse Cartridge's nes/snes regions. See README.md (Limits).
const PROVISIONAL = {
  famicom: {
    w: 105.5, h: 72, t: 17, color: '#8c2f2f',
    outline: [[0, 1.6, 2.5], [3.6, 0, 0.8], [101.9, 0, 0.8], [105.5, 1.6, 0.8],
      [105.5, 70.4, 2.5], [101.9, 72, 2.5], [3.6, 72, 2.5], [0, 70.4, 2.5]],
    recess: { x: 44, y: 2, w: 17.5, h: 64, r: 1.2, depth: 0.6 },
    labelStyle: 'nes', wells: [], ridges: [],
  },
  sfc: {
    w: 126, h: 77, t: 22, color: '#a3a4aa',
    outline: [[0, 1.6, 2.5], [18.6, 1.6, 0.4], [19.4, 0, 0.8], [106.6, 0, 0.8], [107.4, 1.6, 0.4],
      [126, 1.6, 2.5], [126, 77, 2], [0, 77, 2]],
    recess: { x: 23, y: 0.7, w: 80, h: 33.5, r: 2, depth: 0.8 },
    labelStyle: 'snes', frontScrews: [[10.2, 70.8], [115.8, 70.8]], bevel: 1.2,
  },
};
for (const [key, spec] of Object.entries(PROVISIONAL)) if (!pipe.CART_SPECS[key]) pipe.CART_SPECS[key] = spec;

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const fileSha = (p) => sha256(fs.readFileSync(p));
const weld = (g) => {
  if (!g) return null;
  try { return mergeVertices(g, 1e-4); } catch { return g; }
};
const mat = (name, opts = {}) => new THREE.MeshStandardMaterial({ name, roughness: 0.55, metalness: 0, ...opts });

function groupFor(kind, shape, engraving) {
  const b = kind === 'disc' ? pipe.buildDiscGeometry(shape) : pipe.buildCartGeometry(shape, engraving);
  const group = new THREE.Group();
  group.name = shape + (engraving && engraving !== 'none' ? '-' + engraving : '');
  const parts = kind === 'disc'
    ? [['plastic', 'disc', {}], ['hub', 'hub', { roughness: 0.4 }], ['stack', 'stack', {}]]
    : [['plastic', 'shell', {}], ['dark', 'dark', { roughness: 0.7 }], ['metal', 'metal', { metalness: 1, roughness: 0.35 }],
      ['screws', 'screws', { metalness: 1, roughness: 0.3 }], ['pcb', 'pcb', { roughness: 0.8 }],
      ['glass', 'glass', { transparent: true, opacity: 0.35, roughness: 0.1 }],
      ['window', 'window', { transparent: true, opacity: 0.25, roughness: 0.1 }]];
  for (const [key, name, opts] of parts) {
    const geo = weld(b[key]);
    if (!geo) continue;
    const mesh = new THREE.Mesh(geo, mat(name, opts));
    mesh.name = name;
    group.add(mesh);
  }
  if (b.label && b.label.geo) {
    const mesh = new THREE.Mesh(weld(b.label.geo), mat('label', { roughness: 0.65 }));
    mesh.name = 'label';
    group.add(mesh);
  }
  return group;
}

const exporter = new GLTFExporter();
const toGLB = (group) => new Promise((resolve, reject) =>
  exporter.parse(group, (ab) => resolve(Buffer.from(ab)), reject, { binary: true }));

function writeFile(rel, buf) {
  const p = path.join(OUT, rel);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, buf);
  return { path: rel, bytes: buf.length, sha256: sha256(buf) };
}

// The procedural normal maps, ported from Cartridge's pipeline.js (getPlasticNormalMap / getGlitterNormalMap) so
// the bytes match what the shipped renderer drew at run time. Pure functions of fixed seeds.
function plasticNormalRGBA() {
  const n = 128, noise = new Float32Array(n * n);
  let t = 7;
  const rnd = () => ((t = (t * 16807) % 2147483647), t / 2147483647);
  for (let i = 0; i < n * n; i++) noise[i] = rnd();
  const blur = (src) => {
    const out = new Float32Array(n * n);
    for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
      let acc = 0;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) acc += src[((y + dy + n) % n) * n + ((x + dx + n) % n)];
      out[y * n + x] = acc / 9;
    }
    return out;
  };
  const s = blur(blur(noise));
  const out = new Uint8Array(n * n * 4);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const dx = (s[y * n + ((x + 1) % n)] - s[y * n + ((x - 1 + n) % n)]) * 6;
    const dy = (s[((y + 1) % n) * n + x] - s[((y - 1 + n) % n) * n + x]) * 6;
    const len = Math.hypot(dx, dy, 1);
    const o = (y * n + x) * 4;
    out[o] = ((-dx / len) * 0.5 + 0.5) * 255;
    out[o + 1] = ((-dy / len) * 0.5 + 0.5) * 255;
    out[o + 2] = ((1 / len) * 0.5 + 0.5) * 255;
    out[o + 3] = 255;
  }
  return { size: n, data: out };
}

function glitterNormalRGBA() {
  const n = 256, out = new Uint8Array(n * n * 4);
  let t = 3;
  const rnd = () => ((t = (t * 16807) % 2147483647), t / 2147483647);
  for (let i = 0; i < n * n; i++) { out[i * 4] = 128; out[i * 4 + 1] = 128; out[i * 4 + 2] = 255; out[i * 4 + 3] = 255; }
  for (let k = 0; k < 2600; k++) {
    const x = Math.floor(rnd() * n), y = Math.floor(rnd() * n);
    const dx = rnd() * 2 - 1, dy = rnd() * 2 - 1;
    const r = rnd() < 0.3 ? 2 : 1;
    for (let oy = -r; oy < r; oy++) for (let ox = -r; ox < r; ox++) {
      const o = (((y + oy + n) % n) * n + ((x + ox + n) % n)) * 4;
      out[o] = (dx * 0.5 + 0.5) * 255;
      out[o + 1] = (dy * 0.5 + 0.5) * 255;
      out[o + 2] = 190;
    }
  }
  return { size: n, data: out };
}

const png = ({ size, data }) => {
  const img = new PNG({ width: size, height: size });
  img.data = Buffer.from(data);
  return PNG.sync.write(img, { colorType: 6, inputHasAlpha: true });
};

// The 4a review's blocking finding: Cartridge's buildEngravingGeometry has an empty catch, and the missing
// xmldom querySelectorAll made gba-badge/gba-lines differ from gba only by the root node name. Assert geometry,
// not file names: each engraving must add vertices over the base and have its own BIN chunk.
function glbParts(buf) {
  let o = 12;
  const parts = {};
  while (o < buf.length) {
    const len = buf.readUInt32LE(o), type = buf.toString('ascii', o + 4, o + 8);
    parts[type] = buf.subarray(o + 8, o + 8 + len);
    o += 8 + len;
  }
  return parts;
}
function engravingStats(buf) {
  const parts = glbParts(buf);
  const gltf = JSON.parse(parts.JSON.toString('utf8'));
  let verts = 0;
  for (const mesh of gltf.meshes ?? []) for (const prim of mesh.primitives ?? []) {
    verts += gltf.accessors?.[prim.attributes?.POSITION]?.count ?? 0;
  }
  return { verts, bin: sha256(parts['BIN\0'] ?? Buffer.alloc(0)) };
}
function verifyEngravings() {
  const base = engravingStats(fs.readFileSync(path.join(OUT, 'carts/gba.glb')));
  const stats = [];
  for (const [shape, eng] of ENGRAVINGS) {
    const s = engravingStats(fs.readFileSync(path.join(OUT, `carts/${shape}-${eng}.glb`)));
    if (s.verts <= base.verts) {
      throw new Error(`STOP: ${shape}-${eng} has ${s.verts} vertices, not more than ${shape}'s ${base.verts} - `
        + 'the engraving geometry is missing (the empty catch in buildEngravingGeometry hid it)');
    }
    if (s.bin === base.bin) throw new Error(`STOP: ${shape}-${eng}'s BIN equals ${shape}'s - geometry is identical`);
    stats.push(s);
  }
  if (stats.length > 1 && stats[0].bin === stats[1].bin) {
    throw new Error('STOP: the engraving variants have identical geometry');
  }
  console.log(`engraving check: gba ${base.verts} vertices; `
    + ENGRAVINGS.map(([shape, eng], i) => `${shape}-${eng} ${stats[i].verts}`).join('; ') + ' - all differ');
}

async function main() {
  const bin = process.env.FILAMENT_BIN;
  if (!bin) {
    console.error('STOP: FILAMENT_BIN must point to the filament-v1.77.3/bin directory - cmgen and matc are part '
      + 'of the bake and their outputs are in the manifest (see README)');
    process.exit(2);
  }
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  const outputs = [];

  for (const shape of CARTS) {
    const group = groupFor('cart', shape, 'none');
    outputs.push(writeFile(`carts/${shape}.glb`, await toGLB(group)));
  }
  for (const [shape, engraving] of ENGRAVINGS) {
    outputs.push(writeFile(`carts/${shape}-${engraving}.glb`, await toGLB(groupFor('cart', shape, engraving))));
  }
  verifyEngravings();
  for (const shape of DISCS) outputs.push(writeFile(`discs/${shape}.glb`, await toGLB(groupFor('disc', shape))));

  outputs.push(writeFile('textures/plastic-normal.png', png(plasticNormalRGBA())));
  outputs.push(writeFile('textures/glitter-normal.png', png(glitterNormalRGBA())));

  // cmgen names its files after the deploy folder, so deploy into a folder literally named "ibl" under the
  // worktree (never /tmp): ibl_ibl.ktx, ibl_skybox.ktx and sh.txt come out with stable names.
  const tmpRoot = path.join(HERE, '.tmp');
  const tmp = path.join(tmpRoot, 'ibl');
  fs.rmSync(tmpRoot, { recursive: true, force: true });
  fs.mkdirSync(tmp, { recursive: true });
  try {
    execFileSync(path.join(bin, 'cmgen'), ['--size=256', '--format=ktx', '--deploy', tmp,
      path.join(HERE, 'cartridge/assets/studio.hdr')], { stdio: 'inherit' });
    for (const f of fs.readdirSync(tmp)) outputs.push(writeFile(`ibl/${f}`, fs.readFileSync(path.join(tmp, f))));
  } finally { fs.rmSync(tmpRoot, { recursive: true, force: true }); }
  const filamat = path.join(OUT, 'materials/disc.filamat');
  fs.mkdirSync(path.dirname(filamat), { recursive: true });
  execFileSync(path.join(bin, 'matc'), ['-a', 'opengl', '-p', 'mobile', '-S', '-o', filamat, path.join(HERE, 'disc.mat')], { stdio: 'inherit' });
  const buf = fs.readFileSync(filamat);
  outputs.push({ path: 'materials/disc.filamat', bytes: buf.length, sha256: sha256(buf) });

  const inputs = {
    'cartridge/carts/pipeline.js': fileSha(path.join(HERE, 'cartridge/carts/pipeline.js')),
    'cartridge/shared/logo.js': fileSha(path.join(HERE, 'cartridge/shared/logo.js')),
    'cartridge/assets/studio.hdr': fileSha(path.join(HERE, 'cartridge/assets/studio.hdr')),
    'bake-carts.mjs': fileSha(fileURLToPath(import.meta.url)),
    'dom-shim.mjs': fileSha(path.join(HERE, 'dom-shim.mjs')),
    'gltf-shim.mjs': fileSha(path.join(HERE, 'gltf-shim.mjs')),
    'hdr-url-loader.mjs': fileSha(path.join(HERE, 'hdr-url-loader.mjs')),
    'disc.mat': fileSha(path.join(HERE, 'disc.mat')),
    'package.json': fileSha(path.join(HERE, 'package.json')),
    'package-lock.json': fileSha(path.join(HERE, 'package-lock.json')),
    '.nvmrc': fileSha(path.join(HERE, '.nvmrc')),
    'npm-evidence.txt': fileSha(path.join(HERE, 'npm-evidence.txt')),
    filamentTgzSha256: '6d3391197dcbdec318dd8a7f4e8d6fd0766794231d53c756a1565fd92bba31b2',
    cmgen: fileSha(path.join(bin, 'cmgen')),
    matc: fileSha(path.join(bin, 'matc')),
    node: process.version,
  };
  const byBytes = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
  const manifest = { generatedBy: 'tools/carts/bake-carts.mjs', shapes: { carts: CARTS, discs: DISCS, engravings: ENGRAVINGS }, inputs, outputs: outputs.sort((a, b) => byBytes(a.path, b.path)) };
  fs.writeFileSync(path.join(OUT, 'MANIFEST.json'), JSON.stringify(manifest, null, 2) + '\n');
  console.log(`baked ${outputs.length} outputs, ${outputs.reduce((s, o) => s + o.bytes, 0)} bytes`);

  if (CHECK) {
    const ref = path.join(HERE, 'out');
    const walk = (base, d) => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) =>
      e.isDirectory() ? walk(base, path.join(d, e.name)) : [path.relative(base, path.join(d, e.name))]);
    const mine = new Set(walk(OUT, OUT));
    const refs = new Set(walk(ref, ref));
    let bad = 0;
    for (const rel of new Set([...mine, ...refs])) {
      const a = path.join(OUT, rel), b = path.join(ref, rel);
      if (!fs.existsSync(a)) { console.error(`MISSING from the regeneration: ${rel}`); bad++; continue; }
      if (!fs.existsSync(b)) { console.error(`EXTRA against out/: ${rel}`); bad++; continue; }
      if (!fs.readFileSync(a).equals(fs.readFileSync(b))) { console.error(`DIFFERS ${rel}`); bad++; }
    }
    if (bad === 0) { console.log('CHECK OK: every output is byte-identical to out/'); fs.rmSync(OUT, { recursive: true, force: true }); }
    else { console.error(`CHECK FAILED (${bad} difference(s)); out-verify/ kept for inspection (git-ignored)`); process.exitCode = 1; }
  }
}

await main();
