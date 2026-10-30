# Artwork matching fixtures

`make-fixtures.mjs` runs Cartridge's own artwork matching over a hand-written folder listing and hand-written game
names, and writes what it found to `android/app/src/test/resources/art/`: `listing.html`, the page the server would
send, and `cartridge-matches.json`, Cartridge's results. `ArtMatchTest` checks Phobos's port
(`android/app/src/main/java/com/phobos/emulator/art/ArtMatch.kt`) against them. Nothing is fetched.

Made with Node.js v26.11.1:

```sh
node tools/art-fixtures/make-fixtures.mjs android/app/src/test/resources/art
```

`cartridge/` holds three files of Cartridge 0.4.9.3 (MIT; see `docs/third-party/cartridge.md`), copied unchanged from
the source rebuilt from its shipped application (`electron/`). `scraper.js` needs `labelcrop.js`, which the fixtures
don't use.

| File | SHA-256 |
| --- | --- |
| `cartridge/titles.js` | `5627b2c0c35a7b3e1fcdbdc135688e5d3e0f8bc30d96f9b23a435a31cd546ada` |
| `cartridge/scraper.js` | `e4e4de3bea17b7b7d92b9421bdef4a5dd19f94d6c07af594ef7e4ce90c1da80e` |
| `cartridge/labelcrop.js` | `bb738878487954fdcbf49cfe08b6bdde35ff2bce676922572ae92ac5f6be05f7` |

Lookups marked `guard` are where Phobos deliberately differs: a loose (fuzzy) match must have the same numbers as the
game's own name, so "Metal Slug" doesn't take "Metal Slug 3"'s picture. There Phobos finds nothing.
