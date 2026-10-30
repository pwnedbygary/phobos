import Vm from "../assets/studio.hdr?url";
import {
  CanvasTexture as pc,
  CircleGeometry as vc,
  Color as wo,
  CylinderGeometry as yc,
  DataTexture as Qs,
  DirectionalLight as qu,
  ExtrudeGeometry as Pl,
  Group as _o,
  HemisphereLight as Au,
  LinearMipmapLinearFilter as mr,
  MathUtils as va,
  Mesh as Ys,
  MeshBasicMaterial as Ls,
  MeshPhysicalMaterial as Ql,
  MeshStandardMaterial as Zl,
  Path as Yc,
  PerspectiveCamera as Hu,
  PlaneGeometry as Ll,
  PMREMGenerator as Ud,
  Raycaster as dd,
  RepeatWrapping as or,
  RGBAFormat as Ar,
  RingGeometry as Rl,
  Scene as Eo,
  Shape as Xc,
  ShapeGeometry as zl,
  SRGBColorSpace as Oi,
  Vector2 as X,
  WebGLRenderer as Lm,
} from "three";
import { SVGLoader as Gm } from "three/addons/loaders/SVGLoader.js";
import { mergeGeometries as Hm } from "three/addons/utils/BufferGeometryUtils.js";
import { HDRLoader as Bm } from "three/addons/loaders/HDRLoader.js";
import { RoomEnvironment as Rm } from "three/addons/environments/RoomEnvironment.js";
import { d as h, p as C, r as w } from "../shared/logo.js";
let getCartState = () => ({
  settings: null,
  running: false,
});
function setCartStateProvider(provider) {
  if (typeof provider !== "function") throw new TypeError("cart state provider must be a function");
  getCartState = provider;
}
var rect = (e, t, n, r, i = 0) => ({
  x: e,
  y: t,
  w: n,
  h: r,
  r: i,
});
function sharpArcPoints(e, t, n, r, i, a = 24) {
  let o = [];
  for (let s = 1; s < a; s++) {
    let c = ((r + (i - r) * (s / a)) * Math.PI) / 180;
    o.push([e + Math.cos(c) * n, t + Math.sin(c) * n, 0]);
  }
  return o;
}
function peakedCurvePoints(e, t, n, r, i, a = 18) {
  let o = (e + n) / 2,
    s = 2 * i - (t + r) / 2,
    c = [];
  for (let i = 1; i < a; i++) {
    let l = i / a,
      u = (1 - l) * (1 - l) * e + 2 * (1 - l) * l * o + l * l * n,
      d = (1 - l) * (1 - l) * t + 2 * (1 - l) * l * s + l * l * r;
    c.push([u, d, 0]);
  }
  return c;
}
var GAME_BOY_FEATURES = {
    raised: [
      {
        kind: `rect`,
        ...rect(6.5, 4.2, 44.6, 11.6, 5.8),
        height: 0.8,
      },
      {
        kind: `tri`,
        x: 24,
        y: 58.8,
        w: 9,
        h: 3.6,
        height: 0.45,
      },
    ],
    ridges: [
      {
        x: 1,
        y: 4.8,
        w: 4.6,
        h: 10.2,
        n: 7,
        dir: `h`,
        bar: 0.6,
        height: 0.55,
      },
      {
        x: 51.4,
        y: 5.6,
        w: 4.6,
        h: 9.6,
        n: 6,
        dir: `h`,
        bar: 0.6,
        height: 0.55,
      },
    ],
    grooves: [rect(55.4, 18, 0.9, 46)],
    back: {
      screws: [[28.5, 38]],
      ridges: [
        {
          x: 8,
          y: 2.2,
          w: 41,
          h: 6,
          n: 6,
          dir: `h`,
          bar: 0.5,
          height: 0.35,
        },
      ],
    },
  },
  CART_SPECS = {
    gb: {
      w: 57,
      h: 65.5,
      t: 7.7,
      color: `#a7a3ae`,
      outline: [
        [0, 0, 1.4],
        [52.2, 0, 0.4],
        [57, 3.4, 0.4],
        [57, 65.5, 1],
        [0, 65.5, 1],
      ],
      recess: {
        ...rect(6.5, 17.6, 44.6, 39.8, 1.2),
        depth: 0.6,
      },
      labelStyle: `gb`,
      ...GAME_BOY_FEATURES,
    },
    gbc: {
      w: 57,
      h: 65.5,
      t: 8.4,
      color: `#5d6270`,
      clear: !0,
      outline: [
        [0, 0, 1.4],
        [57, 0, 1.4],
        [57, 65.5, 1],
        [0, 65.5, 1],
      ],
      recess: {
        ...rect(6.5, 17.6, 44.6, 39.8, 1.2),
        depth: 0.6,
      },
      labelStyle: `gb`,
      ...GAME_BOY_FEATURES,
      ridges: [
        {
          x: 1,
          y: 4.8,
          w: 4.6,
          h: 10.2,
          n: 7,
          dir: `h`,
          bar: 0.55,
          height: 0.4,
        },
        {
          x: 51.4,
          y: 4.8,
          w: 4.6,
          h: 10.2,
          n: 7,
          dir: `h`,
          bar: 0.55,
          height: 0.4,
        },
      ],
      pcb: rect(5, 5, 47, 58),
    },
    gba: {
      w: 60,
      h: 34.6,
      t: 7.8,
      color: `#3b3c42`,
      outline: [
        [0, 0, 1.9],
        [60, 0, 1.9],
        [60, 6.2, 0.2],
        [58.7, 7.3, 0.2],
        [58.7, 34.6, 0.9],
        [1.3, 34.6, 0.9],
        [1.3, 7.3, 0.2],
        [0, 6.2, 0.2],
      ],
      slabs: [
        {
          outline: [
            [0, 0, 1.9],
            [60, 0, 1.9],
            [60, 6.2, 0.3],
            [0, 6.2, 0.3],
          ],
          grow: 0.45,
        },
      ],
      recess: {
        ...rect(8.1, 7.8, 44, 22.5, 1.3),
        depth: 0.55,
      },
      raised: [
        {
          kind: `arcband`,
          ax: 9,
          bx: 51,
          y: 5.6,
          peak: 4.3,
          width: 0.45,
          height: 0.3,
        },
        {
          kind: `chevron`,
          x: 24.2,
          y: 31.1,
          w: 11.6,
          h: 2.3,
          stroke: 0.7,
          height: 0.3,
        },
      ],
      engraved: {
        raised: [
          {
            kind: `arcband`,
            ax: 9,
            bx: 51,
            y: 4.9,
            peak: 3.4,
            width: 0.45,
            height: 0.3,
          },
          {
            kind: `chevron`,
            x: 24.2,
            y: 31.1,
            w: 11.6,
            h: 2.3,
            stroke: 0.7,
            height: 0.3,
          },
        ],
      },
      grooves: [rect(1.7, 8.3, 1, 25.4), rect(57.3, 8.3, 1, 25.4)],
      engrave: {
        x: 15,
        y: 4.55,
        w: 30,
        h: 1.55,
        height: 0.22,
      },
      back: {
        screws: [[30, 21]],
        ridges: [
          {
            x: 9,
            y: 1.2,
            w: 42,
            h: 4.2,
            n: 5,
            dir: `h`,
            bar: 0.5,
            height: 0.35,
          },
        ],
      },
    },
    ds: {
      w: 33,
      h: 35,
      t: 3.8,
      color: `#4a4b51`,
      outline: [
        [0, 0, 1.3],
        [33, 0, 1.3],
        [33, 31.4, 0],
        [32, 31.4, 0],
        [32, 35, 0.7],
        [1, 35, 0.7],
        [1, 31.4, 0],
        [0, 31.4, 0],
      ],
      recess: {
        ...rect(3, 1.75, 27, 29.4, 1),
        depth: 0.35,
      },
      raised: [
        {
          kind: `tri`,
          x: 15,
          y: 32.3,
          w: 3,
          h: 1.6,
          height: 0.2,
        },
      ],
      slabs: [
        {
          outline: [
            [0, 0, 1.3],
            [33, 0, 1.3],
            [33, 3.2, 0],
            [0, 3.2, 0],
          ],
          grow: 0,
          back: 0.55,
        },
      ],
      back: {
        contacts: {
          x: 3.2,
          y: 30.6,
          w: 26.6,
          h: 4,
          n: 17,
        },
      },
      bevel: 0.35,
    },
    "3ds": {
      w: 35,
      h: 35,
      t: 3.8,
      color: `#e2e2e6`,
      outline: [
        [0, 0, 1.3],
        [35, 0, 0.5],
        [35, 5.3, 0],
        [33, 7.3, 0],
        [33, 35, 0.7],
        [1, 35, 0.7],
        [1, 31.4, 0],
        [0, 31.4, 0],
      ],
      recess: {
        ...rect(2.8, 1.2, 27.9, 29.9, 1),
        depth: 0.35,
      },
      grooves: [rect(31.7, 9, 0.45, 24)],
      slabs: [
        {
          outline: [
            [0, 0, 1.3],
            [33, 0, 1.3],
            [33, 3.2, 0],
            [0, 3.2, 0],
          ],
          grow: 0,
          back: 0.55,
        },
      ],
      back: {
        contacts: {
          x: 3.2,
          y: 30.6,
          w: 26.6,
          h: 4,
          n: 17,
        },
      },
      bevel: 0.35,
    },
    switch: {
      w: 21.2,
      h: 31,
      t: 3.3,
      color: `#1f1f22`,
      outline: [
        [0, 0, 1.7],
        [21.2, 0, 1.7],
        [21.2, 31, 1.3],
        [0, 31, 1.3],
      ],
      recess: {
        ...rect(1.5, 2.1, 18.2, 22.8, 0.9),
        depth: 0.3,
      },
      raised: [
        {
          kind: `tri`,
          x: 9.3,
          y: 27.4,
          w: 2.6,
          h: 1.3,
          height: 0.2,
        },
      ],
      back: {
        contacts: {
          x: 2.5,
          y: 26.5,
          w: 16,
          h: 3.5,
          n: 17,
        },
      },
      bevel: 0.3,
    },
    nes: {
      w: 120,
      h: 134,
      t: 17,
      color: `#9d9da3`,
      outline: [
        [0, 0, 3],
        [120, 0, 3],
        [120, 107, 0.5],
        [116.4, 108.6, 0.5],
        [116.4, 134, 1.5],
        [3.6, 134, 1.5],
        [3.6, 108.6, 0.5],
        [0, 107, 0.5],
      ],
      recess: {
        ...rect(48, 0.8, 55.4, 89.8, 1.5),
        depth: 0.8,
      },
      wells: [rect(18.5, 2.5, 14, 12, 1)],
      ridges: [
        {
          x: 18.5,
          y: 18,
          w: 14,
          h: 76,
          n: 27,
          dir: `h`,
          bar: 0.9,
          height: 0.45,
        },
      ],
      raised: [
        {
          kind: `tri`,
          x: 55,
          y: 99.5,
          w: 10,
          h: 4.5,
          height: 0.4,
        },
      ],
      back: {
        screws: [
          [20, 112],
          [100, 112],
          [60, 30],
        ],
      },
      bevel: 1.2,
    },
    snes: {
      w: 120,
      h: 76,
      t: 20,
      color: `#a3a4aa`,
      outline: [
        [0, 1.6, 2.5],
        [18.6, 1.6, 0.4],
        [19.4, 0, 0.8],
        [100.6, 0, 0.8],
        [101.4, 1.6, 0.4],
        [120, 1.6, 2.5],
        [120, 76, 2],
        [0, 76, 2],
      ],
      recess: {
        ...rect(22.8, 0.7, 75, 33.3, 2),
        depth: 0.8,
      },
      wells: [rect(23.4, 44.5, 73.2, 30.3, 1.5)],
      grooves: [rect(18.4, 3, 0.45, 71), rect(101.15, 3, 0.45, 71)],
      frontScrews: [
        [10.2, 69.8],
        [109.8, 69.8],
      ],
      back: {
        ridges: [
          {
            x: 30,
            y: 8,
            w: 60,
            h: 14,
            n: 6,
            dir: `h`,
            bar: 1,
            height: 0.4,
          },
        ],
      },
      bevel: 1.2,
    },
    n64: {
      w: 116,
      h: 76,
      t: 18,
      color: `#8f9096`,
      outline: [[0, 6, 1.5], ...peakedCurvePoints(0, 6, 116, 6, 0, 24), [116, 6, 1.5], [116, 76, 3], [0, 76, 3]],
      recess: {
        ...rect(30.2, 6.5, 55, 62.3, 3),
        depth: 0.9,
      },
      wells: [rect(2.6, 9, 20.8, 64, 2), rect(92.6, 9, 20.8, 64, 2)],
      grooves: [rect(0.9, 51.5, 1.4, 9), rect(113.7, 51.5, 1.4, 9)],
      back: {
        ridges: [
          {
            x: 20,
            y: 6,
            w: 76,
            h: 10,
            n: 6,
            dir: `h`,
            bar: 0.9,
            height: 0.5,
          },
        ],
        screws: [[58, 60]],
      },
      bevel: 1.2,
    },
    md: {
      w: 100,
      h: 120,
      t: 17,
      color: `#1d1d21`,
      outline: [
        [10, 0, 3],
        [90, 0, 3],
        [90, 5, 0],
        [100, 10, 2],
        [100, 120, 1.5],
        [0, 120, 1.5],
        [0, 10, 2],
        [10, 5, 0],
      ],
      recess: {
        ...rect(6, 14, 88, 92, 2),
        depth: 0.7,
      },
      ridges: [
        {
          x: 16,
          y: 1.2,
          w: 68,
          h: 7.5,
          n: 4,
          dir: `h`,
          bar: 1,
          height: 0.5,
        },
      ],
      back: {
        screws: [[50, 60]],
      },
      bevel: 1.2,
    },
    sms: {
      w: 86,
      h: 112,
      t: 15,
      color: `#1e1e22`,
      outline: [
        [0, 0, 5],
        [86, 0, 5],
        [86, 112, 1.5],
        [0, 112, 1.5],
      ],
      recess: {
        ...rect(6, 13, 74, 88, 2),
        depth: 0.7,
      },
      ridges: [
        {
          x: 10,
          y: 2.5,
          w: 66,
          h: 7,
          n: 4,
          dir: `h`,
          bar: 1,
          height: 0.5,
        },
      ],
      bevel: 1,
    },
    small: {
      w: 48,
      h: 58,
      t: 9,
      color: `#2e2e33`,
      outline: [
        [0, 0, 2.5],
        [48, 0, 2.5],
        [48, 58, 1],
        [0, 58, 1],
      ],
      recess: {
        ...rect(4, 10, 40, 42, 1.2),
        depth: 0.5,
      },
      ridges: [
        {
          x: 7,
          y: 2.2,
          w: 34,
          h: 5.8,
          n: 4,
          dir: `h`,
          bar: 0.6,
          height: 0.35,
        },
      ],
      raised: [
        {
          kind: `tri`,
          x: 21,
          y: 53.5,
          w: 6,
          h: 2.6,
          height: 0.35,
        },
      ],
      bevel: 0.6,
    },
    a26: {
      w: 80,
      h: 102,
      t: 20,
      color: `#1c1c1e`,
      outline: [
        [0, 0, 4],
        [80, 0, 4],
        [80, 102, 1.5],
        [0, 102, 1.5],
      ],
      recess: {
        ...rect(6, 4, 68, 48, 1),
        depth: 0.6,
      },
      ridges: [
        {
          x: 6,
          y: 60,
          w: 68,
          h: 36,
          n: 12,
          dir: `h`,
          bar: 1.1,
          height: 0.6,
        },
      ],
      bevel: 1.2,
    },
    aes: {
      w: 150,
      h: 190,
      t: 30,
      color: `#17171a`,
      outline: [
        [0, 0, 4],
        [150, 0, 4],
        [150, 190, 2],
        [0, 190, 2],
      ],
      recess: {
        ...rect(12, 12, 126, 122, 3),
        depth: 1,
      },
      ridges: [
        {
          x: 15,
          y: 146,
          w: 120,
          h: 36,
          n: 12,
          dir: `h`,
          bar: 1.4,
          height: 0.8,
        },
      ],
      bevel: 1.5,
    },
    hucard: {
      w: 54,
      h: 85,
      t: 0.9,
      color: `#d7d8dd`,
      outline: [
        [0, 0, 3],
        [54, 0, 3],
        [54, 85, 3],
        [0, 85, 3],
      ],
      flatLabel: {
        ...rect(3, 3, 48, 68, 2),
      },
      frontContacts: {
        x: 6,
        y: 76,
        w: 42,
        h: 6.5,
        n: 38,
      },
      bevel: 0.25,
    },
    umd: {
      w: 64,
      h: 64.5,
      t: 4.2,
      color: `#c9ccd3`,
      labelStyle: `umd`,
      outline: [[0, 0, 3], [64, 0, 3], [64, 32.5, 0], ...sharpArcPoints(32, 32.5, 32, 0, 180, 40), [0, 32.5, 0]],
      faceLabel: !0,
      frontWindow: {
        cx: 32,
        cy: 35.4,
        r: 28.2,
      },
      back: {
        window: {
          cx: 32,
          cy: 40,
          r: 17,
        },
      },
      bevel: 0.7,
    },
    pcb: {
      w: 100,
      h: 76,
      t: 1.6,
      color: `#1f6d45`,
      outline: [
        [0, 0, 1],
        [100, 0, 1],
        [100, 76, 1],
        [0, 76, 1],
      ],
      flatLabel: {
        ...rect(42, 8, 52, 44, 1.5),
      },
      chips: [rect(8, 10, 26, 16), rect(8, 32, 26, 16), rect(44, 56, 20, 9)],
      frontContacts: {
        x: 6,
        y: 69,
        w: 88,
        h: 6,
        n: 36,
      },
      bevel: 0.3,
    },
  },
  DISC_SPECS = {
    cd: {
      r: 60,
      hole: 7.5,
      hub: 23,
      under: `silver`,
    },
    dvd: {
      r: 60,
      hole: 7.5,
      hub: 22,
      under: `silver`,
    },
    bluray: {
      r: 60,
      hole: 7.5,
      hub: 22,
      under: `bluray`,
    },
    psx: {
      r: 60,
      hole: 7.5,
      hub: 23,
      under: `black`,
    },
    gdrom: {
      r: 60,
      hole: 7.5,
      hub: 23,
      under: `gold`,
    },
    minidisc: {
      r: 40,
      hole: 7.5,
      hub: 16,
      under: `silver`,
    },
  },
  BOX_ART_CROPS = {
    gb: {
      l: 0.19,
    },
    gbc: {
      l: 0.19,
    },
    gba: {
      l: 0.2,
    },
    nds: {
      l: 0.13,
    },
    psx: {
      l: 0.12,
    },
    n3ds: {
      t: 0.12,
    },
    switch: {
      t: 0.1,
    },
    n64: {
      r: 0.14,
    },
  };
