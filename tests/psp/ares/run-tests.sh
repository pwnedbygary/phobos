#!/usr/bin/env bash
#Builds the PSP as an ares system (ares/psp/psp.cpp, the whole core as Phobos builds it, with ares's node tree) and
#runs system.cpp's checks on the host. tests/psp/run-tests.sh tests the parts underneath on their own.
#usage: tests/psp/ares/run-tests.sh   (PSP_TEST_PROGRAMS: the test programs' folder, see system.cpp; PHOBOS_JOBS:
#compiles at once; PHOBOS_TESTS_REQUIRE naming programs fails the run without them, where it would skip their checks;
#SANITIZE: sanitizers, below)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
#the test programs kept in the repository, unless named otherwise (empty: none)
export PSP_TEST_PROGRAMS=${PSP_TEST_PROGRAMS-$ROOT/tests/psp/programs}
#Objects are kept between runs by tests/objects.sh, this checkout's in its own folder, each compiled again when what
#it was built from changes (any header it includes: the core's object is the whole core) or its compile command does.
OUT=${TMPDIR:-/tmp}/phobos-psp-ares-tests
source "$ROOT/tests/objects.sh"

CC=${CC:-cc}
CXX=${CXX:-c++}
SYSROOT=()
#zlib packs the compressed images (../disc-image.hpp, ../disc-formats.hpp)
LIBRARIES=(-lpthread -ldl -lz)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation -lz)
fi
#One build mode for every file: nall's headers pick debug when none is given, and sljit's own C file, without them,
#would then disagree with the rest about SLJIT_DEBUG (sljitConfigPre.h), and so about its compiler's layout.
#ARES_ENABLE_CHD: CHD disc images are read with libchdr, as Phobos's builds read them
DEFINES=(-DBUILD_DEBUG -DCORE_PSP -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1 -DARES_ENABLE_CHD)
#as system headers: ares's aren't written for -Wall, and the test itself is built with it. Vulkan's headers for the
#hardware renderer (ge/gpu), which the core includes; Vulkan itself is opened at run time, only if it's chosen.
CHDR=$ROOT/thirdparty/libchdr
INCLUDES=(-isystem "$ROOT" -isystem "$ROOT/nall" -isystem "$ROOT/nall/nall" -isystem "$ROOT/libco"
  -isystem "$ROOT/ares" -isystem "$ROOT/thirdparty" -isystem "$CHDR/include"
  -isystem "$ROOT/thirdparty/Vulkan-Headers/include")
#FFmpeg's decoders, as Phobos's builds have them (docs/psp-core.md, part 26); PSP_FFMPEG=0 builds without them (their
#define is in the objects' commands, so switching makes objects of their own).
if [[ ${PSP_FFMPEG:-1} != 0 ]]; then
  FFMPEG=$("$ROOT/thirdparty/ffmpeg/build.sh" host | tail -1)
  DEFINES+=(-DARES_ENABLE_FFMPEG)
  INCLUDES+=(-isystem "$FFMPEG/include")
  LIBRARIES+=(-L"$FFMPEG/lib" -lavcodec -lavutil -Wl,-rpath,"$FFMPEG/lib")
fi
#SANITIZE: sanitizers for the C++ objects and the program, as tests/psp/run-tests.sh takes them (none by default;
#-fsanitize=thread for the GE thread's frames, tests/psp/ares/system.cpp's geThread checks)
# shellcheck disable=SC2206
CXXFLAGS=(-std=c++20 -O1 -g "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" ${SANITIZE:-})

object "$CXX" "$ROOT/ares/psp/psp.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/ares/ares/android_globals.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$HERE/ares-runtime.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/nall/nall/nall.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" "${CXXFLAGS[@]}" -w
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I"$ROOT/thirdparty"
#libchdr, and the zlib, LZMA and zstd decoders it carries
for source in "$CHDR"/src/libchdr_*.c "$CHDR/deps/lzma-25.01/src/LzmaDec.c" "$CHDR/deps/miniz-3.1.1/miniz.c" \
  "$CHDR/deps/zstd-1.5.7/zstddeclib.c"; do
  object "$CC" "$source" -O1 -w "${SYSROOT[@]}" -I"$CHDR/include" -I"$CHDR/deps/zstd-1.5.7"
done
object "$CC" "$ROOT/libco/libco.c" -O1 -w "${SYSROOT[@]}" -I"$ROOT/libco"
object "$CXX" "$HERE/system.cpp" "${CXXFLAGS[@]}" -Wall -Wextra -Werror
objects_done

# shellcheck disable=SC2086
objects_program psp-ares ${SANITIZE:-} "${SYSROOT[@]}" "${LIBRARIES[@]}"
# shellcheck disable=SC2086
$CXX ${SANITIZE:-} "${SYSROOT[@]}" -o "$REPLY.part-$$" "${OBJECTS[@]}" "${LIBRARIES[@]}"
mv -f "$REPLY.part-$$" "$REPLY"
"$REPLY"
