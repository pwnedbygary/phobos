'use strict';
// Cuts the sticker out of a flat scan of a real cartridge (or takes the whole
// printed face of a disc), so the 3D carts wear the actual retail label
// instead of box art. Label positions are fractions of the cart's outline,
// measured from scans; each edge is then snapped to the strongest straight
// edge nearby so slightly different scans still line up.

// [left, top, right, bottom] as fractions of the cart's bounding box.
const SPECS = {
  gb: [0.12, 0.27, 0.885, 0.875],
  gbc: [0.12, 0.27, 0.885, 0.875],
  gba: [0.135, 0.225, 0.868, 0.875],
  ds: [0.09, 0.05, 0.91, 0.89],
  '3ds': [0.08, 0.035, 0.878, 0.888],
  switch: [0.07, 0.06, 0.93, 0.78],
  n64: [0.26, 0.085, 0.735, 0.905],
  snes: [0.19, 0.0, 0.815, 0.448],
  nes: [0.4, 0.0, 0.862, 0.68],
  umd: [0, 0, 1, 1], // the whole front of the caddy
};
// Width / height of each cart's outline, to reject photos, odd special
// editions and scans that include packaging.
const ASPECT = {
  gb: 0.87, gbc: 0.87, gba: 1.734, ds: 0.943, '3ds': 1.0, switch: 0.684, n64: 1.526, snes: 1.579, nes: 0.896, disc: 1, umd: 0.99,
};

// Which scan regions have the same shell as our model (Super Famicom and
// Famicom carts are shaped differently, so their scans don't fit).
const REGIONS = {
  snes: ['US', 'W', ''],
  nes: ['US', 'EU', 'AU', 'W', ''],
};

function luminance(d, i) { return d[i] * 0.11 + d[i + 1] * 0.59 + d[i + 2] * 0.3; }

// Bounding box of the object in the scan: alpha if the scan has it,
// otherwise anything that differs from the corner colour.
function objectBox(bmp, W, H) {
  let transparent = false;
  for (const [x, y] of [[1, 1], [W - 2, 1], [1, H - 2], [W - 2, H - 2]]) {
    if (bmp[(y * W + x) * 4 + 3] < 200) transparent = true;
  }
  const c0 = [bmp[(2 * W + 2) * 4], bmp[(2 * W + 2) * 4 + 1], bmp[(2 * W + 2) * 4 + 2]];
  const inside = (i) => (transparent
    ? bmp[i + 3] > 128
    : Math.abs(bmp[i] - c0[0]) + Math.abs(bmp[i + 1] - c0[1]) + Math.abs(bmp[i + 2] - c0[2]) > 48);
  let x0 = W; let y0 = H; let x1 = -1; let y1 = -1;
  const rowHits = new Int32Array(H);
  const colHits = new Int32Array(W);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      if (inside((y * W + x) * 4)) { rowHits[y]++; colHits[x]++; }
    }
  }
  // Ignore stray specks: a row/column must be at least 2% covered.
  for (let y = 0; y < H; y++) if (rowHits[y] > W * 0.02) { y0 = Math.min(y0, y); y1 = y; }
  for (let x = 0; x < W; x++) if (colHits[x] > H * 0.02) { x0 = Math.min(x0, x); x1 = x; }
  if (x1 < 0 || y1 < 0) return { x: 0, y: 0, w: W, h: H };
  return { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
}

// True when (x, y) is empty background (transparent, or the scan's
// backdrop colour sampled from the image corner).
function isBackground(bmp, W, H, x, y) {
  const i = (Math.max(0, Math.min(H - 1, y)) * W + Math.max(0, Math.min(W - 1, x))) * 4;
  if (bmp[(W + 1) * 4 + 3] < 200) return bmp[i + 3] < 128;
  const c = (2 * W + 2) * 4;
  return Math.abs(bmp[i] - bmp[c]) + Math.abs(bmp[i + 1] - bmp[c + 1]) + Math.abs(bmp[i + 2] - bmp[c + 2]) < 48;
}

