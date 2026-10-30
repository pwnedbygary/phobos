# cart-fixtures

Fixtures for the app-side cart ports (`ui/carts/LookResolver.kt`, and later the label ports), generated from
Cartridge's own code rather than by hand.

- The look/wear cases run `src/carts/pipeline.js` through the **verbatim copy already in `tools/carts/cartridge/`**
  (4a), with the same Node import shims the 4a generator uses. No second copy, nothing fetched.
- Node is the pinned 26.11.1 (`tools/carts/.nvmrc`). Run from the repo root:

```sh
(cd tools/carts && npm ci --offline)      # three/pngjs/xmldom from the local npm cache, as in 4a
node tools/cart-fixtures/make-fixtures.mjs
```

The script writes `android/app/src/test/resources/carts/look-fixtures.json` (41 look cases, 12 wear cases, the
20 shell colours and 6 finishes). `LookResolverTest` compares the Kotlin port against it.

The cases are hand-written representatives: one filename per special-edition rule, near-misses for the
`not`/`only`/rev exclusions, precedence cases (per-game, special editions off, per-system, invalid looks), and the
wear thresholds on both sides of every boundary.
