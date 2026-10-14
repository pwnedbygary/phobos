#!/usr/bin/env bash
#Builds and runs the Allegrex tests (ares/psp/cpu) on the host, with the undefined-behavior and address sanitizers.
#Should the address sanitizer's runtime hang at start (as it once did on a macOS newer than its toolchain),
#SANITIZE="-fsanitize=undefined -fno-sanitize-recover=all" leaves it out. Every test runs on the interpreter and
#again on the recompiler.
#The CPU is built against nall, ares's types and sljit alone (prelude.hpp), not the rest of ares. Everything is
#built with BUILD_DEBUG, which also turns on sljit's own checks of how it's called; sljit's C file and the C++
#files must agree on that, as it changes the layout of sljit's structures.
#Objects are kept between runs by tests/objects.sh, this checkout's in its own folder, each compiled again when what
#it was built from changes (a header it includes too) or its compile command does.
#usage: tests/allegrex/run-tests.sh   (SANITIZE= turns the sanitizers off; PHOBOS_JOBS: compiles at once)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-allegrex-tests
source "$ROOT/tests/objects.sh"

CC=${CC:-cc}
CXX=${CXX:-c++}
SANITIZE=${SANITIZE--fsanitize=address,undefined -fno-sanitize-recover=all}
SYSROOT=()
LIBRARIES=(-lpthread -ldl)
#the Command Line Tools linker can be older than the newest SDK next to it
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation)
fi
DEFINES=(-DBUILD_DEBUG -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty")

#Results measured on a real PSP (measured.cpp), unpacked once into this checkout's folder, each file through a
#temporary one, so one cut short isn't kept.
MEASURED="$BUILD/measured"
mkdir -p "$MEASURED"
for packed in "$HERE"/measured/*.xz; do
  unpacked="$MEASURED/$(basename "$packed" .xz)"
  if [[ ! -f $unpacked || $packed -nt $unpacked ]]; then
    if ! xz -dc "$packed" > "$unpacked.part-$$"; then
      rm -f "$unpacked.part-$$"
      echo "tests/allegrex/measured/$(basename "$packed") doesn't unpack" >&2
      exit 1
    fi
    mv -f "$unpacked.part-$$" "$unpacked"
  fi
done

#nall, sljit and sljit's allocator: built without the sanitizers (their lines say so as they're compiled)
object "$CXX" "$ROOT/nall/nall/nall.cpp" -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I "$ROOT/thirdparty"
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"

# shellcheck disable=SC2206
FLAGS=(-std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
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
