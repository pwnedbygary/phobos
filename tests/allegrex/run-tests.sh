#!/usr/bin/env bash
#Builds and runs the Allegrex tests (ares/psp/cpu) on the host, with the undefined-behavior sanitizer, and on Linux
#the address sanitizer too (its runtime can hang at start on a macOS newer than the toolchain). Every test runs on
#the interpreter and again on the recompiler.
#The CPU is built against nall, ares's types and sljit alone (prelude.hpp), not the rest of ares. Everything is
#built with BUILD_DEBUG, which also turns on sljit's own checks of how it's called; sljit's C file and the C++
#files must agree on that, as it changes the layout of sljit's structures.
#usage: tests/allegrex/run-tests.sh   (SANITIZE= turns the sanitizers off)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-allegrex-tests
mkdir -p "$OUT"

CC=${CC:-cc}
CXX=${CXX:-c++}
if [[ $(uname) == Darwin ]]; then DEFAULT_SANITIZE="-fsanitize=undefined"; else DEFAULT_SANITIZE="-fsanitize=address,undefined"; fi
SANITIZE=${SANITIZE-$DEFAULT_SANITIZE -fno-sanitize-recover=all}
SYSROOT=()
LIBRARIES=(-lpthread -ldl)
#the Command Line Tools linker can be older than the newest SDK next to it
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation)
fi
DEFINES=(-DBUILD_DEBUG -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty")

#nall and sljit change rarely and take the longest to build, so they're built once and kept.
NALL="$OUT/nall-debug.o"
if [[ ! -f $NALL || $ROOT/nall/nall/nall.cpp -nt $NALL ]]; then
  $CXX -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -c "$ROOT/nall/nall/nall.cpp" -o "$NALL"
fi
SLJIT="$OUT/sljit-debug.o"
if [[ ! -f $SLJIT || $ROOT/thirdparty/sljit/sljit_src/sljitLir.c -nt $SLJIT ]]; then
  $CC -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I "$ROOT/thirdparty" -c "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -o "$SLJIT"
fi
ALLOCATOR="$OUT/sljit-allocator.o"
if [[ ! -f $ALLOCATOR || $ROOT/thirdparty/sljitAllocator.cpp -nt $ALLOCATOR ]]; then
  $CXX -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -c "$ROOT/thirdparty/sljitAllocator.cpp" -o "$ALLOCATOR"
fi

# shellcheck disable=SC2086
$CXX -std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
  -include "$HERE/prelude.hpp" "$ROOT/ares/psp/cpu/allegrex.cpp" "$HERE/allegrex.cpp" "$HERE/vfpu.cpp" "$NALL" "$SLJIT" "$ALLOCATOR" \
  "${LIBRARIES[@]}" -o "$OUT/allegrex"
"$OUT/allegrex"
