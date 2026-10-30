import { jsx, jsxs } from "react/jsx-runtime";
var r = typeof window < `u` && !window.cartridge && !new URLSearchParams(location.search).has(`android`),
  i = (e, t) => {
    let n = typeof window < `u` && window.cartridge;
    return n && n.fileUrl ? n.fileUrl(e, t) : void 0;
  },
  a = {
    nes: 0.73,
    snes: 1.38,
    n64: 1.38,
    gb: 0.98,
    gbc: 0.98,
    gba: 0.98,
    nds: 0.9,
    n3ds: 0.88,
    virtualboy: 0.73,
    pokemini: 0.9,
    gc: 0.71,
    wii: 0.71,
    wiiu: 0.71,
    switch: 0.62,
    mastersystem: 0.71,
    genesis: 0.71,
    gamegear: 0.71,
    segacd: 0.88,
    sega32x: 0.71,
    saturn: 0.8,
    dreamcast: 1,
    psx: 1,
    ps2: 0.7,
    psp: 0.58,
    ps3: 0.86,
    pcengine: 0.86,
    pcenginecd: 1,
    ngp: 0.8,
    ngpc: 0.8,
    wonderswan: 0.8,
    wonderswancolor: 0.8,
    atari2600: 0.73,
    atari7800: 0.73,
    lynx: 0.73,
    neogeo: 0.71,
    arcade: 0.75,
    xbox: 0.7,
  },
  o = (e) => a[e] || 0.75,
  s = (e) => e.split(`/`).map(encodeURIComponent).join(`/`);
