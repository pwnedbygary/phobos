#!/usr/bin/env bash
#Builds and runs the PSP system's tests (ares/psp beyond the CPU) on the host, as tests/allegrex/run-tests.sh does
#the CPU's: against nall, ares's types and sljit alone, with the undefined-behavior and address sanitizers.
#usage: tests/psp/run-tests.sh   (SANITIZE= turns the sanitizers off; PSP_TEST_PROGRAMS: see loader.cpp;
#PSP_AUTOTESTS: a pspautotests checkout, whose sample files atrac.cpp and mp3.cpp decode with FFmpeg)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
#the test programs and a real PSP's measurements kept in the repository, unless named otherwise (empty: none)
export PSP_TEST_PROGRAMS=${PSP_TEST_PROGRAMS-$ROOT/tests/psp/programs}
export PSP_GE_RESULTS=${PSP_GE_RESULTS-$ROOT/tests/psp/measurements/ge-round3}
OUT=${TMPDIR:-/tmp}/phobos-psp-tests
mkdir -p "$OUT"

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
DEFINES=(-DBUILD_DEBUG -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1 -DVK_NO_PROTOTYPES)
INCLUDES=(-isystem "$ROOT/nall" -isystem "$ROOT/ares" -isystem "$ROOT" -isystem "$ROOT/thirdparty"
  -isystem "$ROOT/thirdparty/volk" -isystem "$ROOT/thirdparty/Vulkan-Headers/include")
#The GPU renderer's shaders (ares/psp/ge/gpu/shaders): shaders.hpp must be what its GLSL compiles to (its recorded
#hash; with glslang around, compiled again and compared). Its tests (gpu.cpp) need a Vulkan GPU, skipping without
#one (but for the lost GPU's, which pretends);
#Vulkan is loaded at run time (volk), on macOS from Homebrew's loader and MoltenVK where they are (brew install
#vulkan-loader molten-vk), which the system's library paths don't name.
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
VOLK="$OUT/volk.o"
if [[ ! -f $VOLK || $ROOT/thirdparty/volk/volk.c -nt $VOLK ]]; then
  $CC -O1 "${SYSROOT[@]}" -DVK_NO_PROTOTYPES -I "$ROOT/thirdparty/Vulkan-Headers/include" \
    -c "$ROOT/thirdparty/volk/volk.c" -o "$VOLK"
fi

# shellcheck disable=SC2086
$CXX -std=c++20 -O1 -g -Wall -Wextra -Werror $SANITIZE "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
  -include "$ROOT/tests/allegrex/prelude.hpp" "$ROOT/ares/psp/cpu/allegrex.cpp" "$ROOT/ares/psp/memory/memory.cpp" \
  "$ROOT/ares/psp/kernel/loader.cpp" "$ROOT/ares/psp/kernel/kernel.cpp" "$ROOT/ares/psp/ge/ge.cpp" \
  "$HERE/main.cpp" "$HERE/memory.cpp" "$HERE/loader.cpp" "$HERE/kernel.cpp" "$HERE/callbacks.cpp" "$HERE/power.cpp" \
  "$HERE/audio.cpp" "$HERE/sas.cpp" "$HERE/utility.cpp" "$HERE/pools.cpp" "$HERE/messages.cpp" "$HERE/media.cpp" \
  "$HERE/mutexes.cpp" "$HERE/timers.cpp" "$HERE/threadman.cpp" \
  "$HERE/atrac.cpp" "$HERE/mp3.cpp" "$HERE/movies.cpp" "$HERE/psmf.cpp" "$HERE/font.cpp" \
  "$HERE/files.cpp" "$HERE/async.cpp" "$HERE/disc.cpp" \
  "$HERE/disc-formats.cpp" "$HERE/crypto.cpp" "$HERE/decrypt.cpp" "$HERE/modules.cpp" \
  "$HERE/states.cpp" "$HERE/ge.cpp" "$HERE/draw.cpp" "$HERE/draw3d.cpp" "$HERE/curves.cpp" \
  "$HERE/measure.cpp" \
  "$ROOT/ares/psp/ge/gpu/gpu.cpp" "$HERE/gpu.cpp" \
  "$NALL" "$SLJIT" "$ALLOCATOR" "$VOLK" \
  "${LIBRARIES[@]}" -o "$OUT/psp"
"$OUT/psp"