function resolveMediaSpec(e) {
  let t = e.media || {};
  if (t.type === `disc`)
    return {
      disc: DISC_SPECS[t.shape] || DISC_SPECS.cd,
    };
  let n = e.id === `gbc` ? `gbc` : t.shape;
  return {
    spec: CART_SPECS[n] || CART_SPECS.small,
    key: CART_SPECS[n] ? n : `small`,
  };
}
var ENGRAVING_PATHS = {
    wordmark: {
      d: `M389 702Q440 702 477.0 699.0Q514 696 545.0 690.5Q576 685 607 676L585 539Q550 541 520.0 541.5Q490 542 457.5 542.5Q425 543 380 543Q348 543 326.0 535.5Q304 528 288.5 507.0Q273 486 262.0 445.0Q251 404 243 338Q232 262 233.5 220.5Q235 179 254.5 163.0Q274 147 317 147Q390 147 438.0 149.5Q486 152 533 156L540 18Q487 2 435.0 -5.0Q383 -12 309 -12Q198 -12 134.0 23.0Q70 58 49.0 138.0Q28 218 47 352Q62 455 86.0 523.0Q110 591 149.0 630.0Q188 669 247.0 685.5Q306 702 389 702ZM1137 690Q1159 690 1176.5 675.0Q1194 660 1197 638L1295 0H1107L1057 478Q1055 497 1054.5 515.0Q1054 533 1054 551H1026Q1022 536 1017.0 517.0Q1012 498 1004 480L815 0H620L898 645Q906 667 923.5 678.5Q941 690 964 690ZM1179 301 1163 162H803L820 301ZM1785 700Q1875 700 1935.5 680.5Q1996 661 2022.5 614.0Q2049 567 2038 485Q2032 443 2016.0 409.0Q2000 375 1968.5 353.0Q1937 331 1882 325L1881 319Q1899 316 1917.5 305.0Q1936 294 1950.0 271.5Q1964 249 1968 210L1998 0H1805L1781 211Q1779 242 1768.5 251.0Q1758 260 1733 260Q1659 261 1612.0 261.0Q1565 261 1533.0 262.0Q1501 263 1472 265L1487 396H1749Q1784 396 1804.0 403.5Q1824 411 1834.5 431.0Q1845 451 1851 487Q1855 517 1850.5 534.0Q1846 551 1829.5 557.5Q1813 564 1783 564Q1680 564 1611.5 562.5Q1543 561 1519 559L1516 690Q1559 695 1597.5 697.0Q1636 699 1680.0 699.5Q1724 700 1785 700ZM1699 690 1602 0H1419L1516 690ZM2580 690 2483 0H2296L2393 690ZM2778 690 2758 547H2175L2195 690ZM3190 700Q3280 700 3340.5 680.5Q3401 661 3427.5 614.0Q3454 567 3443 485Q3437 443 3421.0 409.0Q3405 375 3373.5 353.0Q3342 331 3287 325L3286 319Q3304 316 3322.5 305.0Q3341 294 3355.0 271.5Q3369 249 3373 210L3403 0H3210L3186 211Q3184 242 3173.5 251.0Q3163 260 3138 260Q3064 261 3017.0 261.0Q2970 261 2938.0 262.0Q2906 263 2877 265L2892 396H3154Q3189 396 3209.0 403.5Q3229 411 3239.5 431.0Q3250 451 3256 487Q3260 517 3255.5 534.0Q3251 551 3234.5 557.5Q3218 564 3188 564Q3085 564 3016.5 562.5Q2948 561 2924 559L2921 690Q2964 695 3002.5 697.0Q3041 699 3085.0 699.5Q3129 700 3190 700ZM3104 690 3007 0H2824L2921 690ZM3836 690 3739 0H3551L3648 690ZM4294 700Q4405 700 4472.5 668.0Q4540 636 4564.5 557.0Q4589 478 4569 338Q4554 233 4529.5 164.5Q4505 96 4465.5 58.5Q4426 21 4366.0 5.5Q4306 -10 4220 -10Q4138 -10 4066.5 -7.0Q3995 -4 3929 0L4019 135Q4054 132 4109.5 130.5Q4165 129 4218 129Q4251 129 4276.0 137.5Q4301 146 4319.0 170.0Q4337 194 4350.0 238.0Q4363 282 4372 352Q4381 416 4382.0 457.0Q4383 498 4374.0 521.0Q4365 544 4344.0 552.5Q4323 561 4288 561Q4254 561 4220.5 561.0Q4187 561 4156.0 561.0Q4125 561 4098 561L4026 690Q4064 693 4108.0 695.5Q4152 698 4199.5 699.0Q4247 700 4294 700ZM4213 690 4116 0H3929L4026 690ZM5085 704Q5125 704 5163.0 701.0Q5201 698 5238.0 692.0Q5275 686 5309 676L5287 541Q5252 542 5218.0 542.5Q5184 543 5150.0 543.5Q5116 544 5076 544Q5037 544 5010.0 534.5Q4983 525 4965.0 502.0Q4947 479 4935.0 439.0Q4923 399 4915 338Q4904 262 4906.5 220.5Q4909 179 4927.0 163.0Q4945 147 4982 147Q5015 147 5036.5 156.0Q5058 165 5075.0 178.5Q5092 192 5107 203L5137 124Q5123 106 5101.0 83.0Q5079 60 5049.5 37.5Q5020 15 4982.0 0.5Q4944 -14 4897 -14Q4828 -14 4782.0 21.0Q4736 56 4718.5 136.5Q4701 217 4719 352Q4734 459 4761.5 528.0Q4789 597 4833.0 635.5Q4877 674 4939.0 689.0Q5001 704 5085 704ZM5302 398 5246 0H5109L5100 107L5087 140L5123 398ZM5661 693Q5720 693 5780.0 692.5Q5840 692 5896.0 689.5Q5952 687 5998 681L5977 548H5729Q5702 548 5689.5 538.0Q5677 528 5673 502L5629 187Q5626 165 5636.0 153.5Q5646 142 5671 142H5917L5914 7Q5867 1 5811.5 -0.5Q5756 -2 5698.5 -2.5Q5641 -3 5589 -3Q5506 -3 5467.5 46.0Q5429 95 5439 173L5491 548Q5501 615 5544.5 654.0Q5588 693 5661 693ZM5492 419H5921L5905 292H5472Z`,
      bounds: [38, -14, 5998, 704],
    },
    badge: {
      d: `M450.0 900L3750.0 900C3998.535 900 4200 698.535 4200 450.0L4200 450.0C4200 201.465 3998.535 0 3750.0 0L450.0 0C201.465 0 0 201.465 0 450.0L0 450.0C0 698.535 201.465 900 450.0 900ZM450.0 810C251.172 810 90 648.828 90 450.0L90 450.0C90 251.172 251.172 90 450.0 90L3750.0 90C3948.828 90 4110 251.172 4110 450.0L4110 450.0C4110 648.828 3948.828 810 3750.0 810ZM1900.0 770.0L2300.0 770.0C2333.138 770.0 2360.0 743.138 2360.0 710.0L2360.0 190.0C2360.0 156.862 2333.138 130.0 2300.0 130.0L1900.0 130.0C1866.862 130.0 1840.0 156.862 1840.0 190.0L1840.0 710.0C1840.0 743.138 1866.862 770.0 1900.0 770.0ZM1960.0 680.0C1943.431 680.0 1930.0 666.569 1930.0 650.0L1930.0 410.0C1930.0 393.431 1943.431 380.0 1960.0 380.0L2240.0 380.0C2256.569 380.0 2270.0 393.431 2270.0 410.0L2270.0 650.0C2270.0 666.569 2256.569 680.0 2240.0 680.0ZM1470.0 510.0L1520.0 510.0C1553.138 510.0 1580.0 483.138 1580.0 450.0L1580.0 450.0C1580.0 416.862 1553.138 390.0 1520.0 390.0L1470.0 390.0C1436.862 390.0 1410.0 416.862 1410.0 450.0L1410.0 450.0C1410.0 483.138 1436.862 510.0 1470.0 510.0ZM1170.0 510.0L1220.0 510.0C1253.138 510.0 1280.0 483.138 1280.0 450.0L1280.0 450.0C1280.0 416.862 1253.138 390.0 1220.0 390.0L1170.0 390.0C1136.862 390.0 1110.0 416.862 1110.0 450.0L1110.0 450.0C1110.0 483.138 1136.862 510.0 1170.0 510.0ZM870.0 510.0L920.0 510.0C953.138 510.0 980.0 483.138 980.0 450.0L980.0 450.0C980.0 416.862 953.138 390.0 920.0 390.0L870.0 390.0C836.862 390.0 810.0 416.862 810.0 450.0L810.0 450.0C810.0 483.138 836.862 510.0 870.0 510.0ZM2680.0 510.0L2730.0 510.0C2763.138 510.0 2790.0 483.138 2790.0 450.0L2790.0 450.0C2790.0 416.862 2763.138 390.0 2730.0 390.0L2680.0 390.0C2646.862 390.0 2620.0 416.862 2620.0 450.0L2620.0 450.0C2620.0 483.138 2646.862 510.0 2680.0 510.0ZM2980.0 510.0L3030.0 510.0C3063.138 510.0 3090.0 483.138 3090.0 450.0L3090.0 450.0C3090.0 416.862 3063.138 390.0 3030.0 390.0L2980.0 390.0C2946.862 390.0 2920.0 416.862 2920.0 450.0L2920.0 450.0C2920.0 483.138 2946.862 510.0 2980.0 510.0ZM3280.0 510.0L3330.0 510.0C3363.138 510.0 3390.0 483.138 3390.0 450.0L3390.0 450.0C3390.0 416.862 3363.138 390.0 3330.0 390.0L3280.0 390.0C3246.862 390.0 3220.0 416.862 3220.0 450.0L3220.0 450.0C3220.0 483.138 3246.862 510.0 3280.0 510.0Z`,
      bounds: [0, 0, 4200, 900],
    },
    lines: {
      d: `M600 0L3400 0L3520 200L720 200ZM0 330L4000 330L4120 530L120 530ZM600 660L3400 660L3520 860L720 860Z`,
      bounds: [0, 0, 4120, 860],
    },
  },
  engravingOverride = null;
