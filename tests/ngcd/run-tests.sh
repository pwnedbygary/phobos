#!/usr/bin/env bash
#Builds and runs the Neo Geo CD layout checks on the host (Linux, g++ >= 13).
#usage: tests/ngcd/run-tests.sh [build-dir]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
#objects are reused when their top-level source is older, so each checkout gets
#its own default build dir; use a fresh one after editing included headers
OUT=${1:-${TMPDIR:-/tmp}/phobos-ngcd-tests-$(printf %s "$ROOT" | cksum | cut -d' ' -f1)}
mkdir -p "$OUT/obj"

CXX=${CXX:-g++}
CC=${CC:-gcc}
CXXFLAGS="-std=c++20 -O1 -g -w -DPROFILE_PERFORMANCE -DCORE_NG -I$HERE/stub -I$ROOT -I$ROOT/nall -I$ROOT/nall/nall -I$ROOT/libco -I$ROOT/ares -I$ROOT/thirdparty -I$ROOT/thirdparty/ymfm/src"

compile() {  #source object [always]
  local obj=$OUT/obj/$2.o
  if [[ ${3:-} == always || ! -f $obj || $1 -nt $obj ]]; then
    echo "  CXX $1"
    $CXX $CXXFLAGS -c "$1" -o "$obj"
  fi
}

compile "$ROOT/ares/ng/ng.cpp" ng always
compile "$ROOT/ares/ares/android_globals.cpp" globals
compile "$HERE/ares-runtime.cpp" ares-runtime
compile "$ROOT/ares/ares/memory/fixed-allocator.cpp" fixed-allocator
compile "$ROOT/nall/nall/nall.cpp" nall
compile "$ROOT/ares/component/processor/m68000/m68000.cpp" m68000
compile "$ROOT/ares/component/processor/z80/z80.cpp" z80
for f in ymfm_opn ymfm_adpcm ymfm_ssg ymfm_misc; do
  compile "$ROOT/thirdparty/ymfm/src/$f.cpp" "$f"
done
if [[ ! -f $OUT/obj/libco.o ]]; then
  echo "  CC  libco/libco.c"
  $CC -O1 -w -I"$ROOT/libco" -c "$ROOT/libco/libco.c" -o "$OUT/obj/libco.o"
fi
compile "$HERE/ngcd.cpp" ngcd always

$CXX -o "$OUT/ngcd" "$OUT"/obj/*.o -lpthread -ldl
"$OUT/ngcd"