function c(e) {
  let [t, n] = String(e).split(`?v=`);
  return n ? `${s(t)}?v=${n}` : s(t);
}
function l(e) {
  if (!e) return null;
  let t = i(`media`, e);
  return t === void 0 ? (r ? e : `cart://media/${c(e)}`) : t;
}
function u(e, t = `box`) {
  if (!e) return null;
  if (t === `box` && e.localArt) {
    let t = i(`local`, e.localArt);
    return t === void 0 ? (r ? e.localArt : `cart://local${s(e.localArt)}`) : t;
  }
  return l(e.art && e.art[t]);
}
function d(e, t) {
  return u(e, `box`) || (t && t.hasArt === !1 ? u(e, `label`) : null);
}
function f(e) {
  let t = e && e.lastState && e.lastState.thumb;
  if (!t) return null;
  let n = i(`state`, t);
  return n === void 0 ? (r ? t : `cart://state/${c(t)}`) : n;
}
function p(e) {
  return f(e) || u(e, `snap`) || u(e, `title`) || u(e, `box`);
}
function m(e) {
  if (!e) return `0m`;
  let t = Math.floor(e / 3600),
    n = Math.floor((e % 3600) / 60);
  return t === 0 ? (n === 0 ? `${Math.max(1, Math.round(e))}s` : `${n}m`) : `${t}h ${n.toString().padStart(2, `0`)}m`;
}
function h(e) {
  if (!e) return `Never played`;
  let t = (Date.now() - e) / 1e3;
  if (t < 60) return `Just now`;
  if (t < 3600) return `${Math.floor(t / 60)}m ago`;
  if (t < 86400) return `${Math.floor(t / 3600)}h ago`;
  let n = Math.floor(t / 86400);
  return n === 1
    ? `Yesterday`
    : n < 7
      ? `${n} days ago`
      : new Date(e).toLocaleDateString(void 0, {
          month: `short`,
          day: `numeric`,
        });
}
var g = {
  US: `USA`,
  W: `World`,
  EU: `Europe`,
  JP: `Japan`,
  AU: `Australia`,
  KR: `Korea`,
  DE: `Germany`,
  FR: `France`,
  ES: `Spain`,
  IT: `Italy`,
  BR: `Brazil`,
  CN: `China`,
};
function _(e, t, n) {
  return Math.max(t, Math.min(n, e));
}
function v(e) {
  let t = (e || `#888888`).replace(`#`, ``),
    n = parseInt(
      t.length === 3
        ? t
            .split(``)
            .map((e) => e + e)
            .join(``)
        : t,
      16,
    ),
    r = (n >> 16) & 255,
    i = (n >> 8) & 255,
    a = n & 255;
  return (0.299 * r + 0.587 * i + 0.114 * a) / 255 > 0.62 ? `#15151b` : `#ffffff`;
}
function y(e, t = 0.5, n = `#000000`) {
  let r = (e) => parseInt(e.replace(`#`, ``), 16),
    i = r(e || `#888888`),
    a = r(n),
    o = (e, t) => (e >> t) & 255;
  return `#${[16, 8, 0]
    .map((e) => Math.round(o(i, e) * (1 - t) + o(a, e) * t))
    .map((e) => e.toString(16).padStart(2, `0`))
    .join(``)}`;
}
function b(e, t, n) {
  if (!e.length) return t;
  let r = e[t] ? e[t].letter : null;
  if (n > 0) {
    for (let n = t + 1; n < e.length; n++) if (e[n].letter !== r) return n;
    return t;
  }
  let i = t;
  for (; i > 0 && e[i - 1].letter === r;) i--;
  if (i !== t) return i;
  if (i === 0) return 0;
  let a = e[i - 1].letter;
  for (--i; i > 0 && e[i - 1].letter === a;) i--;
  return i;
}
function x(...e) {
  return e.filter(Boolean).join(` `);
}
function C({ className: e = `wordmark__logo`, title: t }) {
  return jsxs(`svg`, {
    className: e,
    viewBox: `0 0 20 24`,
    role: t ? `img` : void 0,
    "aria-hidden": !t || void 0,
    "aria-label": t,
    children: [
      jsx(`path`, {
        d: `M2.6 0.5H15.2L19.5 4.8V21.4Q19.5 23.5 17.4 23.5H2.6Q0.5 23.5 0.5 21.4V2.6Q0.5 0.5 2.6 0.5Z`,
        fill: `var(--logo-shell, var(--accent, #7b6cf0))`,
      }),
      jsx(`path`, {
        d: `M2.6 0.5H15.2L19.5 4.8V21.4Q19.5 23.5 17.4 23.5H2.6Q0.5 23.5 0.5 21.4V2.6Q0.5 0.5 2.6 0.5Z`,
        fill: `none`,
        stroke: `rgba(0,0,0,0.35)`,
        strokeWidth: `1`,
      }),
      jsxs(`g`, {
        fill: `rgba(0,0,0,0.28)`,
        children: [
          jsx(`rect`, {
            x: `4`,
            y: `2.2`,
            width: `9.5`,
            height: `1`,
            rx: `0.5`,
          }),
          jsx(`rect`, {
            x: `4`,
            y: `4.2`,
            width: `9.5`,
            height: `1`,
            rx: `0.5`,
          }),
        ],
      }),
      jsx(`rect`, {
        x: `3`,
        y: `7`,
        width: `14`,
        height: `14.5`,
        rx: `1.4`,
        fill: `var(--logo-lapel, #17161c)`,
      }),
      jsx(`path`, {
        d: `M6.6 7H13.4L10 18.6Z`,
        fill: `var(--logo-shirt, #f6f5f8)`,
      }),
      jsx(`path`, {
        d: `M6.6 7L10 18.6L5 9.6Z`,
        fill: `rgba(255,255,255,0.13)`,
      }),
      jsx(`path`, {
        d: `M13.4 7L10 18.6L15 9.6Z`,
        fill: `rgba(255,255,255,0.13)`,
      }),
      jsx(`path`, {
        d: `M7.1 7.9L10 9.2L7.1 10.5Z M12.9 7.9L10 9.2L12.9 10.5Z`,
        fill: `var(--logo-tie, #17161c)`,
      }),
      jsx(`circle`, {
        cx: `10`,
        cy: `9.2`,
        r: `0.85`,
        fill: `var(--logo-tie, #17161c)`,
      }),
      jsx(`circle`, {
        cx: `10`,
        cy: `12.3`,
        r: `0.55`,
        fill: `var(--logo-tie, #17161c)`,
      }),
      jsx(`circle`, {
        cx: `10`,
        cy: `14.6`,
        r: `0.5`,
        fill: `var(--logo-tie, #17161c)`,
      }),
    ],
  });
}
export {
  _ as a,
  h as c,
  v as d,
  b as f,
  o as i,
  m as l,
  f as m,
  g as n,
  d as o,
  y as p,
  u as r,
  x as s,
  C as t,
  p as u,
};