function currentEngravingId() {
  let e = engravingOverride ?? ((getCartState().settings && getCartState().settings.ui.engraving) || `none`);
  return ENGRAVING_PATHS[e] ? e : `none`;
}
var boundsRect = (e, t, n, r) => ({
    x: e,
    y: t,
    w: n,
    h: r,
  }),
  roundedRectPoints = (e, t, n, r, i = 0) => [
    [e, t, i],
    [e + n, t, i],
    [e + n, t + r, i],
    [e, t + r, i],
  ];
function traceRoundedPolygon(e, t, n, r) {
  let i = (e, t) => new X(e - n / 2, r / 2 - t),
    a = t.length;
  for (let n = 0; n < a; n++) {
    let [r, o, s = 0] = t[n],
      [c, l] = t[(n - 1 + a) % a],
      [u, d] = t[(n + 1) % a],
      f = i(r, o);
    if (!s) {
      n === 0 ? e.moveTo(f.x, f.y) : e.lineTo(f.x, f.y);
      continue;
    }
    let p = i(c, l).sub(f),
      m = i(u, d).sub(f),
      h = Math.min(s, p.length() / 2, m.length() / 2),
      g = f.clone().add(p.normalize().multiplyScalar(h)),
      _ = f.clone().add(m.normalize().multiplyScalar(h));
    (n === 0 ? e.moveTo(g.x, g.y) : e.lineTo(g.x, g.y), e.quadraticCurveTo(f.x, f.y, _.x, _.y));
  }
  return (e.closePath(), e);
}
var polygonShape = (e, t, n) => traceRoundedPolygon(new Xc(), e, t, n),
  polygonHole = (e, t, n) => traceRoundedPolygon(new Yc(), e, t, n);
function extrudeSlab(e, t, n, r = 0, i = 8) {
  let a = n - t,
    o = Math.max(0, Math.min(r, a / 2 - 0.01)),
    s = new Pl(e, {
      depth: Math.max(0.01, a - 2 * o),
      bevelEnabled: o > 0,
      bevelThickness: o,
      bevelSize: o,
      bevelOffset: -o,
      bevelSegments: o > 0 ? (o > 0.5 ? 5 : 3) : 0,
      curveSegments: i,
    });
  return (s.translate(0, 0, t + o), s.index ? s.toNonIndexed() : s);
}
function buildLabelPlane(e, t, n, r) {
  let i = new zl(polygonShape(e, t, n), 12);
  i.computeBoundingBox();
  let { min: a, max: o } = i.boundingBox,
    s = i.attributes.position,
    c = i.attributes.uv;
  for (let e = 0; e < s.count; e++) c.setXY(e, (s.getX(e) - a.x) / (o.x - a.x), (s.getY(e) - a.y) / (o.y - a.y));
  return (i.translate(0, 0, r), i);
}
function buildRidgeRects(e) {
  let t = [],
    n = e.n > 1 ? ((e.dir === `h` ? e.h : e.w) - e.bar) / (e.n - 1) : 0;
  for (let r = 0; r < e.n; r++)
    t.push(
      e.dir === `h`
        ? roundedRectPoints(e.x, e.y + r * n, e.w, e.bar, e.bar / 2)
        : roundedRectPoints(e.x + r * n, e.y, e.bar, e.h, e.bar / 2),
    );
  return t;
}
function buildRaisedFeature(e) {
  if (e.kind === `rect`) return [roundedRectPoints(e.x, e.y, e.w, e.h, e.r)];
  if (e.kind === `tri`)
    return [
      [
        [e.x, e.y, 0.3],
        [e.x + e.w, e.y, 0.3],
        [e.x + e.w / 2, e.y + e.h, 0.3],
      ],
    ];
  if (e.kind === `chevron`) {
    let t = e.x + e.w / 2,
      n = e.stroke;
    return [
      [
        [e.x, e.y, 0],
        [e.x + n * 1.4, e.y, 0],
        [t, e.y + e.h - n, 0],
        [e.x + e.w - n * 1.4, e.y, 0],
        [e.x + e.w, e.y, 0],
        [t, e.y + e.h, 0],
      ],
    ];
  }
  if (e.kind === `arcband`) {
    let t = [],
      n = [];
    for (let r = 0; r <= 24; r++) {
      let i = r / 24,
        a = e.ax + (e.bx - e.ax) * i,
        o = e.y - (e.y - e.peak) * 4 * i * (1 - i);
      (t.push([a, o, 0]), n.unshift([a, o + e.width, 0]));
    }
    return [[...t, ...n]];
  }
  return [];
}
function buildEngravingGeometry(e, t, n, r, i) {
  let [a, o, s, c] = e.bounds,
    l = s - a,
    u = c - o,
    d = Math.min(t.w / l, t.h / u),
    f = t.x + (t.w - l * d) / 2,
    p = t.y + (t.h + u * d) / 2,
    m = `<svg xmlns="http://www.w3.org/2000/svg"><path transform="translate(${f - n / 2 - d * a} ${r / 2 - p - d * o}) scale(${d})" d="${e.d}"/></svg>`,
    h = new Gm().parse(m).paths.flatMap((e) => Gm.createShapes(e)),
    g = t.height || 0.22;
  return h.map((e) => extrudeSlab(e, i - 0.15, i + g, Math.min(0.07, g / 3), 6));
}
var geometryCache = new Map();
function buildCartGeometry(e, t = `none`) {
  let n = CART_SPECS[e],
    r =
      t !== `none` && n.engraved
        ? {
            ...n,
            ...n.engraved,
          }
        : n,
    { w: i, h: a, t: o } = r,
    s = r.bevel ?? 0.6,
    c = [],
    l = [],
    u = [],
    d = [],
    f = o / 2,
    p = -o / 2;
  if (r.recess) {
    let e = f - r.recess.depth;
    c.push(extrudeSlab(polygonShape(r.outline, i, a), p, e, s));
    let t = polygonShape(r.outline, i, a);
    t.holes.push(polygonHole(roundedRectPoints(r.recess.x, r.recess.y, r.recess.w, r.recess.h, r.recess.r), i, a));
    for (let e of r.wells || []) t.holes.push(polygonHole(roundedRectPoints(e.x, e.y, e.w, e.h, e.r || 0), i, a));
    c.push(extrudeSlab(t, e - Math.min(1.2, o / 3), f, Math.min(s, 0.45)));
  } else c.push(extrudeSlab(polygonShape(r.outline, i, a), p, f, s));
  for (let e of r.slabs || []) {
    let t = e.grow || 0;
    c.push(extrudeSlab(polygonShape(e.outline, i, a), p - t - (e.back || 0), f + t, Math.min(s, 0.5)));
  }
  let m = (e) => {
    for (let t of r.slabs || []) {
      let n = t.outline.map((e) => e[1]);
      if (e >= Math.min(...n) && e <= Math.max(...n)) return f + (t.grow || 0);
    }
    return f;
  };
  for (let e of r.raised || []) {
    let t = m(e.y + (e.h || 0) / 2);
    for (let n of buildRaisedFeature(e))
      c.push(extrudeSlab(polygonShape(n, i, a), t - 0.2, t + e.height, Math.min(0.15, e.height / 3), 10));
  }
  if (r.engrave && t !== `none` && typeof DOMParser < `u`) {
    let e = r.engrave;
    try {
      c.push(...buildEngravingGeometry(ENGRAVING_PATHS[t], e, i, a, m(e.y + e.h / 2)));
    } catch {}
  }
  for (let e of r.ridges || []) {
    let t = m(e.y + e.h / 2);
    l.push(
      extrudeSlab(
        polygonShape(roundedRectPoints(e.x, e.y, e.w, e.h, Math.min(e.bar, 1)), i, a),
        t - 0.1,
        t + 0.03,
        0,
        4,
      ),
    );
    for (let n of buildRidgeRects(e))
      c.push(extrudeSlab(polygonShape(n, i, a), t - 0.2, t + e.height, Math.min(0.12, e.bar / 4), 4));
  }
  for (let e of r.grooves || [])
    l.push(
      extrudeSlab(
        polygonShape(roundedRectPoints(e.x, e.y, e.w, e.h, Math.min(e.w, e.h) / 2), i, a),
        f - 0.1,
        f + 0.04,
        0,
        4,
      ),
    );
  for (let e of r.chips || [])
    l.push(extrudeSlab(polygonShape(roundedRectPoints(e.x, e.y, e.w, e.h, 0.4), i, a), f, f + 1.4, 0.2, 4));
  if (r.frontContacts) {
    let e = r.frontContacts,
      t = e.w / e.n;
    for (let n = 0; n < e.n; n++)
      u.push(
        extrudeSlab(
          polygonShape(roundedRectPoints(e.x + n * t + t * 0.2, e.y, t * 0.6, e.h, 0.1), i, a),
          f - 0.05,
          f + 0.03,
          0,
          2,
        ),
      );
  }
  for (let [e, t] of r.frontScrews || []) {
    let n = new yc(2.2, 2.2, 0.5, 24).rotateX(Math.PI / 2).toNonIndexed();
    (n.translate(e - i / 2, a / 2 - t, f + 0.02), l.push(n));
    let r = new yc(1.5, 1.5, 0.4, 6).rotateX(Math.PI / 2).toNonIndexed();
    (r.translate(e - i / 2, a / 2 - t, f + 0.1), d.push(r));
  }
  let h = r.back || {};
  for (let e of h.ridges || [])
    for (let t of buildRidgeRects(e))
      c.push(extrudeSlab(polygonShape(t, i, a), p - e.height - (r.slabs?.[0]?.grow || 0), p + 0.2, 0, 4));
  for (let [e, t] of h.screws || []) {
    let n = new yc(2.4, 2.4, 0.5, 24).rotateX(Math.PI / 2).toNonIndexed();
    (n.translate(e - i / 2, a / 2 - t, p - 0.2), l.push(n));
    let r = new yc(1.7, 1.7, 0.4, 24).rotateX(Math.PI / 2).toNonIndexed();
    (r.translate(e - i / 2, a / 2 - t, p - 0.35), d.push(r));
  }
  if (h.contacts) {
    let e = h.contacts;
    l.push(extrudeSlab(polygonShape(roundedRectPoints(e.x, e.y, e.w, e.h, 0.3), i, a), p - 0.06, p + 0.2, 0, 4));
    let t = e.w / e.n;
    for (let n = 0; n < e.n; n++)
      u.push(
        extrudeSlab(
          polygonShape(roundedRectPoints(e.x + n * t + t * 0.22, e.y + 0.5, t * 0.56, e.h - 0.9, 0.1), i, a),
          p - 0.1,
          p,
          0,
          2,
        ),
      );
  }
  let g = null;
  if (r.recess) {
    let e = r.recess,
      t = Math.min(0.45, e.w * 0.01 + 0.2),
      n = r.label || {
        x: e.x + t,
        y: e.y + t,
        w: e.w - t * 2,
        h: e.h - t * 2,
        r: Math.max(0.3, e.r - t),
      };
    g = {
      geo: buildLabelPlane(roundedRectPoints(n.x, n.y, n.w, n.h, n.r), i, a, f - e.depth + 0.06),
      aspect: n.w / n.h,
    };
  } else if (r.faceLabel)
    g = {
      geo: buildLabelPlane(r.outline, i, a, f + 0.05),
      aspect: i / a,
    };
  else if (r.flatLabel) {
    let e = r.flatLabel;
    g = {
      geo: buildLabelPlane(roundedRectPoints(e.x, e.y, e.w, e.h, e.r), i, a, f + 0.04),
      aspect: e.w / e.h,
    };
  }
  let _ = null,
    v = r.pcb || (o >= 5 && !r.flatLabel ? boundsRect(i * 0.08, a * 0.1, i * 0.84, a * 0.84) : null);
  v &&
    (_ = Hm([
      extrudeSlab(polygonShape(roundedRectPoints(v.x, v.y, v.w, v.h, 1), i, a), -0.8, 0.4, 0, 4),
      extrudeSlab(
        polygonShape(roundedRectPoints(v.x + v.w * 0.13, v.y + v.h * 0.28, v.w * 0.34, v.h * 0.21, 0.5), i, a),
        0.4,
        1.4,
        0,
        2,
      ),
      extrudeSlab(
        polygonShape(roundedRectPoints(v.x + v.w * 0.55, v.y + v.h * 0.34, v.w * 0.26, v.h * 0.17, 0.5), i, a),
        0.4,
        1.4,
        0,
        2,
      ),
    ]));
  let y = null;
  if (h.window) {
    let { cx: e, cy: t, r: n } = h.window,
      r = new Rl(3, n, 48);
    (r.rotateY(Math.PI), r.translate(e - i / 2, a / 2 - t, p - 0.05), (y = r));
  }
  let b = null;
  if (r.frontWindow) {
    let { cx: e, cy: t, r: n } = r.frontWindow;
    ((b = new vc(n, 72)), b.translate(e - i / 2, a / 2 - t, f + 0.16));
  }
  return {
    kind: `cart`,
    key: e,
    w: i,
    h: a,
    t: o,
    color: r.color,
    clear: !!r.clear,
    labelStyle: r.labelStyle,
    glass: b,
    plastic: Hm(c),
    dark: l.length ? Hm(l) : null,
    metal: u.length ? Hm(u) : null,
    screws: d.length ? Hm(d) : null,
    label: g,
    pcb: _,
    window: y,
  };
}
function buildDiscGeometry(e) {
  let t = DISC_SPECS[e],
    n = 1.2,
    r = new Xc();
  r.absarc(0, 0, t.r, 0, Math.PI * 2, !1);
  let i = new Yc();
  (i.absarc(0, 0, t.hole, 0, Math.PI * 2, !0), r.holes.push(i));
  let a = extrudeSlab(r, -1.2 / 2, n / 2, 0.3, 64),
    o = new Rl(t.hub, t.r - 0.7, 96, 1),
    s = o.attributes.position,
    c = o.attributes.uv;
  for (let e = 0; e < s.count; e++) c.setXY(e, (s.getX(e) / t.r + 1) / 2, (s.getY(e) / t.r + 1) / 2);
  o.translate(0, 0, 0.62);
  let l = new Rl(t.hole + 0.2, t.hub, 64, 1);
  l.translate(0, 0, 0.63);
  let u = new Rl(t.hub - 5.5, t.hub - 4.6, 64, 1);
  return (
    u.translate(0, 0, 0.65),
    {
      kind: `disc`,
      key: e,
      w: t.r * 2,
      h: t.r * 2,
      t: n,
      under: t.under,
      plastic: a,
      hub: l,
      stack: u,
      label: {
        geo: o,
        aspect: 1,
        round: !0,
      },
    }
  );
}
function getCachedGeometry(e, t) {
  let n = !t && CART_SPECS[e] && CART_SPECS[e].engrave ? currentEngravingId() : `none`,
    r = `${t ? `disc` : `cart`}:${e}:${n}`;
  return (
    geometryCache.has(r) || geometryCache.set(r, t ? buildDiscGeometry(e) : buildCartGeometry(e, n)),
    geometryCache.get(r)
  );
}
var labelTextureCache = new Map(),
  labelPaintVersion = 0,
  getLabelPaintVersion = () => labelPaintVersion,
  MAX_LABEL_TEXTURES = 160,
  labelFontsPromise = null;
