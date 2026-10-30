# Cartridge in Phobos

Cartridge is Isaiah's game frontend. Phobos's Library takes some of its code and rules, starting with the artwork
downloads. This file says what comes from where, on what terms, and from which copy of Cartridge.

## Permission and license

- Phobos's owner reports, on 26 October 2026: "I have Isaiah's permission to use everything, Plus features included".
  That is his report; no written grant from Isaiah is held here.
- Cartridge's application declares the MIT license (`"license": "MIT"`) and Isaiah as its author
  (`"author": "Isaiah"`) in its `package.json`, but ships no license notice of its own. The notice in Phobos's
  `LICENSE` ("Cartridge (portions …)") gives the MIT license's text with a copyright line reconstructed from those two
  fields: `Copyright (c) Isaiah (Cartridge)`. It is not a line Cartridge shipped.
- Code under the MIT license may be included in a work under the GNU GPL version 3, as Phobos is. The ported files keep
  the MIT notice; Phobos's changes to them are under Phobos's license.

## Which copy

Cartridge 0.4.9.3 (application version 0.4.9), as its author distributed it. Its JavaScript was read back out of the
shipped application into a source tree (a local reconstruction, not published); the files below are that tree's
`electron/` folder, byte for byte the Windows build's `resources/app.asar` members.

| Artifact | SHA-256 |
| --- | --- |
| `Cartridge-0.4.9.3-Windows-test.zip` | `f4e2609d30dd4f2a66b53ef984f898b22496316b3d586133d00efa170640c6e6` |
| its `resources/app.asar` | `3db8adb0bf7f0f7eb2dc85c587a9c3aee2be01b7654fac06723fe20ab1e727d8` |
| `electron/titles.js` | `5627b2c0c35a7b3e1fcdbdc135688e5d3e0f8bc30d96f9b23a435a31cd546ada` |
| `electron/scraper.js` | `e4e4de3bea17b7b7d92b9421bdef4a5dd19f94d6c07af594ef7e4ce90c1da80e` |
| `electron/labelcrop.js` | `bb738878487954fdcbf49cfe08b6bdde35ff2bce676922572ae92ac5f6be05f7` |
| `electron/systems.js` | `f58bed3a50f2fc633fec18294c703ea95f18fa77089bcf10840f7d93338024d6` |

## Which Phobos file comes from which

All paths under `android/app/src/main/java/com/phobos/emulator/` unless named in full. Each ported file's first lines
name its origin.

| Phobos | Cartridge | What |
| --- | --- | --- |
| `art/ArtMatch.kt` | `electron/titles.js` (`matchKey`, `parseTags`); `electron/scraper.js` (`sanitize`, `bigrams`, `dice`, `pickBest`, `match`, the listing parser in `loadIndex`) | Ported. Changed: a loose (fuzzy) match must have the game's own numbers. |
| `art/ArtDownloader.kt` | `electron/scraper.js` (`run`, `index`, `loadIndex`, `download`, `isGone`) | The order of the look-ups and what counts as gone or failed. The queue, the pause while a game runs and the fetching are Phobos's. |
| `art/ArtStore.kt` | `electron/scraper.js` (`INDEX_TTL`, `RETRY_MISS_AFTER`, the hour after a failure) | The times only. The file, the cap and the eviction are Phobos's. |
| `art/ArtSystems.kt` | `electron/systems.js` (`thumbs`, `thumbsAlt`) | Some folder names; every one was checked against the server's own listing. |
| `ui/LibraryRows.kt` | `src/main.js` (Continue Playing, Favorites, the game menu) | The rules only, read from the rebuilt source; no code. |
| `tools/art-fixtures/cartridge/` | `electron/titles.js`, `scraper.js`, `labelcrop.js` | Copies, unchanged, to make the test fixtures (`tools/art-fixtures/README.md`). |

## Not taken

- Label scans (LaunchBox Games Database) and their index, `labels.json.gz`: they come with the 3D carts, where they're
  shown. Neither the LaunchBox Games Database nor libretro-thumbnails publishes terms for its pictures; downloading
  them while Phobos runs is the owner's choice. Nothing from either ships with Phobos; the label index, whose
  redistribution the owner waived (27 October 2026), comes with the carts.
- Cartridge's arcade name list, fetched from GitHub: Phobos's arcade games go by their titles in its own
  `assets/Database/Arcade.bml`.
