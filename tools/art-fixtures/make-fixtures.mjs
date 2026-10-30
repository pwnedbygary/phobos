// Writes the expectations Phobos's artwork matching is tested against (ArtMatchTest), from Cartridge's own code:
// cartridge/titles.js, labelcrop.js and scraper.js are Cartridge 0.4.9.3's, copied unchanged (README.md gives their
// SHA-256). The listing and the names looked up are hand-written here; nothing is fetched.
//
//   node tools/art-fixtures/make-fixtures.mjs android/app/src/test/resources/art
import { createRequire } from 'node:module';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { Scraper, sanitize } = require('./cartridge/scraper.js');
const { matchKey } = require('./cartridge/titles.js');

const out = process.argv[2];
if (!out) {
  console.error('usage: node make-fixtures.mjs <output folder>');
  process.exit(2);
}

// A server folder's pictures, as its listing names them (already sanitized, as the server's files are).
const names = [
  'Legend of Zelda, The - A Link to the Past (USA)',
  'Legend of Zelda, The - A Link to the Past (Europe)',
  'Super Mario World (USA)',
  'Super Mario World (Japan)',
  'Metal Slug 3 (NGH-2560)',
  'Garou - Mark of the Wolves (NGH-2530)',
  "King of Fighters '94, The (NGM-055)(NGH-055)",
  'Chrono Trigger (USA)',
  'Pokemon - Red Version (USA, Europe)',
  'Pokemon - Blue Version (USA, Europe)',
  'Sonic The Hedgehog (USA, Europe)',
  'Sonic The Hedgehog 2 (World)',
  'Final Fantasy VII (USA) (Disc 1)',
  'Tetris (World) (Rev 1)',
  'Tetris (Japan) (En)',
  'Mega Man X (USA)',
  'Mega Man X (USA) (Beta)',
  'Ys I _ II (Japan)',
  'Pokémon Stadium (USA)',
  'Contra (USA)',
  'Contra Force (USA)',
];

// The page the server would send: an Apache index, with the links Cartridge's parser has to pass over.
const rows = names.map((n) => `<tr><td><a href="${encodeURIComponent(n)}.png">${n}.png</a></td><td>2025-04-09</td></tr>`);
const html = [
  '<html><head><title>Index of /Test/Named_Boxarts</title></head><body>',
  '<table><tr><th><a href="?C=N;O=D">Name</a></th></tr>',
  '<tr><td><a href="/Test/">Parent Directory</a></td></tr>',
  ...rows,
  '<tr><td><a href="notes.txt">notes.txt</a></td></tr>',
  '<tr><td><a href="Broken%E0%A4%A.png">a bad escape</a></td></tr>',
  '</table></body></html>',
  '',
].join('\n');

// What each lookup's name is, and the region order it's picked in. guard: Phobos looks only for a match with the
// same numbers as the game's own, so it finds nothing where Cartridge's loose step takes another game.
const US = ['US', 'W', 'JP', 'EU'];
const JP = ['JP', 'W', 'US', 'EU'];
const EU = ['EU', 'W', 'US', 'JP'];
const lookups = [
  { base: 'Legend of Zelda, The - A Link to the Past (USA)', order: US },
  { base: 'Legend of Zelda, The - A Link to the Past (Germany)', order: EU },
  { base: 'Super Mario World', order: US },
  { base: 'Super Mario World', order: JP },
  { base: 'Metal Slug', order: US, guard: true },
  { base: "The King of Fighters '98", order: US, guard: true },
  { base: 'Garou - Mark of the Wolves', order: US },
  { base: 'Pokemon - Red Version (USA)', order: US },
  { base: 'Sonic the Hedgehog 2 (USA)', order: US },
  { base: 'Sonic the Hedgehog 3 (USA)', order: US, guard: true },
  { base: 'Tetris', order: US },
  { base: 'Tetris', order: JP },
  { base: 'Mega Man X', order: US },
  { base: 'Ys I & II (Japan)', order: JP },
  { base: 'Pokémon Stadium (USA)', order: US },
  { base: 'Pokemon Stadium (USA)', order: US },
  { base: 'Contra', order: US },
  { base: 'Final Fantasy VII (USA)', order: US },
  { base: 'Chrono Trigger - Refined Text Edition [v2.0]', order: US },
  { base: 'Zzz Not A Game', order: US },
];

const keyNames = [
  'Legend of Zelda, The - A Link to the Past (USA)',
  'Super Mario World (USA) v1.1',
  'Pokémon Stadium (USA)',
  'Ys I & II (Japan)',
  'Some Game (Europe) [!].zip',
  'Game Decrypted',
  'Street Fighter II Special Champion Edition (USA)',
  'Tetris',
  '',
];
const sanitizeNames = ['Ys I & II', 'What? Who* Where: Here/There <a|b> "q" `t`', 'Plain'];

const cacheDir = mkdtempSync(join(here, '.cache-'));
try {
  const scraper = new Scraper({
    mediaDir: cacheDir,
    cacheDir,
    fetch: async () => ({ ok: true, status: 200, text: async () => html }),
  });
  const index = await scraper.index('Test', 'box');
  const matches = lookups.map(({ base, order, guard }) => ({
    base,
    order,
    cartridge: scraper.match(index, base, order),
    ...(guard ? { guard: true } : {}),
  }));
  const fixture = {
    made: 'node tools/art-fixtures/make-fixtures.mjs android/app/src/test/resources/art',
    node: process.version,
    listing: index.names,
    keys: keyNames.map((name) => ({ name, key: matchKey(name) })),
    sanitized: sanitizeNames.map((name) => ({ name, sanitized: sanitize(name) })),
    matches,
  };
  for (const m of matches) if (m.guard && !m.cartridge) throw new Error(`guard case found nothing: ${m.base}`);
  writeFileSync(join(out, 'listing.html'), html);
  writeFileSync(join(out, 'cartridge-matches.json'), `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`wrote ${matches.length} lookups, ${index.names.length} listed names`);
} finally {
  rmSync(cacheDir, { recursive: true, force: true });
}
