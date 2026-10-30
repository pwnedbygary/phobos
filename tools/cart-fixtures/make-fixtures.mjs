// Fixtures for the app-side cart look/wear ports (ui/carts/LookResolver.kt).
//
// Runs Cartridge's own pipeline copy (tools/carts/cartridge/carts/pipeline.js, the 4a verbatim copy) under Node
// and writes the expected values for a hand-written list of cases. Nothing is fetched; the Node import shims and
// module copies already used by the 4a generator are reused as-is.
//
//   node tools/cart-fixtures/make-fixtures.mjs
//
// Writes android/app/src/test/resources/carts/look-fixtures.json.
import { register } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

register(new URL('../carts/hdr-url-loader.mjs', import.meta.url));
const {
  resolveCartLook,
  resolveWearLevel,
  SHELL_FINISH_OPTIONS,
  SHELL_COLOR_OPTIONS,
  DEFAULT_SHELL_COLORS,
} = await import('../carts/cartridge/carts/pipeline.js');

const wear = (playtime, uiWear) => resolveWearLevel({ playtime }, { ui: { wear: uiWear } });

// One representative per special-edition rule, plus near-misses and precedence cases. Each case is the exact
// input the Kotlin port receives; `expected` is what Cartridge's own code returns.
const cases = [
  // The 29 filename rules (representatives, all matching).
  { file: 'Pokemon - Red Version (USA, Europe).gb', system: 'gb' },
  { file: 'Pokemon - Blue Version (USA, Europe).gb', system: 'gb' },
  { file: 'Pokemon - Yellow Version (USA, Europe).gbc', system: 'gbc' },
  { file: 'Donkey Kong Land (USA, Europe).gb', system: 'gb' },
  { file: 'Pokemon - Gold Version (USA, Europe).gbc', system: 'gbc' },
  { file: 'Pokemon - Silver Version (USA, Europe).gbc', system: 'gbc' },
  { file: 'Pokemon - Crystal Version (USA, Europe).gb', system: 'gb' },
  { file: "Kirby Tilt 'n' Tumble (USA).gb", system: 'gb' },
  { file: 'Pokemon Pinball (USA, Europe).gbc', system: 'gbc' },
  { file: 'Pokemon - Ruby Version (USA).gba', system: 'gba' },
  { file: 'Pokemon - Sapphire Version (USA).gba', system: 'gba' },
  { file: 'Pokemon - Emerald Version (USA).gba', system: 'gba' },
  { file: 'Pokemon - FireRed Version (USA).gba', system: 'gba' },
  { file: 'Pokemon - LeafGreen Version (USA).gba', system: 'gba' },
  { file: 'Boktai - The Sun Is in Your Hand (USA).gba', system: 'gba' },
  { file: 'Yoshi Topsy-Turvy (USA).gba', system: 'gba' },
  { file: 'Legend of Zelda, The (USA).nes', system: 'nes' },
  { file: 'Zelda II - The Adventure of Link (USA).nes', system: 'nes' },
  { file: 'Killer Instinct (USA).sfc', system: 'snes' },
  { file: 'Doom (USA).sfc', system: 'snes' },
  { file: 'Spider-Man and Venom - Maximum Carnage (USA).md', system: 'genesis' },
  { file: 'Legend of Zelda, The - Ocarina of Time (USA).z64', system: 'n64' },
  { file: "Legend of Zelda, The - Majora's Mask (USA).z64", system: 'n64' },
  { file: 'Pokemon Stadium 2 (USA).z64', system: 'n64' },
  { file: 'Donkey Kong 64 (USA).z64', system: 'n64' },
  { file: "Tony Hawk's Pro Skater (USA).z64", system: 'n64' },
  { file: "Army Men - Sarge's Heroes 2 (USA).z64", system: 'n64' },
  { file: 'WWF No Mercy (USA).z64', system: 'n64' },
  { file: 'Pokemon - HeartGold Version (USA).nds', system: 'nds' },
  // Near-misses: the `not` / `only` / rev exclusions must come out null.
  { file: 'Pokemon - Red Version (Japan).gb', system: 'gb' },
  { file: 'Legend of Zelda, The (Japan).nes', system: 'nes' },
  { file: 'Legend of Zelda, The - Ocarina of Time (USA) (Rev 1).z64', system: 'n64' },
  { file: 'Killer Instinct (Europe).sfc', system: 'snes' },
  { file: 'Super Mario Bros. (World).nes', system: 'nes' },
  { file: 'Metroid (USA).nes', system: 'nes' },
  // Precedence: per-game key first, special editions (unless off), then per-system; invalid looks ignored.
  { file: 'Super Mario Bros. (World).nes', system: 'nes', cartLooks: { 's:nes': { finish: 'glitter', color: '#112233' } } },
  { file: 'Super Mario Bros. (World).nes', system: 'nes', cartLooks: { 'g:42': { finish: 'clear', color: '#abcdef' } }, id: 42 },
  { file: 'Pokemon - Red Version (USA, Europe).gb', system: 'gb', specialCarts: false },
  { file: 'Pokemon - Red Version (USA, Europe).gb', system: 'gb', specialCarts: false, cartLooks: { 's:gb': { finish: 'metallic', color: '#445566' } } },
  { file: 'Super Mario Bros. (World).nes', system: 'nes', cartLooks: { 's:nes': { finish: '', color: '' } } },
  { file: 'Pokemon - Red Version (USA, Europe).gb', system: 'gb', cartLooks: { 's:gb': { finish: 'solid', color: '#010101' } } },
];