// A UMD scan that shows only the round disc (not the caddy around it).
function isRoundScan(bmp, W, H, box) {
  const pts = [[0.05, 0.05], [0.95, 0.05], [0.05, 0.95], [0.95, 0.95]];
  const empty = pts.filter(([fx, fy]) => isBackground(bmp, W, H, Math.round(box.x + fx * box.w), Math.round(box.y + fy * box.h)));
  return empty.length >= 3;
}

// Mean gradient across a vertical (dir 'v') or horizontal line at `pos`,
// sampled over [a, b] along the line.
function lineEdge(bmp, W, H, dir, pos, a, b) {
  let sum = 0;
  let n = 0;
  if (dir === 'v') {
    const x = Math.max(1, Math.min(W - 2, pos));
    for (let y = Math.max(0, a); y < Math.min(H, b); y += 2) {
      sum += Math.abs(luminance(bmp, (y * W + x + 1) * 4) - luminance(bmp, (y * W + x - 1) * 4));
      n++;
    }
  } else {
    const y = Math.max(1, Math.min(H - 2, pos));
    for (let x = Math.max(0, a); x < Math.min(W, b); x += 2) {
      sum += Math.abs(luminance(bmp, ((y + 1) * W + x) * 4) - luminance(bmp, ((y - 1) * W + x) * 4));
      n++;
    }
  }
  return n ? sum / n : 0;
}

function snap(bmp, W, H, dir, guess, span, a, b) {
  let best = guess;
  let bestV = -1;
  const vals = [];
  for (let p = Math.round(guess - span); p <= Math.round(guess + span); p++) {
    const v = lineEdge(bmp, W, H, dir, p, a, b);
    vals.push(v);
    if (v > bestV) { bestV = v; best = p; }
  }
  vals.sort((x, y) => x - y);
  const median = vals[Math.floor(vals.length / 2)] || 0;
  // Only trust a clear, consistent edge.
  return bestV > Math.max(12, median * 2.2) ? best : guess;
}

/**
 * bmp: BGRA/RGBA pixel buffer. Returns the crop rectangle in pixels, or null
 * when this system has no label spec.
 */
function labelRect(bmp, W, H, shapeKey) {
  const box = objectBox(bmp, W, H);
  const want = ASPECT[shapeKey];
  if (!want || Math.abs(Math.log(box.w / box.h / want)) > 0.12) return null;
  if (shapeKey === 'umd') return box;
  if (shapeKey === 'disc') {
    const s = Math.min(box.w, box.h);
    return { x: box.x + Math.round((box.w - s) / 2), y: box.y + Math.round((box.h - s) / 2), w: s, h: s };
  }
  const spec = SPECS[shapeKey];
  if (!spec) return null;
  const [l, t, r, b] = spec;
  let x0 = box.x + l * box.w;
  let x1 = box.x + r * box.w;
  let y0 = box.y + t * box.h;
  let y1 = box.y + b * box.h;
  const sx = box.w * 0.03;
  const sy = box.h * 0.03;
  const midY = [y0 + (y1 - y0) * 0.2, y0 + (y1 - y0) * 0.8];
  const midX = [x0 + (x1 - x0) * 0.2, x0 + (x1 - x0) * 0.8];
  x0 = snap(bmp, W, H, 'v', x0, sx, ...midY);
  x1 = snap(bmp, W, H, 'v', x1, sx, ...midY);
  if (t > 0.01) y0 = snap(bmp, W, H, 'h', y0, sy, ...midX);
  y1 = snap(bmp, W, H, 'h', y1, sy, ...midX);
  // Stay just inside the printed edge.
  const inX = (x1 - x0) * 0.006;
  const inY = (y1 - y0) * 0.006;
  return {
    x: Math.round(x0 + inX), y: Math.round(y0 + inY),
    w: Math.max(8, Math.round(x1 - x0 - inX * 2)), h: Math.max(8, Math.round(y1 - y0 - inY * 2)),
  };
}

module.exports = { labelRect, objectBox, isRoundScan, SPECS, REGIONS, ASPECT };
