#!/usr/bin/env bash
#Builds and runs the Neo Geo CD layout and save state checks on the host (Linux, g++ >= 13;
#macOS with clang).
#usage: tests/ngcd/run-tests.sh [build-dir]   (PHOBOS_JOBS: compiles at once)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
#objects are kept between runs by tests/objects.sh, each compiled again when what it was built from changes (a
#header it includes too) or its compile command does; each checkout gets its own default build dir
BUILD=${1:-${TMPDIR:-/tmp}/phobos-ngcd-tests-$(printf %s "$ROOT" | cksum | cut -d' ' -f1)}
source "$ROOT/tests/objects.sh"

CXX=${CXX:-g++}
CC=${CC:-gcc}
CXXFLAGS=(-std=c++20 -O1 -g -w -DPROFILE_PERFORMANCE -DCORE_NG -I"$HERE/stub" -I"$ROOT" -I"$ROOT/nall"
  -I"$ROOT/nall/nall" -I"$ROOT/libco" -I"$ROOT/ares" -I"$ROOT/thirdparty" -I"$ROOT/thirdparty/ymfm/src")

object "$CXX" "$ROOT/ares/ng/ng.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/ares/ares/android_globals.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$HERE/ares-runtime.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/ares/ares/memory/fixed-allocator.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/nall/nall/nall.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/ares/component/processor/m68000/m68000.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/ares/component/processor/z80/z80.cpp" "${CXXFLAGS[@]}"
for f in ymfm_opn ymfm_adpcm ymfm_ssg ymfm_misc; do
  object "$CXX" "$ROOT/thirdparty/ymfm/src/$f.cpp" "${CXXFLAGS[@]}"
done
object "$CC" "$ROOT/libco/libco.c" -O1 -w -I"$ROOT/libco"
object "$CXX" "$HERE/ngcd.cpp" "${CXXFLAGS[@]}"
objects_done

#macOS: nall needs CoreFoundation; LDFLAGS can name an SDK the linker understands (-isysroot)
LIBS=(-lpthread -ldl)
if [[ $(uname) == Darwin ]]; then LIBS+=(-framework CoreFoundation); fi
# shellcheck disable=SC2086
$CXX ${LDFLAGS:-} -o "$BUILD/ngcd.part-$$" "${OBJECTS[@]}" "${LIBS[@]}"
mv -f "$BUILD/ngcd.part-$$" "$BUILD/ngcd"
"$BUILD/ngcd"