const looks = cases.map((c, i) => {
  const game = { id: c.id ?? 1, system: c.system, file: c.file, playtime: c.playtime ?? 0 };
  const settings = {
    cartLooks: c.cartLooks ?? {},
    ui: { specialCarts: c.specialCarts !== false, wear: c.uiWear === undefined ? 'off' : c.uiWear },
  };
  const look = resolveCartLook(game, c.system, settings);
  return {
    n: i,
    id: c.id ?? 1,
    file: c.file,
    system: c.system,
    playtime: c.playtime ?? 0,
    specialCarts: c.specialCarts !== false,
    cartLooks: c.cartLooks ?? {},
    expectedLook: look ? { finish: look.finish, color: look.color, special: !!look.special } : null,
  };
});

const wears = [
  [0, 'on'],
  [3599, 'on'],
  [3600, 'on'],
  [28799, 'on'],
  [28800, 'on'],
  [107999, 'on'],
  [108000, 'on'],
  [360000, 'on'],
  [0, 'off'],
  [360000, 'off'],
  [0, 'loved'],
  [360000, 'loved'],
].map(([playtime, uiWear]) => ({ playtime, wear: uiWear, expected: wear(playtime, uiWear) }));

const out = {
  source: 'Cartridge 0.4.9.3 src/carts/pipeline.js via tools/carts/cartridge (verbatim copy)',
  node: process.version,
  shellFinishes: SHELL_FINISH_OPTIONS.map((o) => ({ id: o.id, label: o.label })),
  shellColors: SHELL_COLOR_OPTIONS.map((o) => ({ name: o.name, hex: o.hex })),
  defaultShellColors: DEFAULT_SHELL_COLORS,
  looks,
  wears,
};

const target = join(dirname(fileURLToPath(import.meta.url)), '../../android/app/src/test/resources/carts');
mkdirSync(target, { recursive: true });
writeFileSync(join(target, 'look-fixtures.json'), JSON.stringify(out, null, 2) + '\n');
console.log(`wrote ${looks.length} look cases, ${wears.length} wear cases, ` +
  `${out.shellColors.length} colours, ${out.shellFinishes.length} finishes to ${target}/look-fixtures.json`);
