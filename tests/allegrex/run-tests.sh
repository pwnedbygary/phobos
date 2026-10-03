#!/usr/bin/env bash
#Builds and runs the Allegrex CPU tests (ares/psp/cpu) on the host, with the undefined-behavior sanitizer, and on Linux
#the address sanitizer too (its runtime can hang at start on a macOS newer than the toolchain).
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
#the Command Line Tools linker can be older than the newest SDK next to it
if [[ $(uname) == Darwin ]] && SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
# shellcheck disable=SC2086
$CXX -std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" \
  "$ROOT/ares/psp/cpu/allegrex.cpp" "$HERE/allegrex.cpp" -o "$OUT/allegrex"
"$OUT/allegrex"