function loadLabelFonts() {
  return (
    (labelFontsPromise ||= Promise.all([
      document.fonts.load(`800 40px "M PLUS 2 Variable"`),
      document.fonts.load(`600 20px "M PLUS 2 Variable"`),
    ]).catch(() => {})),
    labelFontsPromise
  );
}
function evictLabelTextures() {
  if (labelTextureCache.size <= MAX_LABEL_TEXTURES) return;
  let e = [...labelTextureCache.entries()].sort((e, t) => e[1].used - t[1].used);
  for (let [t, n] of e.slice(0, labelTextureCache.size - MAX_LABEL_TEXTURES))
    n.refs > 0 || (n.tex.dispose(), labelTextureCache.delete(t));
}
function createLabelCanvas(e) {
  let t = document.createElement(`canvas`);
  return ((t.width = e >= 1 ? 640 : Math.round(640 * e)), (t.height = e >= 1 ? Math.round(640 / e) : 640), t);
}
function createCanvasTexture(e) {
  let t = new pc(e);
  return ((t.colorSpace = Oi), (t.anisotropy = 4), t);
}
function wrapCanvasText(e, t, n) {
  let r = t.split(/\s+/),
    i = [],
    a = ``;
  for (let t of r) {
    let r = a ? `${a} ${t}` : t;
    e.measureText(r).width > n && a ? (i.push(a), (a = t)) : (a = r);
  }
  return (a && i.push(a), i);
}
function paintGeneratedLabel(e, t, n, { color: r, band: i, title: a, sub: o, round: s }) {
  let c = e.createLinearGradient(0, 0, t * 0.4, n);
  (c.addColorStop(0, C(r, 0.35, `#ffffff`)),
    c.addColorStop(0.45, r),
    c.addColorStop(1, C(r, 0.5, `#000000`)),
    (e.fillStyle = c),
    e.fillRect(0, 0, t, n),
    e.save(),
    (e.globalAlpha = 0.08),
    (e.strokeStyle = `#ffffff`),
    (e.lineWidth = t * 0.02));
  for (let r = -n; r < t + n; r += t * 0.07) (e.beginPath(), e.moveTo(r, n), e.lineTo(r + n, 0), e.stroke());
  (e.restore(), (e.fillStyle = h(r)), (e.textBaseline = `alphabetic`));
  let l = t * 0.08;
  if (s) {
    ((e.textAlign = `center`),
      (e.font = `800 ${Math.round(t * 0.1)}px "M PLUS 2 Variable", sans-serif`),
      e.fillText(a, t / 2, n * 0.3),
      o &&
        ((e.font = `700 ${Math.round(t * 0.038)}px "M PLUS 2 Variable", sans-serif`), e.fillText(o, t / 2, n * 0.78)));
    return;
  }
  i &&
    ((e.font = `700 ${Math.round(Math.min(t, n) * 0.075)}px "M PLUS 2 Variable", sans-serif`),
    (e.globalAlpha = 0.85),
    e.fillText(i, l, l + Math.min(t, n) * 0.06),
    (e.globalAlpha = 1));
  let u = Math.round(Math.min(t * 0.13, n * 0.2));
  e.font = `800 ${u}px "M PLUS 2 Variable", sans-serif`;
  let d = wrapCanvasText(e, a, t - l * 2).slice(0, 3),
    f = u * 1.02,
    p = n - l - (o ? u * 0.55 : 0) - (d.length - 1) * f;
  for (let t of d) (e.fillText(t, l, p), (p += f));
  o &&
    ((e.font = `600 ${Math.round(u * 0.42)}px "M PLUS 2 Variable", sans-serif`),
    (e.globalAlpha = 0.8),
    e.fillText(o, l, n - l * 0.8),
    (e.globalAlpha = 1));
}
function loadLabelImage(e) {
  return new Promise((t, n) => {
    let r = new Image();
    ((r.crossOrigin = `anonymous`), (r.decoding = `async`), (r.onload = () => t(r)), (r.onerror = n), (r.src = e));
  });
}
function drawCoverCrop(e, t, n, r, i) {
  let a = i || {},
    o = r.naturalWidth * (a.l || 0),
    s = r.naturalHeight * (a.t || 0),
    c = r.naturalWidth * (1 - (a.l || 0) - (a.r || 0)),
    l = r.naturalHeight * (1 - (a.t || 0) - (a.b || 0)),
    u = Math.max(t / c, n / l),
    d = t / u,
    f = n / u,
    p = o + (c - d) / 2,
    m = s + (l - f) * (t / n > 1.3 ? 0.03 : 0.3);
  e.drawImage(r, p, m, d, f, 0, 0, t, n);
}
function paintGameBoyStrip(e, t, n) {
  let r = Math.round(t * 0.085);
  return (
    (e.fillStyle = `#c9c8ce`),
    e.fillRect(t - r, 0, r, n),
    e.save(),
    e.translate(t - r / 2, n * 0.62),
    e.rotate(-Math.PI / 2),
    (e.fillStyle = `#2a2a2e`),
    (e.font = `800 ${Math.round(r * 0.42)}px "M PLUS 2 Variable", sans-serif`),
    (e.textAlign = `center`),
    (e.textBaseline = `middle`),
    e.fillText(`THIS SIDE OUT`, 0, 0),
    e.restore(),
    r
  );
}
function hasTransparentCorners(e) {
  try {
    let t = document.createElement(`canvas`);
    ((t.width = 16), (t.height = 16));
    let n = t.getContext(`2d`);
    n.drawImage(e, 0, 0, 16, 16);
    let r = n.getImageData(0, 0, 16, 16).data;
    return [0, 15, 240, 255].filter((e) => r[e * 4 + 3] < 40).length >= 3;
  } catch {
    return !1;
  }
}
function paintUmdLabel(e, t, n, r, i, a) {
  let o = e.createLinearGradient(0, 0, t, n);
  (o.addColorStop(0, `#e4e6eb`), o.addColorStop(1, `#b9bdc6`), (e.fillStyle = o), e.fillRect(0, 0, t, n));
  let s = t * 0.5,
    c = (35.4 / 64.5) * n,
    l = (28.2 / 64) * t;
  if ((e.save(), e.beginPath(), e.arc(s, c, l, 0, Math.PI * 2), e.clip(), r)) {
    let t = Math.max((2 * l) / r.naturalWidth, (2 * l) / r.naturalHeight),
      n = r.naturalWidth * t,
      i = r.naturalHeight * t;
    e.drawImage(r, s - n / 2, c - i * 0.42, n, i);
  } else {
    let r = e.createRadialGradient(s, c, l * 0.2, s, c, l);
    (r.addColorStop(0, C(i, 0.3, `#ffffff`)),
      r.addColorStop(1, C(i, 0.35, `#000000`)),
      (e.fillStyle = r),
      e.fillRect(0, 0, t, n),
      a &&
        ((e.fillStyle = h(i)),
        (e.textAlign = `center`),
        (e.font = `800 ${Math.round(t * 0.07)}px "M PLUS 2 Variable", sans-serif`),
        wrapCanvasText(e, a, l * 1.5)
          .slice(0, 2)
          .forEach((n, r) => e.fillText(n, s, c - l * 0.45 + r * t * 0.08))));
  }
  (e.restore(),
    (e.fillStyle = `rgba(235,238,244,0.92)`),
    e.beginPath(),
    e.arc(s, c, l * 0.3, 0, Math.PI * 2),
    e.fill(),
    (e.fillStyle = `#9aa0ab`),
    e.beginPath(),
    e.arc(s, c, l * 0.12, 0, Math.PI * 2),
    e.fill(),
    (e.strokeStyle = `rgba(0,0,0,0.25)`),
    (e.lineWidth = t * 0.006),
    e.beginPath(),
    e.arc(s, c, l, 0, Math.PI * 2),
    e.stroke());
}
function paintLabelSheen(e, t, n) {
  let r = e.createLinearGradient(0, 0, t, n);
  (r.addColorStop(0, `rgba(255,255,255,0.10)`),
    r.addColorStop(0.5, `rgba(255,255,255,0)`),
    r.addColorStop(1, `rgba(0,0,0,0.10)`),
    (e.fillStyle = r),
    e.fillRect(0, 0, t, n));
}
function labelSeed(e) {
  let t = 2166136261;
  for (let n = 0; n < e.length; n++) ((t ^= e.charCodeAt(n)), (t = Math.imul(t, 16777619)));
  return ((t >>> 0) % 2147483646) + 1;
}
function paintLabelWear(e, t, n, r, i) {
  let a = labelSeed(i || `x`),
    o = () => ((a = (a * 16807) % 2147483647), a / 2147483647),
    s = Math.min(t, n);
  (e.save(),
    (e.globalCompositeOperation = `multiply`),
    (e.fillStyle = `rgba(245, 232, 200, ${0.1 + r * 0.08})`),
    e.fillRect(0, 0, t, n),
    (e.globalCompositeOperation = `screen`),
    (e.fillStyle = `rgba(255, 250, 238, ${0.03 + r * 0.035})`),
    e.fillRect(0, 0, t, n),
    (e.globalCompositeOperation = `source-over`));
  let c = [0, 24, 55, 95][r];
  for (let i = 0; i < c; i++) {
    let i = Math.floor(o() * 4),
      a = o(),
      c = o() ** 2.2 * s * (0.02 + r * 0.018),
      l = s * (0.01 + o() * 0.05);
    ((e.fillStyle = `rgba(236, 230, 214, ${0.3 + o() * 0.4})`),
      e.beginPath(),
      i === 0
        ? e.ellipse(a * t, 0, l, c, 0, 0, Math.PI * 2)
        : i === 1
          ? e.ellipse(a * t, n, l, c, 0, 0, Math.PI * 2)
          : i === 2
            ? e.ellipse(0, a * n, c, l, 0, 0, Math.PI * 2)
            : e.ellipse(t, a * n, c, l, 0, 0, Math.PI * 2),
      e.fill());
  }
  let l = [0, 8, 20, 40][r];
  e.lineCap = `round`;
  for (let r = 0; r < l; r++) {
    let r = o() * t,
      i = o() * n,
      a = o() * Math.PI * 2,
      c = s * (0.04 + o() * 0.3);
    ((e.strokeStyle = `rgba(255, 253, 245, ${0.18 + o() * 0.35})`),
      (e.lineWidth = Math.max(0.6, s * (0.0015 + o() * 0.003))),
      e.beginPath(),
      e.moveTo(r, i),
      e.quadraticCurveTo(
        r + Math.cos(a) * c * 0.5 + (o() - 0.5) * s * 0.03,
        i + Math.sin(a) * c * 0.5 + (o() - 0.5) * s * 0.03,
        r + Math.cos(a) * c,
        i + Math.sin(a) * c,
      ),
      e.stroke());
  }
  for (let i = 0; i < r * 3; i++) {
    let i = o() * t,
      a = o() * n,
      c = s * (0.05 + o() * 0.12),
      l = e.createRadialGradient(i, a, 0, i, a, c);
    (l.addColorStop(0, `rgba(90, 76, 52, ${0.06 + r * 0.03})`),
      l.addColorStop(1, `rgba(90, 76, 52, 0)`),
      (e.fillStyle = l),
      e.fillRect(i - c, a - c, c * 2, c * 2));
  }
  if (r >= 2) {
    let i = Math.floor(o() * 4),
      a = s * (0.05 + o() * 0.05 + (r - 2) * 0.04),
      c = i % 2 ? t : 0,
      l = i > 1 ? n : 0,
      u = i % 2 ? -1 : 1,
      d = i > 1 ? -1 : 1;
    ((e.fillStyle = `rgba(0, 0, 0, 0.28)`),
      e.beginPath(),
      e.moveTo(c + u * a * 1.15, l),
      e.lineTo(c, l + d * a * 1.15),
      e.lineTo(c + u * a * 0.2, l + d * a * 0.2),
      e.fill(),
      (e.fillStyle = `#e9e3d3`),
      e.beginPath(),
      e.moveTo(c + u * a, l),
      e.lineTo(c, l + d * a),
      e.lineTo(c + u * a * 0.05, l + d * a * 0.05),
      e.fill());
  }
  if (r >= 3) {
    let r = t * (0.15 + o() * 0.6),
      i = s * (0.08 + o() * 0.1),
      a = s * (0.03 + o() * 0.05),
      c = o() < 0.5;
    ((e.fillStyle = `#d9d2c1`), e.beginPath());
    let l = c ? 0 : n,
      u = c ? 1 : -1;
    e.moveTo(r, l);
    for (let t = 1; t < 9; t++) e.lineTo(r + (i * t) / 9, l + u * a * (0.4 + o() * 0.6) * Math.sin((Math.PI * t) / 9));
    (e.lineTo(r + i, l), e.fill());
    let d = n * (0.2 + o() * 0.6),
      f = d + (o() - 0.5) * n * 0.3;
    ((e.strokeStyle = `rgba(255, 252, 240, 0.35)`),
      (e.lineWidth = Math.max(1, s * 0.004)),
      e.beginPath(),
      e.moveTo(0, d),
      e.lineTo(t, f),
      e.stroke(),
      (e.strokeStyle = `rgba(0, 0, 0, 0.18)`),
      e.beginPath(),
      e.moveTo(0, d + s * 0.004),
      e.lineTo(t, f + s * 0.004),
      e.stroke());
  }
  e.restore();
}
function acquireLabelTexture(e, t) {
  let n = labelTextureCache.get(e);
  return (
    n ||
      ((n = {
        tex: t(),
        refs: 0,
        used: 0,
      }),
      labelTextureCache.set(e, n),
      evictLabelTextures()),
    (n.refs += 1),
    (n.used = performance.now()),
    n.tex
  );
}
function releaseLabelTexture(e) {
  let t = labelTextureCache.get(e);
  t && (t.refs = Math.max(0, t.refs - 1));
}
function labelTextureKey(e, t) {
  return e.kind === `art`
    ? `art|${e.url}|${e.style || ``}|${t.toFixed(2)}`
    : `gen|${e.color}|${e.band || ``}|${e.title}|${e.sub || ``}|${+!!e.round}|${+!!e.umd}|${t.toFixed(2)}`;
}
function createLabelTexture(e, t, n = null) {
  let r = createLabelCanvas(t),
    i = r.getContext(`2d`),
    a = r.width,
    o = r.height,
    s = createCanvasTexture(r),
    c = e;
  for (; c && c.kind === `art`;) c = c.fallback;
  let l = () =>
      loadLabelFonts().then(() => {
        (e.style === `umd` || c.umd ? paintUmdLabel(i, a, o, null, c.color, c.title) : paintGeneratedLabel(i, a, o, c),
          paintLabelSheen(i, a, o),
          n && paintLabelWear(i, a, o, n.wear, n.seed),
          (s.needsUpdate = !0),
          labelPaintVersion++);
      }),
    u = (e) =>
      loadLabelImage(e.url).then((t) => {
        if (e.style === `scan` && e.umd && hasTransparentCorners(t)) {
          paintUmdLabel(i, a, o, null, `#8a8f99`);
          let e = a * 0.5,
            n = (35.4 / 64.5) * o,
            r = (28.2 / 64) * a * 1.02;
          (i.save(),
            i.beginPath(),
            i.arc(e, n, r, 0, Math.PI * 2),
            i.clip(),
            i.drawImage(t, e - r, n - r, r * 2, r * 2),
            i.restore());
        } else if (e.style === `scan`) i.drawImage(t, 0, 0, a, o);
        else if (e.style === `umd`) paintUmdLabel(i, a, o, t);
        else {
          let n = a;
          (e.style === `gb` && (n = a - paintGameBoyStrip(i, a, o)),
            i.save(),
            i.beginPath(),
            i.rect(0, 0, n, o),
            i.clip(),
            drawCoverCrop(i, n, o, t, e.crop),
            i.restore(),
            paintLabelSheen(i, a, o));
        }
        (n && paintLabelWear(i, a, o, n.wear, n.seed), (s.needsUpdate = !0), labelPaintVersion++);
      }),
    d = (e) => {
      e && e.kind === `art` && e.url ? u(e).catch(() => d(e.fallback)) : c && l();
    };
  return ((i.fillStyle = c ? C(c.color, 0.2, `#000000`) : `#222`), i.fillRect(0, 0, a, o), d(e), s);
}
var shadowTexture = null;
function getShadowTexture() {
  if (shadowTexture) return shadowTexture;
  let e = document.createElement(`canvas`);
  ((e.width = 128), (e.height = 128));
  let t = e.getContext(`2d`),
    n = t.createRadialGradient(64, 64, 0, 64, 64, 64);
  return (
    n.addColorStop(0, `rgba(0,0,0,0.55)`),
    n.addColorStop(0.5, `rgba(0,0,0,0.22)`),
    n.addColorStop(1, `rgba(0,0,0,0)`),
    (t.fillStyle = n),
    t.fillRect(0, 0, 128, 128),
    (shadowTexture = new pc(e)),
    shadowTexture
  );
}
var CAMERA_FOV = 24,
  INSERT_DURATION_MS = 900,
  MAX_POOLED_ITEMS = 48,
  contactMaterial = new Zl({
    color: `#d2a84a`,
    metalness: 1,
    roughness: 0.32,
  }),
  screwMaterial = new Zl({
    color: `#9a9aa2`,
    metalness: 1,
    roughness: 0.35,
  }),
  pcbMaterial = new Zl({
    color: `#2f7a4c`,
    roughness: 0.5,
    metalness: 0.15,
    emissive: `#0b2616`,
  });
