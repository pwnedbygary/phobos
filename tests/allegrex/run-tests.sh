#!/usr/bin/env bash
#Builds and runs the Allegrex tests (ares/psp/cpu) on the host, with the undefined-behavior sanitizer, and on Linux
#the address sanitizer too (its runtime can hang at start on a macOS newer than the toolchain).
#The CPU is built against nall and ares's types alone (prelude.hpp), not the rest of ares.
#usage: tests/allegrex/run-tests.sh   (SANITIZE= turns the sanitizers off)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-allegrex-tests
mkdir -p "$OUT"

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
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT")

#nall changes rarely and takes the longest to build, so it's built once and kept.
NALL="$OUT/nall.o"
if [[ ! -f $NALL || $ROOT/nall/nall/nall.cpp -nt $NALL ]]; then
  $CXX -std=c++20 -O1 "${SYSROOT[@]}" "${INCLUDES[@]}" -c "$ROOT/nall/nall/nall.cpp" -o "$NALL"
fi

# shellcheck disable=SC2086
$CXX -std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${INCLUDES[@]}" \
  -include "$HERE/prelude.hpp" "$ROOT/ares/psp/cpu/allegrex.cpp" "$HERE/allegrex.cpp" "$NALL" \
  "${LIBRARIES[@]}" -o "$OUT/allegrex"
"$OUT/allegrex"
