# The PSP core's test programs, built

Real PSP programs the host tests run in the PSP core, kept here already built so that every checkout, and CI, runs
the tests that need them. They're built from source in this repository by `tools/psp-test-programs/build.sh`, whose
README says what each one does and which tests use it. `tests/psp/run-tests.sh` and `tests/psp/ares/run-tests.sh`
use this folder unless `PSP_TEST_PROGRAMS` names another.

Until 2026-10-06 the repository held no binaries and the programs were built for each run. The owner then asked
for them to be kept here (and for `tests/psp/measurements`), so another machine needs no PSP toolchain to run every
test.

## What they are, and where they come from

| File | Built from |
| --- | --- |
| `hello.elf`, `hello.prx`, `EBOOT.PBP` | `tools/psp-test-programs/hello/` (a static executable, a relocatable module, and an EBOOT.PBP holding the static one) |
| `system.elf` | `tools/psp-test-programs/system/` |
| `disc.elf` | `tools/psp-test-programs/disc/` |
| `gu.elf` | `tools/psp-test-programs/gu/` |
| `copy.elf`, `blit.elf`, `clut.elf`, `blend.elf`, `doublelist.elf`, `cube.elf`, `celshading.elf`, `envmap.elf` | pspsdk's own GU samples, from the toolchain |
| `pspmeasure.elf` | `tools/psp-measure/` (the measurement program the owner ran on their PSP) |

They were built on 2026-10-04 (`disc.elf` on 2026-10-05) with pspdev's toolchain
(<https://github.com/pspdev/pspdev>), and are the very files the tests have been run with since. They hold no
Sony code, no game and no firmware. Their parts come from:
- Phobos's own sources (the programs in `tools/psp-test-programs` and `tools/psp-measure`);
- pspsdk, the free PSP SDK, including its GU samples (pspsdk's BSD-style license; see the pspsdk sources);
- newlib, pspdev's C library (BSD-style licenses; see the newlib sources).

## Rebuilding them

When their sources change, rebuild them and replace these files in the same commit. With pspdev's Docker image:

```
docker run --rm -v "$PWD":/src -w /src -e PSPDEV=/usr/local/pspdev ghcr.io/pspdev/pspdev:latest \
  tools/psp-test-programs/build.sh /src/tests/psp/programs
```

Or, with pspdev installed (`psp-config` on the `PATH`), run `tools/psp-test-programs/build.sh tests/psp/programs`.

## SHA-256

```
b05e4da16289f6a82006ab3bea370127fdb3deb62deb3a27fe3236f63842a3ba  EBOOT.PBP
9d3ee22eaae9c01a443de3a8ae52d4bd0a4ce30757f152d513e924e8c9cd6037  blend.elf
023c6d1eb9f9d9181236620cc0f997afc0ea8343e2aae0f071f19fe32af50dc0  blit.elf
d25001dfe6a0d8a5806097df1867b2b3addcb085f9b92ce9f988cb498985f889  celshading.elf
7c7337660bc4fcfa4d91fdbd181cc034b4703d3124312242f6afa99cffc26ee5  clut.elf
45e471aea6930d8eeccbe22fa1021dc7330f6e709f9a298459a282450a78367e  copy.elf
1569cbd324b07d35373a2bbb117ce2b079588e683bd3eff0dbc3e2fb1d5c375f  cube.elf
d4bfc95a9649c219f685f600f5de4b7b148f6863f291e5f2e3eee7162c4cfbe9  disc.elf
7bc1f00f331b04808bf3d479b803e75c41296e9407805f0de76386ef4c814752  doublelist.elf
f0e8d94d22a410fcf7db6e8f8507a1401276586bc2a24f03e7de10c21fec842b  envmap.elf
039c63dc8a7fa7eb2adeb58221a809b8b7d6e80a23164ce8149197a2e26b0dd5  gu.elf
ce08aad78b4a886a08982413d1ecfd79639b3cda0b7de7fc26a21b81be663feb  hello.elf
287a42e38c907710723379a408e12640142549230df3babd6e62ac369e5e3910  hello.prx
28e45c6e1169d785ef21bfdf6e5eea5cbc050205bf529d9895f73e81f5cecf51  pspmeasure.elf
c7a041b4b9182d1c3e01913b13e1fd8b2495307ece8e53412d9f11225c39cbb1  system.elf
```