function createDiscMaterial(e) {
  let t = {
    metalness: 1,
    roughness: 0.12,
    iridescence: 1,
    iridescenceIOR: 1.7,
    iridescenceThicknessRange: [180, 820],
  };
  return e === `black`
    ? new Ql({
        ...t,
        color: `#141417`,
        metalness: 0.6,
        roughness: 0.18,
        iridescence: 0.5,
      })
    : e === `gold`
      ? new Ql({
          ...t,
          color: `#e6d5a2`,
        })
      : e === `bluray`
        ? new Ql({
            ...t,
            color: `#c5cfe8`,
          })
        : new Ql({
            ...t,
            color: `#dfe3ea`,
          });
}
var plasticNormalMap = null;
function getPlasticNormalMap() {
  if (plasticNormalMap) return plasticNormalMap;
  let e = new Float32Array(16384),
    t = 7,
    n = () => ((t = (t * 16807) % 2147483647), t / 2147483647);
  for (let t = 0; t < 16384; t++) e[t] = n();
  let r = (e) => {
      let t = new Float32Array(16384);
      for (let n = 0; n < 128; n++)
        for (let r = 0; r < 128; r++) {
          let i = 0;
          for (let t = -1; t <= 1; t++)
            for (let a = -1; a <= 1; a++) i += e[((n + t + 128) % 128) * 128 + ((r + a + 128) % 128)];
          t[n * 128 + r] = i / 9;
        }
      return t;
    },
    i = r(r(e)),
    a = new Uint8Array(65536);
  for (let e = 0; e < 128; e++)
    for (let t = 0; t < 128; t++) {
      let n = (i[e * 128 + ((t + 1) % 128)] - i[e * 128 + ((t - 1 + 128) % 128)]) * 6,
        r = (i[((e + 1) % 128) * 128 + t] - i[((e - 1 + 128) % 128) * 128 + t]) * 6,
        o = Math.hypot(n, r, 1),
        s = (e * 128 + t) * 4;
      ((a[s] = ((-n / o) * 0.5 + 0.5) * 255),
        (a[s + 1] = ((-r / o) * 0.5 + 0.5) * 255),
        (a[s + 2] = ((1 / o) * 0.5 + 0.5) * 255),
        (a[s + 3] = 255));
    }
  return (
    (plasticNormalMap = new Qs(a, 128, 128, Ar)),
    (plasticNormalMap.wrapS = or),
    (plasticNormalMap.wrapT = or),
    plasticNormalMap.repeat.set(0.22, 0.22),
    (plasticNormalMap.generateMipmaps = !0),
    (plasticNormalMap.minFilter = mr),
    (plasticNormalMap.needsUpdate = !0),
    plasticNormalMap
  );
}
var glitterNormalMap = null;
function getGlitterNormalMap() {
  if (glitterNormalMap) return glitterNormalMap;
  let e = new Uint8Array(262144),
    t = 3,
    n = () => ((t = (t * 16807) % 2147483647), t / 2147483647);
  for (let t = 0; t < 65536; t++) ((e[t * 4] = 128), (e[t * 4 + 1] = 128), (e[t * 4 + 2] = 255), (e[t * 4 + 3] = 255));
  for (let t = 0; t < 2600; t++) {
    let t = Math.floor(n() * 256),
      r = Math.floor(n() * 256),
      i = n() * 2 - 1,
      a = n() * 2 - 1,
      o = n() < 0.3 ? 2 : 1;
    for (let n = -o; n < o; n++)
      for (let s = -o; s < o; s++) {
        let o = (((r + n + 256) % 256) * 256 + ((t + s + 256) % 256)) * 4;
        ((e[o] = (i * 0.5 + 0.5) * 255), (e[o + 1] = (a * 0.5 + 0.5) * 255), (e[o + 2] = 190));
      }
  }
  return (
    (glitterNormalMap = new Qs(e, 256, 256, Ar)),
    (glitterNormalMap.wrapS = or),
    (glitterNormalMap.wrapT = or),
    glitterNormalMap.repeat.set(0.09, 0.09),
    (glitterNormalMap.needsUpdate = !0),
    glitterNormalMap
  );
}
var wearMaps = [];
function getWearMap(e) {
  if (wearMaps[e]) return wearMaps[e];
  let t = document.createElement(`canvas`);
  ((t.width = 512), (t.height = 512));
  let n = t.getContext(`2d`);
  ((n.fillStyle = `rgb(128,128,128)`), n.fillRect(0, 0, 512, 512));
  let r = 11 + e,
    i = () => ((r = (r * 16807) % 2147483647), r / 2147483647),
    a = [0, 26, 70, 150][e];
  n.lineCap = `round`;
  for (let e = 0; e < a; e++) {
    let e = i() * 512,
      t = i() * 512,
      r = i() * Math.PI,
      a = 8 + i() * 70,
      o = i() < 0.8 ? 205 + i() * 50 : 60;
    ((n.strokeStyle = `rgba(${o},${o},${o},${0.35 + i() * 0.5})`),
      (n.lineWidth = 0.6 + i() * 1.4),
      n.beginPath(),
      n.moveTo(e, t),
      n.quadraticCurveTo(
        e + Math.cos(r) * a * 0.5 + (i() - 0.5) * 8,
        t + Math.sin(r) * a * 0.5 + (i() - 0.5) * 8,
        e + Math.cos(r) * a,
        t + Math.sin(r) * a,
      ),
      n.stroke());
  }
  for (let t = 0; t < e * 5; t++) {
    let e = i() * 512,
      t = i() * 512,
      r = 12 + i() * 40,
      a = n.createRadialGradient(e, t, 0, e, t, r),
      o = i() < 0.5 ? 200 : 90;
    (a.addColorStop(0, `rgba(${o},${o},${o},0.35)`),
      a.addColorStop(1, `rgba(${o},${o},${o},0)`),
      (n.fillStyle = a),
      n.fillRect(e - r, t - r, r * 2, r * 2));
  }
  let o = new pc(t);
  return ((o.wrapS = or), (o.wrapT = or), o.repeat.set(1 / 70, 1 / 70), (wearMaps[e] = o), o);
}
function ageShellColor(e, t) {
  if (!t) return e;
  let n = new wo(e);
  return n.r * 0.3 + n.g * 0.55 + n.b * 0.15 > 0.45 ? C(e, 0.06 * t, `#c9b476`) : e;
}
function createShellMaterial(e, t, n, r = 0) {
  let i = ageShellColor(e, r),
    a =
      r > 0 && !n
        ? {
            roughnessMap: getWearMap(Math.min(3, r)),
            roughness: 1,
          }
        : {};
  return t === `translucent` || t === `clear`
    ? new Ql({
        color: i,
        roughness: 0.14,
        metalness: 0,
        clearcoat: n ? 0 : 0.8,
        clearcoatRoughness: 0.12,
        transparent: !0,
        opacity: t === `clear` ? 0.3 : 0.5,
        depthWrite: !1,
        side: 2,
      })
    : n
      ? t === `metallic`
        ? new Zl({
            color: i,
            roughness: 0.35,
            metalness: 0.8,
          })
        : t === `glow`
          ? new Zl({
              color: i,
              roughness: 0.5,
              emissive: i,
              emissiveIntensity: 0.45,
            })
          : new Zl({
              color: i,
              roughness: 0.52,
              metalness: 0,
            })
      : t === `metallic`
        ? new Ql({
            color: i,
            roughness: 0.3,
            metalness: 0.85,
            clearcoat: 0.5,
            clearcoatRoughness: 0.25,
            normalMap: getPlasticNormalMap(),
            normalScale: new X(0.05, 0.05),
            ...(r
              ? {
                  roughnessMap: getWearMap(Math.min(3, r)),
                  roughness: 0.62,
                }
              : {}),
          })
        : t === `glitter`
          ? new Ql({
              color: i,
              roughness: 0.28,
              metalness: 0.35,
              clearcoat: 1,
              clearcoatRoughness: 0.08,
              normalMap: getGlitterNormalMap(),
              normalScale: new X(1, 1),
              ...a,
              ...(r
                ? {
                    roughness: 0.56,
                  }
                : {}),
            })
          : t === `glow`
            ? new Ql({
                color: i,
                roughness: 0.45,
                metalness: 0,
                clearcoat: 0.3,
                clearcoatRoughness: 0.4,
                emissive: i,
                emissiveIntensity: 0.42,
                transparent: !0,
                opacity: 0.9,
              })
            : new Ql({
                color: i,
                roughness: 0.5,
                metalness: 0,
                clearcoat: 0.18,
                clearcoatRoughness: 0.55,
                normalMap: getPlasticNormalMap(),
                normalScale: new X(0.07, 0.07),
                ...a,
              });
}
function createLabelMaterial(e) {
  return new Ql({
    map: e,
    roughness: 0.46,
    metalness: 0,
    clearcoat: 0.32,
    clearcoatRoughness: 0.34,
    envMapIntensity: 0.8,
    polygonOffset: !0,
    polygonOffsetFactor: -2,
    polygonOffsetUnits: -2,
  });
}
var studioEnvironmentPromise = null,
  studioEnvironmentTexture = null;
