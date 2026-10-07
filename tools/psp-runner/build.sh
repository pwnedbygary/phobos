#!/usr/bin/env bash
#Builds tools/psp-runner, the headless PSP game runner: the PSP core as an ares system (ares/psp/psp.cpp, the
#whole core as Phobos builds it, with ares's node tree) with a command-line front end (runner.cpp) that boots a
#game, runs it a frame at a time, and reports what happens. Its objects are kept between builds in a folder
#outside the repository (a fresh TMPDIR rebuilds everything).
#usage: tools/psp-runner/build.sh   (the binary ends up in the build folder, named psp-runner)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-psp-runner
mkdir -p "$OUT/obj"

CC=${CC:-cc}
CXX=${CXX:-c++}
SYSROOT=()
#zlib packs the compressed disc images (ares/psp/kernel/disc.cpp) and the runner's PNGs
LIBRARIES=(-lpthread -ldl -lz)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation -lz)
fi
#One build mode for every file: nall's headers pick debug when none is given, and sljit's own C file, without
#them, would then disagree with the rest about SLJIT_DEBUG (sljitConfigPre.h), and so about its compiler's
#layout. ARES_ENABLE_CHD: CHD disc images are read with libchdr, as Phobos's builds read them.
DEFINES=(-DBUILD_DEBUG -DCORE_PSP -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1 -DARES_ENABLE_CHD)
#as system headers: ares's aren't written for -Wall, and the runner itself is built with it
CHDR=$ROOT/thirdparty/libchdr
INCLUDES=(-isystem "$ROOT" -isystem "$ROOT/nall" -isystem "$ROOT/nall/nall" -isystem "$ROOT/libco"
  -isystem "$ROOT/ares" -isystem "$ROOT/thirdparty" -isystem "$CHDR/include")
CXXFLAGS=(-std=c++20 -O1 -g "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}")

compile() {  #source object [always]
  local object=$OUT/obj/$2.o
  if [[ ${3:-} == always || ! -f $object || $1 -nt $object ]]; then
    echo "  CXX $1"
    $CXX "${CXXFLAGS[@]}" -w -c "$1" -o "$object"
  fi
}

compile "$ROOT/ares/psp/psp.cpp" psp always
compile "$ROOT/ares/ares/android_globals.cpp" globals
#the parts of ares's framework the core needs beside its own code: the node tree, and the debugger it refers to
compile "$ROOT/tests/psp/ares/ares-runtime.cpp" ares-runtime
compile "$ROOT/nall/nall/nall.cpp" nall
compile "$ROOT/thirdparty/sljitAllocator.cpp" sljit-allocator
if [[ ! -f $OUT/obj/sljit.o || $ROOT/thirdparty/sljit/sljit_src/sljitLir.c -nt $OUT/obj/sljit.o ]]; then
  echo "  CC  sljitLir.c"
  $CC -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I"$ROOT/thirdparty" -c "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" \
    -o "$OUT/obj/sljit.o"
fi
#libchdr, and the zlib, LZMA and zstd decoders it carries
for source in "$CHDR"/src/libchdr_*.c "$CHDR/deps/lzma-25.01/src/LzmaDec.c" "$CHDR/deps/miniz-3.1.1/miniz.c" \
  "$CHDR/deps/zstd-1.5.7/zstddeclib.c"; do
  object=$OUT/obj/chdr-$(basename "$source" .c).o
  if [[ ! -f $object || $source -nt $object ]]; then
    echo "  CC  $(basename "$source")"
    $CC -O1 -w "${SYSROOT[@]}" -I"$CHDR/include" -I"$CHDR/deps/zstd-1.5.7" -c "$source" -o "$object"
  fi
done
if [[ ! -f $OUT/obj/libco.o ]]; then
  echo "  CC  libco.c"
  $CC -O1 -w "${SYSROOT[@]}" -I"$ROOT/libco" -c "$ROOT/libco/libco.c" -o "$OUT/obj/libco.o"
fi
echo "  CXX $HERE/runner.cpp"
$CXX "${CXXFLAGS[@]}" -Wall -Wextra -Werror -c "$HERE/runner.cpp" -o "$OUT/obj/runner.o"

$CXX "${SYSROOT[@]}" -o "$OUT/psp-runner" "$OUT"/obj/*.o "${LIBRARIES[@]}"
echo "built $OUT/psp-runner"
