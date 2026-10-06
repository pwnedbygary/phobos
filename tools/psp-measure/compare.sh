#!/usr/bin/env bash
#Builds compare.cpp against the PSP core (as tests/allegrex/run-tests.sh builds the tests, but optimized and
#without sanitizers) and runs it on a folder of psp-measure's VFPU and FPU results (results/vfpu).
#usage: tools/psp-measure/compare.sh <results folder> (its vfpu/ when it has one, so results/ or results/vfpu)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
TESTS="$ROOT/tests/allegrex"
OUT=${TMPDIR:-/tmp}/phobos-vfpu-compare
mkdir -p "$OUT"
[[ $# -eq 1 ]] || { echo "usage: $0 <results folder>" >&2; exit 1; }

CC=${CC:-cc}
CXX=${CXX:-c++}
SYSROOT=()
LIBRARIES=(-lpthread -ldl)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation)
fi
DEFINES=(-DBUILD_DEBUG -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty")

$CXX -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -c "$ROOT/nall/nall/nall.cpp" -o "$OUT/nall.o"
$CC -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I "$ROOT/thirdparty" -c "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -o "$OUT/sljit.o"
$CXX -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -c "$ROOT/thirdparty/sljitAllocator.cpp" -o "$OUT/allocator.o"
$CXX -std=c++20 -O2 -Wall -Wextra "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -include "$TESTS/prelude.hpp" \
  "$ROOT/ares/psp/cpu/allegrex.cpp" "$HERE/compare.cpp" "$OUT/nall.o" "$OUT/sljit.o" "$OUT/allocator.o" \
  "${LIBRARIES[@]}" -o "$OUT/compare"
FOLDER=$1
[[ -d "$1/vfpu" ]] && FOLDER="$1/vfpu"
"$OUT/compare" "$FOLDER"
