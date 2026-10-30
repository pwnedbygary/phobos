# Provenance of tools/carts (PR 4a)

Generated carts and discs are derived from the files below. Hashes are sha256; the manifest
(`out/MANIFEST.json`) records the same inputs plus every output.

## Copied verbatim from Cartridge (MIT)

Source: the Cartridge reconstruction repository `/home/garyb/LLM-Projects/Cartridge` (the app's own frontend
sources, MIT licensed). The copies keep Cartridge's import layout so its relative imports resolve unmodified.

| File | sha256 | Origin |
| --- | --- | --- |
| `cartridge/carts/pipeline.js` | `a5cbbe9d288972781db3ef26ecf471221b76a16005eb7541155b699e66011967` | `src/carts/pipeline.js` |
| `cartridge/shared/logo.js` | `83ed228846aac6875002b074ff26515342e9c52e793bcfa235fef35cf795eaaf` | `src/shared/logo.js` |
| `cartridge/assets/studio.hdr` | `f6a989f89432eb4eee3191364a9c1ceed195c4ec3544173a3c04fd96cb91d0ba` | `src/assets/studio.hdr` |

`logo.js` is a JSX component imported by `pipeline.js`; the generator never renders it (the owner's engraving
choice drops the wordmark). The `hdr-url-loader.mjs` shim supplies a stub for `react/jsx-runtime` and for
Vite's `?url` asset specifier so the verbatim copy loads under plain Node, and `dom-shim.mjs` gives the parsed
document the two tag-name `querySelectorAll` selectors three's SVGLoader uses (xmldom 0.9.8 has none; without
them the engraving geometry vanished silently - the 4a review's blocking finding).

Cartridge is MIT licensed. Any file derived from these copies keeps the notice; the app-facing notice update
rides with PR 4b (`docs/third-party/cartridge.md`).

## Filament 1.77.3 Linux archive (Apache-2.0)

`https://github.com/google/filament/releases/download/v1.77.3/filament-v1.77.3-linux.tgz`
sha256 `6d3391197dcbdec318dd8a7f4e8d6fd0766794231d53c756a1565fd92bba31b2`.

Its `bin/cmgen` and `bin/matc` produce `out/ibl/*` and `out/materials/disc.filamat`. Tools only; the app's
Filament Android dependencies arrive in PR 4b. The archive's `LICENSE` is the Apache-2.0 text
(sha256 `02d70b593dd8e9355dd8e1ca8d13c303bee5bcaf3065b66ff6e4f534e1e6a2c1`).

## npm dependencies (build tool only)

| Package | Version | sha256 (tarball) | Licence |
| --- | --- | --- | --- |
| `three` | 0.186.0 | `61eeff9d7616005c9a481c796f52287d81fbbbc0d55eaca5565322924252c1aa` | MIT |
| `pngjs` | 7.0.0 | `c4b71873d48a692dd5eeb1dcb62185c7aafe40f2180c7cc7f30ec71e89a080be` | MIT |
| `@xmldom/xmldom` | 0.9.8 | `eb85b871f14f569d1fd93b951cddfb22c9e9de1e8010838dbf256bafb67fc1fc` | MIT |

Lockfile integrities and the cache provenance are in `npm-evidence.txt` (this folder). 0.9.8 carries an
npm advisory; see README.md (Limits).

## studio.hdr

Origin and licence are **unknown**. It ships by the owner's explicit choice (scope note 1, choice 3),
stated plainly here and in the PR body. The generated IBL files inherit that uncertainty.

## Generated outputs

`out/` holds the baked GLBs, normal maps, IBL KTX, `disc.filamat` and `MANIFEST.json`. They are derived from
the inputs above; the manifest pins them by hash. The Cartridge reconstruction repository is itself a
reconstruction of the Cartridge app and its assets.
