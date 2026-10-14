#!/usr/bin/env bash
#Builds tools/psp-runner, the headless PSP game runner: the PSP core as an ares system (ares/psp/psp.cpp, the
#whole core as Phobos builds it, with ares's node tree) with a command-line front end (runner.cpp) that boots a
#game, runs it a frame at a time, and reports what happens. Its objects are kept between builds outside the
#repository by tests/objects.sh, this checkout's in its own folder, each compiled again when what it was built from
#changes (any header it includes) or its compile command does.
#usage: tools/psp-runner/build.sh   (the binary ends up in this checkout's build folder, named psp-runner and a hash
#of its configuration, which the last line names; a copy goes in ${TMPDIR:-/tmp}/phobos-psp-runner/psp-runner, the
#last one built there.
#PHOBOS_JOBS: compiles at once)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/phobos-psp-runner
source "$ROOT/tests/objects.sh"

CC=${CC:-cc}
CXX=${CXX:-c++}
SYSROOT=()
#zlib packs the compressed disc images (ares/psp/kernel/disc.cpp) and the runner's PNGs
LIBRARIES=(-lpthread -ldl -lz)
if [[ $(uname) == Darwin ]]; then
  if SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then SYSROOT=(-isysroot "$SDK"); fi
  LIBRARIES=(-framework CoreFoundation -lz)
fi
#One build mode for every file: nall's headers pick debug when none is given, and sljit's own C file, without
#them, would then disagree with the rest about SLJIT_DEBUG (sljitConfigPre.h), and so about its compiler's
#layout. ARES_ENABLE_CHD: CHD disc images are read with libchdr, as Phobos's builds read them.
DEFINES=(-DBUILD_DEBUG -DCORE_PSP -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1 -DARES_ENABLE_CHD)
#as system headers: ares's aren't written for -Wall, and the runner itself is built with it. Vulkan's headers for
#the hardware renderer (ge/gpu), which the core includes; Vulkan itself is opened at run time, only if it's chosen.
CHDR=$ROOT/thirdparty/libchdr
INCLUDES=(-isystem "$ROOT" -isystem "$ROOT/nall" -isystem "$ROOT/nall/nall" -isystem "$ROOT/libco"
  -isystem "$ROOT/ares" -isystem "$ROOT/thirdparty" -isystem "$CHDR/include"
  -isystem "$ROOT/thirdparty/Vulkan-Headers/include")
#FFmpeg's decoders for the games' music and movies, as Phobos's builds have them (docs/psp-core.md, part 26),
#built once for the host by thirdparty/ffmpeg/build.sh and kept; PSP_FFMPEG=0 builds without them (the games'
#movies then stay black and their ATRAC music silent). Their define is in the objects' commands, so switching
#makes objects of their own.
if [[ ${PSP_FFMPEG:-1} != 0 ]]; then
  FFMPEG=$("$ROOT/thirdparty/ffmpeg/build.sh" host | tail -1)
  DEFINES+=(-DARES_ENABLE_FFMPEG)
  INCLUDES+=(-isystem "$FFMPEG/include")
  LIBRARIES+=(-L"$FFMPEG/lib" -lavcodec -lavutil -Wl,-rpath,"$FFMPEG/lib")
fi
CXXFLAGS=(-std=c++20 -O1 -g "${SYSROOT[@]}" "${DEFINES[@]}" "${INCLUDES[@]}")

object "$CXX" "$ROOT/ares/psp/psp.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/ares/ares/android_globals.cpp" "${CXXFLAGS[@]}" -w
#the parts of ares's framework the core needs beside its own code: the node tree, and the debugger it refers to
object "$CXX" "$ROOT/tests/psp/ares/ares-runtime.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/nall/nall/nall.cpp" "${CXXFLAGS[@]}" -w
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" "${CXXFLAGS[@]}" -w
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" -O1 "${SYSROOT[@]}" "${DEFINES[@]}" -I"$ROOT/thirdparty"
#libchdr, and the zlib, LZMA and zstd decoders it carries
for source in "$CHDR"/src/libchdr_*.c "$CHDR/deps/lzma-25.01/src/LzmaDec.c" "$CHDR/deps/miniz-3.1.1/miniz.c" \
  "$CHDR/deps/zstd-1.5.7/zstddeclib.c"; do
  object "$CC" "$source" -O1 -w "${SYSROOT[@]}" -I"$CHDR/include" -I"$CHDR/deps/zstd-1.5.7"
done
object "$CC" "$ROOT/libco/libco.c" -O1 -w "${SYSROOT[@]}" -I"$ROOT/libco"
object "$CXX" "$HERE/runner.cpp" "${CXXFLAGS[@]}" -Wall -Wextra -Werror
objects_done

objects_program psp-runner "${SYSROOT[@]}" "${LIBRARIES[@]}"
$CXX "${SYSROOT[@]}" -o "$REPLY.part-$$" "${OBJECTS[@]}" "${LIBRARIES[@]}"
mv -f "$REPLY.part-$$" "$REPLY"
cp "$REPLY" "$OUT/psp-runner.part-$$"
mv -f "$OUT/psp-runner.part-$$" "$OUT/psp-runner"
echo "built $REPLY"
