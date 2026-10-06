#!/usr/bin/env bash
#Builds the PSP as an ares system (ares/psp/psp.cpp, the whole core as Phobos builds it, with ares's node tree) and
#runs system.cpp's checks on the host. tests/psp/run-tests.sh tests the parts underneath on their own.
#usage: tests/psp/ares/run-tests.sh   (PSP_TEST_PROGRAMS: the test programs' folder, see system.cpp)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
#ares and nall change rarely and take the longest to build, so their objects are kept and rebuilt when their top
#source is newer; a fresh TMPDIR rebuilds everything (after editing headers they include, say).
OUT=${TMPDIR:-/tmp}/phobos-psp-ares-tests
mkdir -p "$OUT/obj"

CC=${CC:-cc}
CXX=${CXX:-c++}
SYSROOT=()
#zlib packs the compressed images (../disc-image.hpp, ../disc-formats.hpp)
LIBRARIES=(-lpthread -ldl -lz)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation -lz)
fi
#One build mode for every file: nall's headers pick debug when none is given, and sljit's own C file, without them,
#would then disagree with the rest about SLJIT_DEBUG (sljitConfigPre.h), and so about its compiler's layout.
#ARES_ENABLE_CHD: CHD disc images are read with libchdr, as Phobos's builds read them
DEFINES=(-DBUILD_DEBUG -DCORE_PSP -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1 -DARES_ENABLE_CHD)
#as system headers: ares's aren't written for -Wall, and the test itself is built with it
CHDR=$ROOT/thirdparty/libchdr
INCLUDES=(-isystem "$ROOT" -isystem "$ROOT/nall" -isystem "$ROOT/nall/nall" -isystem "$ROOT/libco"
  -isystem "$ROOT/ares" -isystem "$ROOT/thirdparty" -isystem "$CHDR/include")
#FFmpeg's decoders, as Phobos's builds have them (docs/psp-core.md, part 26); PSP_FFMPEG=0 builds without them. The
#core's object is built afresh every run (below), so switching needs no fresh TMPDIR.
if [[ ${PSP_FFMPEG:-1} != 0 ]]; then
  FFMPEG=$("$ROOT/thirdparty/ffmpeg/build.sh" host | tail -1)
  DEFINES+=(-DARES_ENABLE_FFMPEG)
  INCLUDES+=(-isystem "$FFMPEG/include")
  LIBRARIES+=(-L"$FFMPEG/lib" -lavcodec -lavutil -Wl,-rpath,"$FFMPEG/lib")
fi
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
compile "$HERE/ares-runtime.cpp" ares-runtime
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
echo "  CXX $HERE/system.cpp"
$CXX "${CXXFLAGS[@]}" -Wall -Wextra -Werror -c "$HERE/system.cpp" -o "$OUT/obj/system.o"

$CXX "${SYSROOT[@]}" -o "$OUT/psp-ares" "$OUT"/obj/*.o "${LIBRARIES[@]}"
"$OUT/psp-ares"
