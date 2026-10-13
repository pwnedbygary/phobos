#!/usr/bin/env bash
#Builds and runs the Allegrex tests (ares/psp/cpu) on the host, with the undefined-behavior and address sanitizers.
#Should the address sanitizer's runtime hang at start (as it once did on a macOS newer than its toolchain),
#SANITIZE="-fsanitize=undefined -fno-sanitize-recover=all" leaves it out. Every test runs on the interpreter and
#again on the recompiler.
#The CPU is built against nall, ares's types and sljit alone (prelude.hpp), not the rest of ares. Everything is
#built with BUILD_DEBUG (unless PHOBOS_TESTS_MODE says release), which also turns on sljit's own checks of how it's
#called; sljit's C file and the C++ files must agree on that, as it changes the layout of sljit's structures.
#Objects are kept between runs by tests/objects.sh, this checkout's in its own folder, each compiled again when what
#it was built from changes (a header it includes too) or its compile command does.
#usage: tests/allegrex/run-tests.sh   (SANITIZE= turns the sanitizers off; PHOBOS_JOBS: compiles at once;
#PHOBOS_TESTS_MODE=release: built as the app is, below; PHOBOS_TESTS_REQUIRE and PHOBOS_TESTS_REPORT as in
#tests/psp/run-tests.sh)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-allegrex-tests
source "$ROOT/tests/objects.sh"

CC=${CC:-cc}
CXX=${CXX:-c++}
#PHOBOS_TESTS_MODE: debug (the default: BUILD_DEBUG, sljit's own checks on, -O1, the sanitizers unless SANITIZE says
#otherwise) or release, built as the app's release builds are (CMakeLists.txt: BUILD_RELEASE, SLJIT_DEBUG=0, -O3 and
#its math and loop flags, without its link-time optimization), with no sanitizers unless SANITIZE names some; sljit's C
#file and the C++ files get the same mode either way.
case ${PHOBOS_TESTS_MODE:-debug} in
  debug)
    MODE=(-DBUILD_DEBUG)
    OPTIMIZE=(-O1)
    SANITIZE=${SANITIZE--fsanitize=address,undefined -fno-sanitize-recover=all} ;;
  release)
    MODE=(-DBUILD_RELEASE -DSLJIT_DEBUG=0)
    OPTIMIZE=(-O3 -ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math)
    SANITIZE=${SANITIZE-} ;;
  *) echo "PHOBOS_TESTS_MODE is debug or release, not ${PHOBOS_TESTS_MODE}" >&2; exit 1 ;;
esac
SYSROOT=()
LIBRARIES=(-lpthread -ldl)
#the Command Line Tools linker can be older than the newest SDK next to it
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation)
fi
DEFINES=("${MODE[@]}" -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty")

#Results measured on a real PSP (measured.cpp), unpacked into this checkout's folder, each file through a temporary
#one, so one cut short isn't kept; xz checks a packed file's own integrity as it unpacks it, failing on a damaged one.
#A file is unpacked again when its packed file's SHA-256, kept beside it, changes: a tree copied with its times kept
#can hold older packed files than the folder's. SHA256SUMS* beside them are the sums of the PSP's own files, of which
#some are kept here whole and the rest in part (the 16k sets) or not at all: those kept whole are checked against
#them.
sha256() {
  if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
MEASURED="$BUILD/measured"
mkdir -p "$MEASURED"
command -v xz > /dev/null || { echo "tests/allegrex/measured needs xz to unpack" >&2; exit 1; }
for packed in "$HERE"/measured/*.xz; do
  unpacked="$MEASURED/$(basename "$packed" .xz)"
  sum=$(sha256 "$packed")
  if [[ ! -f $unpacked || $(cat "$unpacked.sha256" 2> /dev/null) != "$sum" ]]; then
    if ! xz -dc "$packed" > "$unpacked.part-$$"; then
      rm -f "$unpacked.part-$$"
      echo "tests/allegrex/measured/$(basename "$packed") doesn't unpack (damaged?)" >&2
      exit 1
    fi
    mv -f "$unpacked.part-$$" "$unpacked"
    echo "$sum" > "$unpacked.sha256"
  fi
done
for sums in "$HERE"/measured/SHA256SUMS*; do
  while read -r sum name; do
    file="$MEASURED/$name"
    [[ -f $file ]] || file="$HERE/measured/$name"
    [[ -f $file ]] || continue  #(a file of the PSP's not kept here)
    if [[ $(sha256 "$file") != "$sum" ]]; then
      echo "tests/allegrex/measured: $name isn't the file the PSP made ($(basename "$sums"))" >&2
      exit 1
    fi
  done < "$sums"
done

#nall, sljit and sljit's allocator: built without the sanitizers (their lines say so as they're compiled)
object "$CXX" "$ROOT/nall/nall/nall.cpp" -std=c++20 "${OPTIMIZE[@]}" "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" "${OPTIMIZE[@]}" "${SYSROOT[@]}" "${DEFINES[@]}" -I "$ROOT/thirdparty"
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" -std=c++20 "${OPTIMIZE[@]}" "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"

# shellcheck disable=SC2206
FLAGS=(-std=c++20 "${OPTIMIZE[@]}" -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
  -include "$HERE/prelude.hpp")
for source in "$ROOT/ares/psp/cpu/allegrex.cpp" "$HERE/allegrex.cpp" "$HERE/vfpu.cpp" "$HERE/measured.cpp"; do
  object "$CXX" "$source" "${FLAGS[@]}"
done
objects_done
# shellcheck disable=SC2086
objects_program allegrex $SANITIZE "${SYSROOT[@]}" "${LIBRARIES[@]}"
# shellcheck disable=SC2086
$CXX $SANITIZE "${SYSROOT[@]}" "${OBJECTS[@]}" "${LIBRARIES[@]}" -o "$REPLY.part-$$"
mv -f "$REPLY.part-$$" "$REPLY"
ALLEGREX_MEASURED="$MEASURED" "$REPLY"