function loadStudioEnvironment() {
  return (
    (studioEnvironmentPromise ||= new Bm()
      .loadAsync(Vm)
      .then((e) => ((e.mapping = 303), (studioEnvironmentTexture = e), e))
      .catch(() => null)),
    studioEnvironmentPromise
  );
}
function applyEnvironmentMap(e) {
  let { renderer: t, scene: n } = e,
    r = new Ud(t);
  if (studioEnvironmentTexture) {
    ((n.environment = r.fromEquirectangular(studioEnvironmentTexture).texture),
      n.environmentRotation.set(0, -0.5, 0),
      r.dispose());
    return;
  }
  ((n.environment = r.fromScene(new Rm(), 0.04).texture),
    loadStudioEnvironment().then((t) => {
      if (e.disposed || !t) {
        r.dispose();
        return;
      }
      let i = r.fromEquirectangular(t).texture;
      (r.dispose(),
        n.environment && n.environment.dispose(),
        (n.environment = i),
        n.environmentRotation.set(0, -0.5, 0),
        (e.dirty = !0));
    }));
}
function resolveRenderQuality() {
  let e = getCartState().settings && getCartState().settings.ui.quality;
  return e === `balanced` || e === `low` ? e : `high`;
}
var lerp = (e, t, n) => e + (t - e) * n,
  easeOutQuad = (e) => 1 - (1 - e) * (1 - e),
  easeInCubic = (e) => e * e * e,
  CartStageRenderer = class {
    constructor(e, { onPick: t, onHover: n } = {}) {
      ((this.canvas = e),
        (this.onPick = t),
        (this.onHover = n),
        (this.items = new Map()),
        (this.pool = new Map()),
        (this.poolSize = 0),
        (this.w = 1),
        (this.h = 1),
        (this.last = performance.now()),
        (this.q = resolveRenderQuality()));
      let r = new Lm({
        canvas: e,
        antialias: this.q !== `low`,
        alpha: !0,
        powerPreference: `high-performance`,
      });
      (r.setPixelRatio(
        Math.min(
          window.devicePixelRatio || 1,
          {
            high: 2,
            balanced: 1.5,
            low: 1,
          }[this.q],
        ),
      ),
        (r.outputColorSpace = Oi),
        (r.toneMapping = 7),
        (r.toneMappingExposure = 1),
        r.setClearColor(0, 0),
        (r.debug.checkShaderErrors = !1),
        (this.renderer = r));
      let i = new Eo();
      ((this.scene = i),
        applyEnvironmentMap(this),
        (i.environmentIntensity = 0.85),
        i.add(new Au(16777215, 2763318, 0.25)));
      let a = new qu(16774892, 1.5);
      (a.position.set(-0.55, 0.85, 1.1), i.add(a));
      let o = new qu(13161215, 1);
      (o.position.set(0.9, 0.35, -0.7),
        i.add(o),
        (this.scene = i),
        (this.camera = new Hu(CAMERA_FOV, 1, 1, 1e4)),
        (this.raycaster = new dd()),
        (this.pointer = new X()),
        (this.hovered = null),
        (this.hoverLocal = {
          x: 0,
          y: 0,
        }),
        (this.pressed = null),
        (this.par = {
          x: 0,
          y: 0,
        }),
        (this.parCur = {
          x: 0,
          y: 0,
        }),
        (this.onMove = (e) => this.pick(e, `hover`)),
        (this.onClick = (e) => this.pick(e, `click`)),
        (this.onDown = (e) => {
          let t = this.hitTest(e);
          t && ((this.pressed = t.key), (this.dirty = !0));
        }),
        (this.onUp = () => {
          this.pressed && ((this.pressed = null), (this.dirty = !0));
        }),
        (this.onLeave = () => {
          ((this.par = {
            x: 0,
            y: 0,
          }),
            (this.pressed = null),
            (this.canvas.style.cursor = ``),
            this.hovered !== null && ((this.hovered = null), this.onHover && this.onHover(null)),
            (this.dirty = !0));
        }),
        e.addEventListener(`pointermove`, this.onMove),
        e.addEventListener(`click`, this.onClick),
        e.addEventListener(`pointerdown`, this.onDown),
        e.addEventListener(`pointerleave`, this.onLeave),
        window.addEventListener(`pointerup`, this.onUp),
        (this.loop = this.loop.bind(this)),
        (this.raf = requestAnimationFrame(this.loop)));
    }
    setSize(e, t) {
      ((this.dirty = !0),
        (this.w = Math.max(1, e)),
        (this.h = Math.max(1, t)),
        this.renderer.setSize(this.w, this.h, !1));
      let n = this.camera;
      ((n.aspect = this.w / this.h),
        n.position.set(0, 0, this.h / 2 / Math.tan(va.degToRad(CAMERA_FOV / 2))),
        (n.near = n.position.z * 0.35),
        (n.far = n.position.z * 2.5),
        n.updateProjectionMatrix());
    }
    create(e) {
      let t = this.unpark(e);
      if (t) return t;
      let n = getCachedGeometry(e.modelKey, e.disc),
        r = new _o(),
        i = new _o();
      r.add(i);
      let a = [],
        o = (t, n, r = {}) => {
          if (!t) return null;
          let o = new Ys(t, n);
          return ((o.userData.key = e.key), (r.parent || i).add(o), r.own && a.push(n), o);
        },
        s = null;
      if (n.kind === `disc`)
        ((s = new _o()),
          i.add(s),
          o(n.plastic, createDiscMaterial(n.under), {
            parent: s,
            own: !0,
          }),
          o(
            n.hub,
            new Ql({
              color: `#eef1f6`,
              transparent: !0,
              opacity: 0.4,
              roughness: 0.08,
            }),
            {
              parent: s,
              own: !0,
            },
          ),
          o(
            n.stack,
            new Zl({
              color: `#ffffff`,
              transparent: !0,
              opacity: 0.5,
              roughness: 0.2,
            }),
            {
              parent: s,
              own: !0,
            },
          ));
      else {
        let t = e.color || n.color,
          r = e.finish || (n.clear ? `translucent` : `solid`);
        (o(n.plastic, createShellMaterial(t, r, this.q === `low`, e.wear || 0), {
          own: !0,
        }),
          o(
            n.dark,
            new Zl({
              color: C(t, 0.42, `#000000`),
              roughness: 0.85,
            }),
            {
              own: !0,
            },
          ),
          o(n.metal, contactMaterial),
          o(n.screws, screwMaterial),
          n.pcb && (r === `translucent` || r === `clear`) && o(n.pcb, pcbMaterial),
          n.window &&
            o(n.window, createDiscMaterial(`silver`), {
              own: !0,
            }),
          n.glass &&
            o(
              n.glass,
              new Ql({
                color: `#ffffff`,
                transparent: !0,
                opacity: 0.1,
                roughness: 0.04,
                metalness: 0,
                clearcoat: 1,
                clearcoatRoughness: 0.03,
                depthWrite: !1,
              }),
              {
                own: !0,
              },
            ));
      }
      let c = null,
        l = null;
      if (n.label && e.label) {
        let t = n.kind === `disc` ? 0 : e.wear || 0;
        l = labelTextureKey(e.label, n.label.aspect) + (t ? `|w${t}|${e.seed || ``}` : ``);
        let r = acquireLabelTexture(l, () =>
          createLabelTexture(
            e.label,
            n.label.aspect,
            t
              ? {
                  wear: t,
                  seed: String(e.seed || ``),
                }
              : null,
          ),
        );
        c = o(n.label.geo, createLabelMaterial(r), {
          parent: s || i,
          own: !0,
        });
      }
      let u = new Ys(
        new Ll(1, 1),
        new Ls({
          map: getShadowTexture(),
          transparent: !0,
          depthWrite: !1,
          opacity: 0.8,
        }),
      );
      ((u.renderOrder = -1), this.scene.add(u), this.scene.add(r));
      let d = e;
      return {
        key: e.key,
        sig: this.sig(e),
        model: n,
        group: r,
        inner: i,
        spin: s,
        mats: a,
        labelMesh: c,
        lk: l,
        shadow: u,
        cur: {
          x: d.x,
          y: d.y,
          z: d.z - 60,
          rx: d.rx || 0,
          ry: d.ry || 0,
          rz: d.rz || 0,
          s: d.size * 0.9,
          o: 0,
        },
        tgt: {},
        hero: 0,
        heroSince: performance.now(),
        insertT0: 0,
        leaving: !1,
        hov: 0,
        press: 0,
        tx: 0,
        ty: 0,
      };
    }
    sig(e) {
      let t = !e.disc && CART_SPECS[e.modelKey] && CART_SPECS[e.modelKey].engrave ? currentEngravingId() : ``;
      return `${e.modelKey}|${+!!e.disc}|${e.color || ``}|${e.finish || ``}|${e.wear || 0}|${t}|${e.label ? labelTextureKey(e.label, 1) : ``}`;
    }
    park(e) {
      ((e.group.visible = !1), (e.shadow.visible = !1), (e.parkedAt = performance.now()));
      let t = this.pool.get(e.sig) || [];
      for (t.push(e), this.pool.set(e.sig, t), this.poolSize++; this.poolSize > MAX_POOLED_ITEMS;) {
        let e = null,
          t = 1 / 0;
        for (let [n, r] of this.pool) r[0].parkedAt < t && ((t = r[0].parkedAt), (e = n));
        let n = this.pool.get(e);
        (this.destroy(n.shift()), n.length || this.pool.delete(e), this.poolSize--);
      }
    }
    unpark(e) {
      let t = this.sig(e),
        n = this.pool.get(t);
      if (!n || !n.length) return null;
      let r = n.pop();
      return (
        n.length || this.pool.delete(t),
        this.poolSize--,
        (r.key = e.key),
        r.group.traverse((t) => {
          t.isMesh && (t.userData.key = e.key);
        }),
        (r.cur = {
          x: e.x,
          y: e.y,
          z: (e.z || 0) - 60,
          rx: e.rx || 0,
          ry: e.ry || 0,
          rz: e.rz || 0,
          s: e.size * 0.9,
          o: 0,
        }),
        (r.tgt = {}),
        (r.hero = 0),
        (r.heroSince = performance.now()),
        (r.insertT0 = 0),
        (r.leaving = !1),
        (r.hov = 0),
        (r.press = 0),
        (r.tx = 0),
        (r.ty = 0),
        (r.group.visible = !0),
        r
      );
    }
    destroy(e) {
      (this.scene.remove(e.group),
        this.scene.remove(e.shadow),
        e.shadow.geometry.dispose(),
        e.shadow.material.dispose());
      for (let t of e.mats) t.dispose();
      e.lk && releaseLabelTexture(e.lk);
    }
    sync(e) {
      this.dirty = !0;
      let t = new Set();
      for (let n of e) {
        t.add(n.key);
        let e = this.items.get(n.key);
        if (e && e.sig !== this.sig(n)) {
          let t = e.cur;
          (this.destroy(e), (e = this.create(n)), (e.cur = t), this.items.set(n.key, e));
        }
        (e || ((e = this.create(n)), this.items.set(n.key, e)),
          (e.leaving = !1),
          (e.tgt = {
            x: n.x,
            y: n.y,
            z: n.z || 0,
            rx: n.rx || 0,
            ry: n.ry || 0,
            rz: n.rz || 0,
            s: n.size,
            o: n.opacity ?? 1,
          }),
          n.hero && !e.isHero && (e.heroSince = performance.now()),
          (e.isHero = !!n.hero),
          n.inserting && !e.insertT0 && (e.insertT0 = performance.now()),
          (e.insertStyle = n.insert || `down`),
          n.inserting || (e.insertT0 = 0),
          (e.instant = !!n.instant),
          (e.shadowOn = n.shadow !== !1));
      }
      for (let [e, n] of this.items)
        !t.has(e) &&
          !n.leaving &&
          ((n.leaving = !0),
          (n.tgt = {
            ...n.tgt,
            o: 0,
            s: (n.tgt.s || 1) * 0.85,
          }));
    }
    hitTest(e) {
      let t = this.canvas.getBoundingClientRect();
      (this.pointer.set(((e.clientX - t.left) / t.width) * 2 - 1, -((e.clientY - t.top) / t.height) * 2 + 1),
        this.raycaster.setFromCamera(this.pointer, this.camera));
      let n = [];
      for (let e of this.items.values()) !e.leaving && e.group.visible && n.push(e.group);
      let r = this.raycaster.intersectObjects(n, !0)[0];
      if (!r) return null;
      let i = r.object.userData.key,
        a = this.items.get(i);
      if (!a) return null;
      let o = a.group.worldToLocal(r.point.clone()),
        s = a.model,
        c = (e) => Math.max(-1, Math.min(1, e));
      return {
        key: i,
        local: {
          x: c(o.x / (s.w / 2)),
          y: c(o.y / (s.h / 2)),
        },
      };
    }
    pick(e, t) {
      let n = this.hitTest(e),
        r = n ? n.key : null;
      ((this.canvas.style.cursor = r ? `pointer` : ``),
        t === `click` && r && this.onPick && this.onPick(r),
        t === `hover` &&
          ((this.par = {
            x: this.pointer.x,
            y: this.pointer.y,
          }),
          (this.hoverLocal = n
            ? n.local
            : {
                x: 0,
                y: 0,
              }),
          (this.dirty = !0),
          r !== this.hovered && ((this.hovered = r), this.onHover && this.onHover(r))));
    }
    loop(e) {
      if (((this.raf = requestAnimationFrame(this.loop)), document.hidden || getCartState().running)) {
        this.last = e;
        return;
      }
      let t = getLabelPaintVersion(),
        n = this.dirty || t !== this.paints,
        r = !1;
      for (let e of this.items.values()) {
        let t = e.cur,
          i = e.tgt;
        (e.insertT0 ||
          e.leaving ||
          Math.abs(t.x - i.x) + Math.abs(t.y - i.y) + Math.abs(t.z - i.z) > 0.3 ||
          Math.abs(t.rx - i.rx) + Math.abs(t.ry - i.ry) + Math.abs(t.rz - i.rz) > 0.002 ||
          Math.abs(t.s - i.s) > 0.3 ||
          Math.abs(t.o - (i.o ?? 1)) > 0.004 ||
          Math.abs(e.hero - +!!e.isHero) > 0.004) &&
          (n = !0);
        let a = e.key === this.hovered;
        ((Math.abs(e.hov - +!!a) > 0.004 ||
          Math.abs(e.press - +(e.key === this.pressed)) > 0.004 ||
          Math.abs(e.tx - (a ? this.hoverLocal.x : 0)) > 0.004 ||
          Math.abs(e.ty - (a ? this.hoverLocal.y : 0)) > 0.004) &&
          (n = !0),
          (e.isHero || e.hov > 0.01) && this.q !== `low` && (r = !0));
      }
      if ((Math.abs(this.parCur.x - this.par.x) + Math.abs(this.parCur.y - this.par.y) > 0.002 && (n = !0), !n && !r)) {
        this.last = e;
        return;
      }
      if (!n && e - this.lastDraw < 32) return;
      ((this.lastDraw = e), (this.dirty = !1), (this.paints = t));
      let i = Math.min(0.05, (e - this.last) / 1e3);
      this.last = e;
      let a = 1 - Math.exp(-i * 9),
        o = 1 - Math.exp(-i * 7);
      e / 1e3;
      let s = 1 - Math.exp(-i * 10),
        c = 1 - Math.exp(-i * 8),
        l = 1 - Math.exp(-i * 18);
      ((this.parCur.x = lerp(this.parCur.x, this.par.x, 1 - Math.exp(-i * 5))),
        (this.parCur.y = lerp(this.parCur.y, this.par.y, 1 - Math.exp(-i * 5))));
      for (let [t, n] of this.items) {
        let r = n.cur,
          u = n.tgt,
          d = n.instant ? 1 : a;
        ((r.x = lerp(r.x, u.x, d)),
          (r.y = lerp(r.y, u.y, d)),
          (r.z = lerp(r.z, u.z, d)),
          (r.rx = lerp(r.rx, u.rx, d)),
          (r.ry = lerp(r.ry, u.ry, d)),
          (r.rz = lerp(r.rz, u.rz, d)),
          (r.s = lerp(r.s, u.s, d)),
          (r.o = lerp(r.o, u.o, n.instant ? 1 : o)),
          (n.hero = lerp(n.hero, n.isHero && !n.insertT0 ? 1 : 0, 1 - Math.exp(-i * 4))));
        let f = t === this.hovered && !n.insertT0;
        if (
          ((n.hov = lerp(n.hov, +!!f, s)),
          (n.press = lerp(n.press, +(t === this.pressed), l)),
          (n.tx = lerp(n.tx, f ? this.hoverLocal.x : 0, c)),
          (n.ty = lerp(n.ty, f ? this.hoverLocal.y : 0, c)),
          n.leaving && r.o < 0.02)
        ) {
          (this.park(n), this.items.delete(t));
          continue;
        }
        let p = n.model,
          m = r.s / p.h,
          h = 0,
          g = 0,
          _ = 0,
          v = 0,
          y = 1;
        if (n.insertT0) {
          let t = Math.min(1, (e - n.insertT0) / INSERT_DURATION_MS),
            i = easeOutQuad(Math.min(1, t / 0.34)),
            a = t < 0.34 ? 0 : easeInCubic((t - 0.34) / 0.66);
          ((y = Math.max(0, 1 - t * 2.5)),
            n.insertStyle === `front`
              ? ((v = -i * 1.32), (h = i * 0.06 * r.s - a * 0.35 * r.s), (g = -a * 2.6 * r.s))
              : (n.insertStyle === `away` && (_ = i * Math.PI),
                (h = t < 0.34 ? i * 0.1 * r.s : 0.1 * r.s - a * 2.4 * r.s)));
        }
        let b = n.hov * (1 - 0.6 * n.press);
        (n.group.position.set(r.x, -r.y + h + r.s * 0.035 * b, r.z + g + r.s * 0.14 * b),
          n.group.rotation.set(r.rx + v, r.ry + _, r.rz),
          n.group.scale.setScalar(m * (1 + 0.045 * n.hov - 0.05 * n.press)),
          (n.group.visible = r.o > 0.01));
        let x = (e - n.heroSince) / 1e3,
          S = n.hero,
          C = S * (1 - 0.75 * n.hov),
          w = -n.ty * 0.3 - this.parCur.y * 0.05,
          T = n.tx * 0.42 + this.parCur.x * 0.08;
        p.kind === `disc`
          ? (n.inner.rotation.set(-0.42 * S + w, Math.sin(x * 0.6) * 0.12 * C + T, 0),
            (n.inner.position.y = Math.sin(x * 1.1) * p.h * 0.02 * C),
            n.spin && (n.spin.rotation.z -= i * (0.25 + 0.9 * S + 2.2 * n.hov)))
          : (n.inner.rotation.set((0.06 + Math.sin(x * 0.9) * 0.05) * C + w, Math.sin(x * 0.7) * 0.32 * C + T, 0),
            (n.inner.position.y = Math.sin(x * 1.1) * p.h * 0.025 * C));
        let E = r.o > 0.985;
        for (let e of n.mats) {
          let t = e.userData.baseOpacity ?? (e.userData.baseOpacity = e.opacity),
            n = e.userData.baseTransparent ?? (e.userData.baseTransparent = e.transparent);
          e.opacity = t * r.o;
          let i = n || !E;
          e.transparent !== i && ((e.transparent = i), (e.needsUpdate = !0));
        }
        let D = p.w * m;
        (n.shadow.position.set(r.x, -r.y - r.s * 0.6 + h * 0.2, r.z - r.s * 0.3),
          n.shadow.scale.set(D * (1.05 - 0.12 * Math.sin(x * 1.1) * S) * (1 + 0.08 * n.hov), r.s * 0.16, 1),
          (n.shadow.material.opacity = 0.5 * r.o * y * (1 - 0.3 * n.hov)),
          (n.shadow.visible = n.group.visible && n.shadowOn !== !1));
      }
      this.renderer.render(this.scene, this.camera);
    }
    dispose() {
      this.disposed = !0;
      for (let e of this.pool.values()) for (let t of e) this.destroy(t);
      (this.pool.clear(),
        (this.poolSize = 0),
        cancelAnimationFrame(this.raf),
        this.canvas.removeEventListener(`pointermove`, this.onMove),
        this.canvas.removeEventListener(`click`, this.onClick),
        this.canvas.removeEventListener(`pointerdown`, this.onDown),
        this.canvas.removeEventListener(`pointerleave`, this.onLeave),
        window.removeEventListener(`pointerup`, this.onUp));
      for (let e of this.items.values()) this.destroy(e);
      (this.items.clear(), this.renderer.dispose());
    }
  },
  SHELL_FINISH_OPTIONS = [
    {
      id: `solid`,
      label: `Solid`,
    },
    {
      id: `translucent`,
      label: `Translucent`,
    },
    {
      id: `clear`,
      label: `Clear`,
    },
    {
      id: `metallic`,
      label: `Metallic`,
    },
    {
      id: `glitter`,
      label: `Glitter`,
    },
    {
      id: `glow`,
      label: `Glow`,
    },
  ],
  SHELL_COLOR_OPTIONS = [
    {
      name: `Classic Grey`,
      hex: `#a7a3ae`,
    },
    {
      name: `Charcoal`,
      hex: `#3b3c42`,
    },
    {
      name: `Black`,
      hex: `#1d1d21`,
    },
    {
      name: `White`,
      hex: `#ecedf0`,
    },
    {
      name: `Atomic Purple`,
      hex: `#6f4fb0`,
    },
    {
      name: `Grape`,
      hex: `#5a2f8e`,
    },
    {
      name: `Berry`,
      hex: `#c22e6e`,
    },
    {
      name: `Bubblegum`,
      hex: `#f39ac0`,
    },
    {
      name: `Fire Red`,
      hex: `#d8342b`,
    },
    {
      name: `Ruby`,
      hex: `#a8102a`,
    },
    {
      name: `Tangerine`,
      hex: `#f07a22`,
    },
    {
      name: `Dandelion`,
      hex: `#f2c230`,
    },
    {
      name: `Kiwi`,
      hex: `#8cc63f`,
    },
    {
      name: `Jungle Green`,
      hex: `#1f8a4c`,
    },
    {
      name: `Teal`,
      hex: `#18a0a0`,
    },
    {
      name: `Ice Blue`,
      hex: `#8fd3e8`,
    },
    {
      name: `Sapphire`,
      hex: `#1e3fa8`,
    },
    {
      name: `Midnight`,
      hex: `#1f2a52`,
    },
    {
      name: `Gold`,
      hex: `#c9a349`,
    },
    {
      name: `Silver`,
      hex: `#b9bcc2`,
    },
  ],
  DEFAULT_SHELL_COLORS = {
    solid: `#6f4fb0`,
    translucent: `#6f4fb0`,
    clear: `#e8ecf2`,
    metallic: `#c9a349`,
    glitter: `#5a2f8e`,
    glow: `#b8f0a0`,
  },
  POKEMON_TITLE_PATTERN = `(Pokemon|Pocket Monsters)`,
  SPECIAL_CART_LOOKS = [
    {
      sys: [`gb`, `gbc`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Red Version|Rote Edition|Version Rouge|Versione Rossa|Edicion Roja)`,
        `i`,
      ),
      not: /\(Japan\)/,
      color: `#c8242c`,
      finish: `solid`,
    },
    {
      sys: [`gb`, `gbc`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Blue Version|Blaue Edition|Version Bleue|Versione Blu|Edicion Azul)`,
        `i`,
      ),
      not: /\(Japan\)/,
      color: `#2a4fa8`,
      finish: `solid`,
    },
    {
      sys: [`gb`, `gbc`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Yellow Version|Gelbe Edition|Version Jaune|Versione Gialla|Edicion Amarilla)`,
        `i`,
      ),
      not: /\(Japan\)/,
      color: `#f2c81e`,
      finish: `solid`,
    },
    {
      sys: [`gb`, `gbc`],
      re: /^Donkey Kong Land/i,
      not: /\(Japan\)/,
      color: `#f5c518`,
      finish: `solid`,
    },
    {
      sys: [`gb`, `gbc`],
      re: RegExp(`^${POKEMON_TITLE_PATTERN} - (Gold Version|Goldene Edition|Version Or|Versione Oro|Edicion Oro)`, `i`),
      not: /\(Japan\)/,
      color: `#c9a349`,
      finish: `metallic`,
    },
    {
      sys: [`gb`, `gbc`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Silver Version|Silberne Edition|Version Argent|Versione Argento|Edicion Plata)`,
        `i`,
      ),
      not: /\(Japan\)/,
      color: `#b9bcc2`,
      finish: `metallic`,
    },
    {
      sys: [`gb`, `gbc`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Crystal Version|Kristall-Edition|Version Cristal|Versione Cristallo|Edicion Cristal)`,
        `i`,
      ),
      color: `#8fd3e8`,
      finish: `translucent`,
    },
    {
      sys: [`gb`, `gbc`],
      re: /^(Kirby Tilt 'n' Tumble|Koro Koro Kirby)/i,
      color: `#f39ac0`,
      finish: `translucent`,
    },
    {
      sys: [`gb`, `gbc`],
      re: /^Pokemon Pinball/i,
      not: /\(Japan\)/,
      color: `#1e1e1e`,
      finish: `solid`,
    },
    {
      sys: [`gba`],
      re: RegExp(`^${POKEMON_TITLE_PATTERN} - (Ruby|Rubin|Version Rubis|Versione Rubino|Edicion Rubi)`, `i`),
      color: `#a8102a`,
      finish: `translucent`,
    },
    {
      sys: [`gba`],
      re: RegExp(`^${POKEMON_TITLE_PATTERN} - (Sapphire|Saphir|Version Saphir|Versione Zaffiro|Edicion Zafiro)`, `i`),
      color: `#1e3fa8`,
      finish: `translucent`,
    },
    {
      sys: [`gba`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Emerald|Smaragd|Version Emeraude|Versione Smeraldo|Edicion Esmeralda)`,
        `i`,
      ),
      color: `#1a8a4a`,
      finish: `translucent`,
    },
    {
      sys: [`gba`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Fire ?Red|Feuerrote|Version Rouge Feu|Versione Rosso Fuoco|Edicion Rojo Fuego)`,
        `i`,
      ),
      color: `#e0452a`,
      finish: `translucent`,
    },
    {
      sys: [`gba`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (Leaf ?Green|Blattgrune|Version Vert Feuille|Versione Verde Foglia|Edicion Verde Hoja)`,
        `i`,
      ),
      color: `#5cae3c`,
      finish: `translucent`,
    },
    {
      sys: [`gba`],
      re: /^(Boktai|Bokura no Taiyou|Zoku Bokura no Taiyou)/i,
      color: `#e8eef0`,
      finish: `clear`,
    },
    {
      sys: [`gba`],
      re: /^(Yoshi Topsy-Turvy|Yoshi's Universal Gravitation)/i,
      not: /\(Japan\)/,
      color: `#4caf50`,
      finish: `solid`,
    },
    {
      sys: [`nes`],
      re: /^(Legend of Zelda, The|Zelda II - The Adventure of Link)/i,
      not: /\(Japan\)/,
      color: `#c9a44a`,
      finish: `metallic`,
    },
    {
      sys: [`snes`],
      re: /^Killer Instinct/i,
      only: /\(USA/,
      color: `#1c1c1c`,
      finish: `solid`,
    },
    {
      sys: [`snes`],
      re: /^(Doom|Spider-Man (&|and) Venom - Maximum Carnage)\b/i,
      only: /\(USA/,
      color: `#b3262a`,
      finish: `solid`,
    },
    {
      sys: [`genesis`],
      re: /^Spider-Man (&|and) Venom - Maximum Carnage/i,
      only: /\(USA/,
      color: `#b3262a`,
      finish: `solid`,
    },
    {
      sys: [`n64`],
      re: /^Legend of Zelda, The - Ocarina of Time \(USA\)(?!.*Rev)/i,
      color: `#c9a44a`,
      finish: `metallic`,
    },
    {
      sys: [`n64`],
      re: /^Legend of Zelda, The - Majora's Mask/i,
      only: /\((USA|Europe)/,
      color: `#c9a44a`,
      finish: `metallic`,
    },
    {
      sys: [`n64`],
      re: /^Pokemon Stadium 2/i,
      only: /\((USA|Europe)/,
      color: `#c9a44a`,
      finish: `metallic`,
    },
    {
      sys: [`n64`],
      re: /^(Donkey Kong 64|Earthworm Jim 3D|Tony Hawk's Pro Skater 2)/i,
      only: /\(USA/,
      color: `#f2c522`,
      finish: `solid`,
    },
    {
      sys: [`n64`],
      re: /^(All-Star Baseball 2001|Jeremy McGrath Supercross 2000|NBA Jam 2000|NFL Quarterback Club 2001|Rocket - Robot on Wheels|Spider-Man)\b/i,
      only: /\(USA/,
      color: `#b3262a`,
      finish: `solid`,
    },
    {
      sys: [`n64`],
      re: /^(007 - The World Is Not Enough|Bass Masters 2000|Hydro Thunder|Madden NFL 2001|Tony Hawk's Pro Skater)\b(?! 2| 3)/i,
      only: /\(USA/,
      color: `#2c56a8`,
      finish: `solid`,
    },
    {
      sys: [`n64`],
      re: /^(Army Men - Air Combat|Army Men - Sarge's Heroes 2|BattleTanx - Global Assault|Rayman 2)/i,
      only: /\(USA/,
      color: `#3f7a3a`,
      finish: `solid`,
    },
    {
      sys: [`n64`],
      re: /^(Aidyn Chronicles|Armorines|Batman Beyond|Battlezone|ECW Hardcore Revolution|Fighter Destiny 2|Madden NFL 2002|Nuclear Strike 64|Power Rangers - Lightspeed Rescue|Rally Challenge 2000|Road Rash 64|Rugrats in Paris|Scooby-Doo! - Classic Creep Capers|Tom Clancy's Rainbow Six|Tony Hawk's Pro Skater 3|Turok 2|Turok 3|Turok - Rage Wars|WWF No Mercy|WWF WrestleMania 2000)/i,
      only: /\(USA/,
      color: `#1c1c1c`,
      finish: `solid`,
    },
    {
      sys: [`nds`],
      re: RegExp(
        `^${POKEMON_TITLE_PATTERN} - (HeartGold|SoulSilver|Black|White|Black Version 2|White Version 2|Black 2|White 2)`,
        `i`,
      ),
      color: `#1a1418`,
      finish: `solid`,
    },
  ];
function findSpecialEditionLook(e) {
  if (!e || !e.file) return null;
  let t = e.file;
  for (let n of SPECIAL_CART_LOOKS)
    if (n.sys.includes(e.system) && n.re.test(t) && !(n.not && n.not.test(t)) && (!n.only || n.only.test(t)))
      return {
        finish: n.finish,
        color: n.color,
        special: !0,
      };
  return null;
}
var isCartLook = (e) => e && e.finish && e.color;
function resolveCartLook(e, t, n) {
  let r = (n && n.cartLooks) || {},
    i = (n && n.ui) || {};
  if (e && isCartLook(r[`g:${e.id}`])) return r[`g:${e.id}`];
  if (e && i.specialCarts !== !1) {
    let t = findSpecialEditionLook(e);
    if (t) return t;
  }
  return isCartLook(r[`s:${t}`]) ? r[`s:${t}`] : null;
}
function resolveWearLevel(e, t) {
  let n = (t && t.ui && t.ui.wear) || `off`;
  if (n === `off` || !e) return 0;
  if (n === `loved`) return 3;
  let r = (e.playtime || 0) / 3600;
  return r < 1 ? 0 : r < 8 ? 1 : r < 30 ? 2 : 3;
}
var INSERT_DIRECTIONS = {
    gb: `away`,
    gbc: `away`,
    gba: `away`,
    ds: `away`,
    "3ds": `away`,
    switch: `away`,
    umd: `away`,
    nes: `front`,
  },
  webglSupport = null;
function hasWebGL() {
  if (webglSupport === null)
    try {
      let e = document.createElement(`canvas`);
      webglSupport = !!(e.getContext(`webgl2`) || e.getContext(`webgl`));
    } catch {
      webglSupport = !1;
    }
  return webglSupport;
}
function describeGameCart(e, t, n = `classic`, r = null) {
  let { spec: i, key: a, disc: o } = resolveMediaSpec(t),
    s = !!o,
    c = s
      ? t.media.shape in
        {
          cd: 1,
          dvd: 1,
          bluray: 1,
          psx: 1,
          gdrom: 1,
          minidisc: 1,
        }
        ? t.media.shape
        : `cd`
      : a,
    l = {
      kind: `gen`,
      color: t.color,
      band: t.short,
      title: e ? e.title : t.name,
      round: s,
      umd: a === `umd`,
    },
    u = l,
    d = e && w(e, `box`);
  d &&
    (u = {
      kind: `art`,
      url: d,
      crop: e.localArt ? null : BOX_ART_CROPS[t.id],
      style: i && i.labelStyle,
      fallback: l,
    });
  let f = e && !e.localArt && w(e, `label`);
  f &&
    (u = {
      kind: `art`,
      url: f,
      style: `scan`,
      umd: a === `umd`,
      fallback: u.kind === `art` ? u : l,
    });
  let p = r || getCartState().settings || {},
    m = (p.ui && p.ui.shell) || n || `classic`,
    h = (e && e.art && e.art.color) || t.color,
    g,
    _;
  if (!s && i) {
    let n = resolveCartLook(e, t.id, p);
    n
      ? ((g = n.color), (_ = n.finish))
      : m === `tinted`
        ? (g = C(h, 0.45, i.color))
        : m === `clear` && ((g = C(h, 0.5, `#9aa0ad`)), (_ = `translucent`));
  }
  let v = s ? 0 : resolveWearLevel(e, p),
    y = (!s && p.ui && p.ui.insert === `real` && INSERT_DIRECTIONS[a]) || `down`;
  return {
    modelKey: c,
    disc: s,
    label: u,
    color: g,
    finish: _,
    wear: v,
    insert: y,
    seed: e ? e.id : t.id,
    aspect: s ? 1 : i.w / i.h,
  };
}
function describeSystemCart(e, t, n, r = null) {
  let { spec: i, key: a, disc: o } = resolveMediaSpec(e),
    s = !!o,
    c = {
      kind: `gen`,
      color: e.color,
      round: s,
      title: e.short,
      umd: a === `umd`,
      band: `${e.maker} · ${e.year}`,
      sub: `${t} ${t === 1 ? `game` : `games`}`,
    },
    l = c,
    u = null,
    d = 0,
    f = r || getCartState().settings || {};
  if (n && n.length) {
    let t = [...n].sort((e, t) => (t.lastPlayed || 0) - (e.lastPlayed || 0)),
      r = t.find((e) => e.art && e.art.label);
    r &&
      (l = {
        kind: `art`,
        url: w(r, `label`),
        style: `scan`,
        umd: a === `umd`,
        fallback: c,
      });
    let i = r || (t[0] && t[0].lastPlayed ? t[0] : null);
    i && !s && ((u = resolveCartLook(i, e.id, f)), (d = resolveWearLevel(i, f)));
  }
  return (
    !u && !s && (u = resolveCartLook(null, e.id, f)),
    {
      modelKey: s ? e.media.shape : a,
      disc: s,
      aspect: s ? 1 : i.w / i.h,
      label: l,
      color: u ? u.color : void 0,
      finish: u ? u.finish : void 0,
      wear: d,
      seed: e.id,
    }
  );
}
export {
  CART_SPECS,
  DISC_SPECS,
  BOX_ART_CROPS,
  ENGRAVING_PATHS,
  resolveMediaSpec,
  buildCartGeometry,
  buildDiscGeometry,
  getCachedGeometry,
  createLabelTexture,
  CartStageRenderer,
  SHELL_FINISH_OPTIONS,
  SHELL_COLOR_OPTIONS,
  DEFAULT_SHELL_COLORS,
  findSpecialEditionLook,
  resolveCartLook,
  resolveWearLevel,
  hasWebGL,
  describeGameCart,
  describeSystemCart,
  setCartStateProvider,
};
