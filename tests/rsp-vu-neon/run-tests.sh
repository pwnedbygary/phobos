#!/usr/bin/env bash
#Builds and runs the RSP vector NEON sequence checks on an AArch64 host (macOS or Linux).
#usage: tests/rsp-vu-neon/run-tests.sh [states per sequence] [seed]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-rsp-vu-neon-tests
mkdir -p "$OUT"

CXX=${CXX:-c++}
SYSROOT=()
#the Command Line Tools linker can be older than the newest SDK next to it
if [[ $(uname) == Darwin ]] && SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
$CXX -std=c++20 -O2 -Wno-#warnings "${SYSROOT[@]}" -I"$ROOT/thirdparty" "$HERE/rsp-vu-neon.cpp" -o "$OUT/rsp-vu-neon"
"$OUT/rsp-vu-neon" "$@"
