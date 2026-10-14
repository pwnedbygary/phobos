#!/usr/bin/env bash
#Builds and runs the PSP system's tests (ares/psp beyond the CPU) on the host, as tests/allegrex/run-tests.sh does
#the CPU's: against nall, ares's types and sljit alone, with the undefined-behavior and address sanitizers. Objects are
#kept between runs by tests/objects.sh, this checkout's in its own folder, each compiled again when what it was built
#from changes (a header it includes too) or its compile command does.
#usage: tests/psp/run-tests.sh   (SANITIZE= turns the sanitizers off; PSP_TEST_PROGRAMS: see loader.cpp;
#PSP_AUTOTESTS: a pspautotests checkout, whose sample files atrac.cpp and mp3.cpp decode with FFmpeg; PHOBOS_JOBS:
#compiles at once)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
#the test programs and a real PSP's measurements kept in the repository, unless named otherwise (empty: none)
export PSP_TEST_PROGRAMS=${PSP_TEST_PROGRAMS-$ROOT/tests/psp/programs}
export PSP_GE_RESULTS=${PSP_GE_RESULTS-$ROOT/tests/psp/measurements/ge-round3}
OUT=${TMPDIR:-/tmp}/phobos-psp-tests
source "$ROOT/tests/objects.sh"
#round 6 of the PSP's GE measurements (packed with xz, as tests/allegrex/measured's are), unpacked once into this
#checkout's folder, unless PSP_GE_ROUND6 names another folder (empty: none); each file through a temporary one, so one
#cut short isn't kept, and all of them again if they don't match SHA256SUMS (another commit's, after a checkout, say)
if [[ -z ${PSP_GE_ROUND6+set} ]]; then
  export PSP_GE_ROUND6="$BUILD/ge-round6"
  mkdir -p "$PSP_GE_ROUND6"
  MEASURED6="$HERE/measurements/ge-round6"
  unpack6() {
    for packed in "$MEASURED6"/*.xz; do
      unpacked="$PSP_GE_ROUND6/$(basename "$packed" .xz)"
      if [[ ${1:-} == all || ! -f $unpacked || $packed -nt $unpacked ]]; then
        if xz -dc "$packed" > "$unpacked.part-$$"; then mv -f "$unpacked.part-$$" "$unpacked"; else rm -f "$unpacked.part-$$"; fi
      fi
    done
    cp "$MEASURED6/manifest6.txt" "$PSP_GE_ROUND6/manifest6.txt.part-$$"
    mv -f "$PSP_GE_ROUND6/manifest6.txt.part-$$" "$PSP_GE_ROUND6/manifest6.txt"
  }
  sums6() {
    local sum=(shasum -a 256)
    if command -v sha256sum > /dev/null; then sum=(sha256sum); fi
    (cd "$PSP_GE_ROUND6" && "${sum[@]}" -c "$MEASURED6/SHA256SUMS") > "$BUILD/ge-round6.sums" 2>&1
  }
  command -v xz > /dev/null || { echo "tests/psp/measurements/ge-round6 needs xz to unpack" >&2; exit 1; }
  unpack6
  if ! sums6; then
    unpack6 all
    if ! sums6; then
      grep -v ': OK$' "$BUILD/ge-round6.sums" >&2
      echo "tests/psp/measurements/ge-round6: unpacked, the files don't match SHA256SUMS" >&2
      exit 1
    fi
  fi
fi

CC=${CC:-cc}
CXX=${CXX:-c++}
SANITIZE=${SANITIZE--fsanitize=address,undefined -fno-sanitize-recover=all}
#The address sanitizer's check for stack use after return gives every call a fresh frame on its own heap, which the
#interpreter's per-instruction and the GE's per-pixel functions make hundreds of times slower; its other checks stay.
export ASAN_OPTIONS=${ASAN_OPTIONS-detect_stack_use_after_return=0}
SYSROOT=()
#zlib packs the disc tests' CSO images (disc-image.hpp)
LIBRARIES=(-lpthread -ldl -lz)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation -lz)
fi
DEFINES=(-DBUILD_DEBUG -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty"
  -isystem "$ROOT/thirdparty/Vulkan-Headers/include")
#The hardware renderer's shaders (ares/psp/ge/gpu/shaders): shaders.hpp must be what its GLSL compiles to (its
#recorded hash; with glslang around, compiled again and compared). Its tests (gpu.cpp) need a Vulkan GPU, skipping
#without one; PSP_GPU_ON_CPU=1 lets them take one that draws on the CPU (Mesa's lavapipe, as CI does). Vulkan is opened
#at run time, on macOS from Homebrew's loader and MoltenVK where they are (brew install vulkan-loader molten-vk), which
#the system's library paths don't name.
"$ROOT/ares/psp/ge/gpu/shaders/compile.sh" --check
if [[ $(uname) == Darwin && -z ${DYLD_FALLBACK_LIBRARY_PATH:-} ]]; then
  export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib:/usr/local/lib:/usr/lib
fi
#FFmpeg's decoders (docs/psp-core.md, part 26), built once for the host by thirdparty/ffmpeg/build.sh and kept;
#PSP_FFMPEG=0 builds without them, as the core is without its define (no decoders: the libraries refuse streams).
if [[ ${PSP_FFMPEG:-1} != 0 ]]; then
  FFMPEG=$("$ROOT/thirdparty/ffmpeg/build.sh" host | tail -1)
  DEFINES+=(-DARES_ENABLE_FFMPEG)
  INCLUDES+=(-isystem "$FFMPEG/include")
  LIBRARIES+=(-L"$FFMPEG/lib" -lavcodec -lavutil -Wl,-rpath,"$FFMPEG/lib")
fi

#nall, sljit and sljit's allocator: built without the sanitizers (their lines say so as they're compiled)
object "$CXX" "$ROOT/nall/nall/nall.cpp" -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I "$ROOT/thirdparty"
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" -std=c++20 -O1 "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"

# shellcheck disable=SC2206
FLAGS=(-std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}"
  -include "$ROOT/tests/allegrex/prelude.hpp")
for source in "$ROOT/ares/psp/cpu/allegrex.cpp" "$ROOT/ares/psp/memory/memory.cpp" \
  "$ROOT/ares/psp/kernel/loader.cpp" "$ROOT/ares/psp/kernel/kernel.cpp" "$ROOT/ares/psp/ge/ge.cpp" \
  "$HERE/main.cpp" "$HERE/memory.cpp" "$HERE/loader.cpp" "$HERE/kernel.cpp" "$HERE/callbacks.cpp" "$HERE/power.cpp" \
  "$HERE/audio.cpp" "$HERE/sas.cpp" "$HERE/utility.cpp" "$HERE/dialogs.cpp" "$HERE/pools.cpp" "$HERE/messages.cpp" \
  "$HERE/media.cpp" \
  "$HERE/mutexes.cpp" "$HERE/timers.cpp" "$HERE/threadman.cpp" \
  "$HERE/atrac.cpp" "$HERE/mp3.cpp" "$HERE/movies.cpp" "$HERE/psmf.cpp" "$HERE/font.cpp" "$HERE/ccc.cpp" \
  "$HERE/jpeg.cpp" \
  "$HERE/files.cpp" "$HERE/async.cpp" "$HERE/disc.cpp" \
  "$HERE/disc-formats.cpp" "$HERE/disc-info.cpp" "$HERE/desktop-disc-info.cpp" "$HERE/crypto.cpp" \
  "$HERE/decrypt.cpp" "$HERE/pgd.cpp" "$HERE/modules.cpp" "$ROOT/desktop/PspDiscInfo.cpp" \
  "$HERE/states.cpp" "$HERE/ge.cpp" "$HERE/draw.cpp" "$HERE/draw3d.cpp" "$HERE/curves.cpp" \
  "$HERE/measure.cpp" "$HERE/measured6.cpp" "$ROOT/ares/psp/ge/gpu/gpu.cpp" "$HERE/gpu.cpp"; do
  object "$CXX" "$source" "${FLAGS[@]}"
done
objects_done
# shellcheck disable=SC2086
objects_program psp $SANITIZE "${SYSROOT[@]}" "${LIBRARIES[@]}"
# shellcheck disable=SC2086
$CXX $SANITIZE "${SYSROOT[@]}" "${OBJECTS[@]}" "${LIBRARIES[@]}" -o "$REPLY.part-$$"
mv -f "$REPLY.part-$$" "$REPLY"
"$REPLY"
