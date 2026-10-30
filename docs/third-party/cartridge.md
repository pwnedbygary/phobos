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
| `tools/carts/cartridge/` | `src/carts/pipeline.js`, `src/shared/logo.js`, `src/assets/studio.hdr` | Copies, unchanged, for the build-time carts generator (`tools/carts/README.md`); the tools are not shipped, but their outputs are. |
| `tools/cart-fixtures/cartridge/` | `electron/titles.js`, `scraper.js`, `labelcrop.js` | Copies, unchanged, for the label ports' fixtures (`tools/cart-fixtures/README.md`); not shipped in the app. |
| `android/app/src/main/assets/carts/` | `src/carts/pipeline.js`, `src/shared/logo.js`, `src/assets/studio.hdr`, via `tools/carts` | The generator's outputs (GLBs, IBL KTX, `disc.filamat`, the normal maps): generated, not copied, and pinned by `android/app/src/main/assets/carts/MANIFEST.json`. |
| `android/app/src/main/assets/carts/labels.json.gz` | Cartridge 0.4.9.3's `data/labels.json.gz` | A copy, not a generated output: the LaunchBox index, bundled by the owner's 30 October decision. Its paragraph is below. |

## Label scans and their index

**Label scans (LaunchBox Games Database) and their index, `labels.json.gz`.** The index ships with Phobos in
`android/app/src/main/assets/carts/labels.json.gz` (sha256
`f7f1e8bba203e96b30ef1ecb4d3ee7ff6fe5555d6be8b8b371f354d5585cb089`); the scans themselves are downloaded at
runtime from `https://images.launchbox-app.com/` only, through the art downloader's queue, pause rule and 256 MiB
cap, when the user enables Carts and real labels. The right to redistribute the index has not been established: the
reconstruction's PROVENANCE.md:14-18 records "The right to redistribute that LaunchBox-derived index has not been
established", and THIRD_PARTY_NOTICES.md:25 records "Not pursued (owner, 2026-10-27)". No licence is recorded or
implied. The owner chose to bundle it on 2026-10-30 for the 3D carts, accepting the risk; that is a product
decision, not a licence. Neither the LaunchBox Games Database nor libretro-thumbnails publishes terms for its
pictures; downloading them while Phobos runs is the owner's choice.

## Not taken

- Cartridge's arcade name list, fetched from GitHub: Phobos's arcade games go by their titles in its own
  `assets/Database/Arcade.bml`.

## Filament libraries (build dependency)

The 3D carts use Filament 1.77.3 from Maven Central (Android AARs, Apache-2.0; the LICENSE carries the license
text). The exact artifacts, with their full SHA-256:

- `filament-android-1.77.3.aar` sha256 587a73db65e7c1289af799b1a7e0d7debdbeb9bf8d4253adb203199811096490
- `gltfio-android-1.77.3.aar` sha256 a258b4fdc52ded6c5313f1da6aa365f4da36c9c59eac789ef1399f8193099f33
- `filament-utils-android-1.77.3.aar` sha256 53884f6abbe56ee05ce8d3e43c732d7fa921446debf838be15d0e72d02926bbc

They are the only new dependency; the prebuilt native libraries are 16 KB-page aligned (verified with ELF LOAD
`p_align` 0x4000 on every arm64 `.so`).
