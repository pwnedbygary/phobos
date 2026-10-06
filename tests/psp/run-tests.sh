#!/usr/bin/env bash
#Builds and runs the PSP system's tests (ares/psp beyond the CPU) on the host, as tests/allegrex/run-tests.sh does
#the CPU's: against nall, ares's types and sljit alone, with the undefined-behavior sanitizer, and on Linux the
#address sanitizer too.
#usage: tests/psp/run-tests.sh   (SANITIZE= turns the sanitizers off; PSP_TEST_PROGRAMS: see loader.cpp)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-psp-tests
mkdir -p "$OUT"

CC=${CC:-cc}
CXX=${CXX:-c++}
if [[ $(uname) == Darwin ]]; then DEFAULT_SANITIZE="-fsanitize=undefined"; else DEFAULT_SANITIZE="-fsanitize=address,undefined"; fi
SANITIZE=${SANITIZE-$DEFAULT_SANITIZE -fno-sanitize-recover=all}
#The address sanitizer's check for stack use after return gives every call a fresh frame on its own heap, which the
#interpreter's per-instruction and the GE's per-pixel functions make hundreds of times slower; its other checks stay.
export ASAN_OPTIONS=${ASAN_OPTIONS-detect_stack_use_after_return=0}
SYSROOT=()
LIBRARIES=(-lpthread -ldl)
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
  -include "$ROOT/tests/allegrex/prelude.hpp" "$ROOT/ares/psp/cpu/allegrex.cpp" "$ROOT/ares/psp/memory/memory.cpp" \
  "$ROOT/ares/psp/kernel/loader.cpp" "$ROOT/ares/psp/kernel/kernel.cpp" "$ROOT/ares/psp/ge/ge.cpp" \
  "$HERE/main.cpp" "$HERE/memory.cpp" "$HERE/loader.cpp" "$HERE/kernel.cpp" "$HERE/files.cpp" "$HERE/ge.cpp" \
  "$HERE/draw.cpp" "$HERE/draw3d.cpp" "$HERE/measure.cpp" \
  "$NALL" "$SLJIT" "$ALLOCATOR" \
  "${LIBRARIES[@]}" -o "$OUT/psp"
"$OUT/psp"
