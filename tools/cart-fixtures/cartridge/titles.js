'use strict';
// Turns No-Intro / Redump style filenames into display titles.
//   "Legend of Zelda, The - A Link to the Past (USA)" → "The Legend of Zelda: A Link to the Past"

const REGIONS = [
  ['USA', 'US'], ['World', 'W'], ['Europe', 'EU'], ['Japan', 'JP'], ['Australia', 'AU'],
  ['Korea', 'KR'], ['Germany', 'DE'], ['France', 'FR'], ['Spain', 'ES'], ['Italy', 'IT'],
  ['Brazil', 'BR'], ['China', 'CN'], ['Asia', 'AS'], ['Canada', 'CA'], ['Netherlands', 'NL'],
  ['Sweden', 'SE'],
];

function parseTags(tags) {
  let region = null;
  let disc = null;
  const flags = [];
  for (const raw of tags) {
    const t = raw.trim();
    if (!region) {
      for (const [name, code] of REGIONS) {
        if (t.split(/,\s*/).includes(name)) { region = code; break; }
      }
    }
    const d = t.match(/^Dis[ck]\s*(\d+)/i);
    if (d) disc = Number(d[1]);
    if (/^(Beta|Proto|Demo|Sample|Hack|Unl|Pirate|Kiosk)/i.test(t)) flags.push(t.split(/\s/)[0]);
    if (/^Rev\s/i.test(t)) flags.push(t);
  }
  return { region, disc, flags };
}

function cleanTitle(base) {
  const tags = [];
  let t = String(base)
    .replace(/_/g, ' ')
    .replace(/\s*[([]([^)\]]*)[)\]]/g, (_, inner) => { tags.push(inner); return ''; })
    .replace(/\s+/g, ' ')
    // Dump-tool leftovers: "Game Decrypted", "Game [!]", "Game (Trimmed)"
    .replace(/\s+(decrypted|encrypted|trimmed|dec|enc)$/i, '')
    // Version numbers stuck on the name: "Game v1.0.0"
    .replace(/\s+v\d+(\.\d+)+$/i, '')
    .trim();

  // Leading article moved to the end: "Legend of Zelda, The - Subtitle"
  t = t.replace(/^(.+?), (The|A|An)(\s+-\s+.*)?$/i, (_, main, art, rest) => `${art} ${main}${rest || ''}`);
  // First " - " reads better as a colon
  t = t.replace(/\s+-\s+/, ': ');
  if (!t) t = String(base);

  const info = parseTags(tags);
  return { title: t, tags, ...info };
}

function sortKey(title) {
  return String(title)
    .toLowerCase()
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/^(the|a|an)\s+/, '')
    .replace(/[^a-z0-9 ]/g, '')
    .trim();
}

function letterOf(key) {
  const c = (key || '').charAt(0).toUpperCase();
  return c >= 'A' && c <= 'Z' ? c : '#';
}

// Normalised key used for fuzzy artwork matching.
function matchKey(name) {
  return String(name)
    .replace(/\s+v\d+(\.\d+)+$/i, '')
    .replace(/\.[a-z0-9]{1,4}$/i, '')
    .replace(/\s*[([][^)\]]*[)\]]/g, '')
    .replace(/\s+v\d+(\.\d+)+\s*$/i, '')
    .replace(/\s+(decrypted|encrypted|trimmed)\s*$/i, '')
    .replace(/^(.+?), (the|a|an)\b/i, '$2 $1')
    .toLowerCase()
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/&/g, 'and')
    .replace(/\b(version|edition)\b/g, '')
    .replace(/[^a-z0-9]/g, '');
}

module.exports = { cleanTitle, sortKey, letterOf, matchKey, parseTags };
