#!/usr/bin/env bash
#Builds tools/psp-runner for Android (arm64-v8a), to run on a phone from adb shell without the app: the runner as
#build.sh builds it for the host, compiled as the app's release build compiles the core (the NDK's clang, the app's
#defines, -O3 and ThinLTO: CMakeLists.txt's Android flags, android/app/build.gradle.kts's flavors, the NDK's own
#release flags, its static libc++), with android/measure.hpp put into its frame loop (the emulation thread's CPU
#time and counters, the cores it ran on and their clocks, the app's pinning of that thread; android/measure.hpp).
#
#usage: tools/psp-runner/build-android.sh [REV]
#  REV: a commit to build (anything git names one by: a hash, a tag, origin/master); without one, this checkout as it
#  is. A commit is built in a checkout of its own in the work folder (a clone sharing this repository's objects),
#  detached at it and patched where its tree needs it for this build (android/patches: each one that applies), so a
#  bisect's commits build one after another there, each compiling only what changed since the last (tests/objects.sh,
#  this checkout's, keeps the objects).
#The build is a folder, named by the commit and the flavor, which the last line printed names: psp-runner-android
#(stripped), lib/ (FFmpeg's libavcodec.so and libavutil.so, which the binary finds through its run path,
#$ORIGIN/lib), symbols/psp-runner-android (not stripped, for a profile's symbols) and build.txt (what was built and
#how; its "report" line says whether this runner has --report).
#Environment:
#  ANDROID_NDK_HOME, ANDROID_NDK_ROOT  the NDK (r28; otherwise the newest ndk/28.* of $ANDROID_HOME, $ANDROID_SDK_ROOT
#                       or ~/Library/Android/sdk)
#  PHOBOS_FLAVOR        modern (the default: the Modern APK's -march=armv8.2-a+fp16+dotprod) or legacy (armv8-a+simd)
#  PHOBOS_JOBS          compiles at once, and ThinLTO's jobs at the link (4)
#  PHOBOS_MAX_LOAD      wait before building while the host's load average (1 minute) is above this
#  PHOBOS_ANDROID_WORK  the work folder: the commits' checkout, the objects, FFmpeg's builds
#                       (${TMPDIR:-/tmp}/phobos-psp-runner-android)
#  PHOBOS_ANDROID_OUT   where the builds' folders go (the work folder's builds/)
#  PHOBOS_FFMPEG_CACHE  FFmpeg's builds (the work folder's ffmpeg/); PSP_FFMPEG=0 builds without FFmpeg (the games'
#                       movies then black, their ATRAC music silent)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SELF=$(cd "$HERE/../.." && pwd)
REV=${1:-}
JOBS=${PHOBOS_JOBS:-4}
FLAVOR=${PHOBOS_FLAVOR:-modern}
#the app's minSdk (android/app/build.gradle.kts), the API level its native code is built for
API=26
WORK=${PHOBOS_ANDROID_WORK:-${TMPDIR:-/tmp}/phobos-psp-runner-android}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd -P)
BUILDS=${PHOBOS_ANDROID_OUT:-$WORK/builds}

case $FLAVOR in
  modern) MARCH=-march=armv8.2-a+fp16+dotprod ;;
  legacy) MARCH=-march=armv8-a+simd ;;
  *) echo "PHOBOS_FLAVOR is modern or legacy, not $FLAVOR" >&2; exit 2 ;;
esac

#The NDK: r28, as the app's CI builds with (.github/workflows/android.yml)
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [[ -z $NDK ]]; then
  for SDK in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
    [[ -n $SDK && -d $SDK/ndk ]] || continue
    NDK=$(find "$SDK/ndk" -mindepth 1 -maxdepth 1 -name '28.*' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
    [[ -n $NDK ]] && break
  done
fi
if [[ -z $NDK || ! -f $NDK/source.properties ]]; then
  echo "no NDK r28 found: name it in ANDROID_NDK_HOME" >&2
  exit 1
fi
REVISION=$(sed -n 's/^Pkg.Revision *= *//p' "$NDK/source.properties")
[[ $REVISION == 28.* ]] || echo "warning: NDK $REVISION, not r28 as the app's builds" >&2
case $(uname -s) in
  Darwin) TOOLS=$NDK/toolchains/llvm/prebuilt/darwin-x86_64 ;;
  Linux) TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64 ;;
  *) echo "the NDK's tools are for macOS and Linux" >&2; exit 1 ;;
esac
#(the compilers by every word, as tests/objects.sh takes them, and as CMake calls them for Android)
CC="$TOOLS/bin/clang --target=aarch64-none-linux-android$API --sysroot=$TOOLS/sysroot"
CXX="$TOOLS/bin/clang++ --target=aarch64-none-linux-android$API --sysroot=$TOOLS/sysroot"

#A lock: one build at a time in the work folder's checkout, a lock left by a build that's gone taken over.
lock() {  #folder
  local owner
  while ! mkdir "$1" 2> /dev/null; do
    owner=$(cat "$1/owner" 2> /dev/null || true)
    if [[ -n $owner ]] && ! kill -0 "$owner" 2> /dev/null; then
      echo "taking over $1 from process $owner, which has gone" >&2
      rm -rf "$1"
      continue
    fi
    sleep 2
  done
  echo $$ > "$1/owner"
  LOCKED=$1
}
LOCKED=
trap '[[ -z $LOCKED ]] || rm -rf "$LOCKED"' EXIT

if [[ -n ${PHOBOS_MAX_LOAD:-} ]]; then
  while true; do
    if [[ -r /proc/loadavg ]]; then LOAD=$(cut -d' ' -f1 /proc/loadavg)
    else LOAD=$(sysctl -n vm.loadavg | awk '{print $2}'); fi
    awk -v load="$LOAD" -v most="$PHOBOS_MAX_LOAD" 'BEGIN { exit !(load > most) }' || break
    echo "the load average is $LOAD (above $PHOBOS_MAX_LOAD): waiting" >&2
    sleep 30
  done
fi

#The tree: this checkout, or the commit asked for in the work folder's checkout (a clone sharing this repository's
#objects: nothing is written to this repository)
PATCHED=()
if [[ -n $REV ]]; then
  if ! COMMIT=$(git -C "$SELF" rev-parse --verify --quiet "$REV^{commit}"); then
    echo "no such commit here: $REV" >&2
    exit 1
  fi
  ROOT=$WORK/checkout
  lock "$WORK/checkout.lock"
  if ! git -C "$ROOT" rev-parse --git-dir > /dev/null 2>&1 ||
     ! git -C "$ROOT" cat-file -e "$COMMIT^{commit}" 2> /dev/null; then
    rm -rf "$ROOT"
    git clone -q --shared --no-checkout "$(git -C "$SELF" rev-parse --path-format=absolute --git-common-dir)" "$ROOT"
  fi
  #(the last build's patches undone; files git changes get new times, so their objects are compiled again)
  git -C "$ROOT" reset -q --hard
  git -C "$ROOT" clean -q -fd
  git -C "$ROOT" -c advice.detachedHead=false checkout -q --detach "$COMMIT"
  for patch in "$HERE"/android/patches/*.patch; do
    [[ -f $patch ]] || continue
    if git -C "$ROOT" apply --check "$patch" 2> /dev/null; then
      git -C "$ROOT" apply "$patch"
      PATCHED+=("$(basename "$patch")")
    fi
  done
else
  ROOT=$SELF
  COMMIT=$(git -C "$ROOT" rev-parse --verify --quiet HEAD || echo unknown)
fi
SOURCE=$(git -C "$ROOT" describe --tags --match 'v[0-9]*' --always --abbrev=12 "$COMMIT" 2> /dev/null || echo unknown)
NAME=${COMMIT:0:9}
if [[ -z $REV && -n $(GIT_OPTIONAL_LOCKS=0 git -C "$ROOT" status --porcelain --untracked-files=no 2> /dev/null) ]]
then
  SOURCE+=-dirty
  NAME+=-dirty
fi
((${#PATCHED[@]} == 0)) || SOURCE+=+android-patches
SOURCE=${SOURCE//[^A-Za-z0-9._+-]/_}
NAME+=-$FLAVOR
echo "building $SOURCE ($FLAVOR) for Android in $ROOT" >&2

#FFmpeg's decoders, for arm64-v8a at the app's API level, as the app's CMake builds them (its build's own make takes
#every core the host has: here it's told PHOBOS_JOBS, through getconf, which it asks)
FFMPEG=
if [[ ${PSP_FFMPEG:-1} != 0 && -f $ROOT/thirdparty/ffmpeg/build.sh ]]; then
  export PHOBOS_FFMPEG_CACHE=${PHOBOS_FFMPEG_CACHE:-$WORK/ffmpeg}
  SHIM=$WORK/getconf-shim
  mkdir -p "$SHIM"
  REAL_GETCONF=$(command -v getconf)
  printf '#!/bin/sh\nif [ "$1" = _NPROCESSORS_ONLN ]; then echo %s; else exec %s "$@"; fi\n' "$JOBS" "$REAL_GETCONF" \
    > "$SHIM/getconf"
  chmod +x "$SHIM/getconf"
  FFMPEG=$(PATH="$SHIM:$PATH" bash "$ROOT/thirdparty/ffmpeg/build.sh" android "$NDK" "$API" | tail -1)
fi

#The app's flags (CMakeLists.txt, android/app/build.gradle.kts, the NDK's android-legacy.toolchain.cmake, which the
#NDK's CMake toolchain takes by default), in the order CMake gives them (the app's compile_commands.json): the NDK's;
#the flavor's (Gradle's cFlags and cppFlags); the release build's; then position-independent code (ANDROID_PIE),
#CMakeLists.txt's Android options and phobos_core's hidden symbols, which are the compiles' alone.
NDK_FLAGS=(-g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong
  -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security)
RELEASE_FLAGS=(-O3 -DNDEBUG)
APP_FLAGS=(-fPIC -w -O3 -flto=thin -ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math
  -fvisibility=hidden -fvisibility-inlines-hidden)
#(CMakeLists.txt's PHOBOS_DEFINITIONS, but for the one core built here, and Android's SLJIT_CONFIG_ARM_64)
DEFINES=(-DBUILD_RELEASE -DVULKAN -DVK_NO_PROTOTYPES -DMIA_LIBRARY -DSLJIT_HAVE_CONFIG_PRE=1 -DSLJIT_HAVE_CONFIG_POST=1
  -DSLJIT_DEBUG=0 -DARES_ENABLE_ZIP -DARES_ENABLE_CHD -DCORE_PSP -DPHOBOS_TAPE_SPEED -DPROFILE_PERFORMANCE
  -DSLJIT_CONFIG_ARM_64=1)
CHDR=$ROOT/thirdparty/libchdr
INCLUDES=(-isystem "$ROOT" -isystem "$ROOT/nall" -isystem "$ROOT/nall/nall" -isystem "$ROOT/libco"
  -isystem "$ROOT/ares" -isystem "$ROOT/thirdparty" -isystem "$CHDR/include"
  -isystem "$ROOT/thirdparty/Vulkan-Headers/include")
LIBRARIES=()
if [[ -n $FFMPEG ]]; then
  DEFINES+=(-DARES_ENABLE_FFMPEG)
  INCLUDES+=(-isystem "$FFMPEG/include")
  LIBRARIES+=(-L"$FFMPEG/lib" -lavcodec -lavutil)
fi
#zlib (the disc images, the runner's PNGs), the log (the core's notes), the window functions the core refers to,
#and dlopen for Vulkan's loader: all the system's own
LIBRARIES+=(-lz -llog -landroid -ldl -lm)
CFLAGS=("${DEFINES[@]}" "${INCLUDES[@]}" "${NDK_FLAGS[@]}" "$MARCH" "${RELEASE_FLAGS[@]}" "${APP_FLAGS[@]}")
CXXFLAGS=("${DEFINES[@]}" "${INCLUDES[@]}" "${NDK_FLAGS[@]}" -std=c++20 "$MARCH" "${RELEASE_FLAGS[@]}"
  "${APP_FLAGS[@]}")
#(the ares sources' prefix header, as CMakeLists.txt gives ARES_ALL_SOURCES)
ARES_HEADER=(-include "$ROOT/ares/ares/ares.hpp")

#The runner, with the measurements' three calls put into its frame loop at the lines every runner since v2.2.0 has:
#begin() before the loop's clock starts, frame() after each frame's run, end() after the loop's clock stops. Written
#only when it changes, so its object is kept otherwise.
GENERATED=$WORK/generated/$(printf %s "$ROOT" | cksum | cut -d' ' -f1)
mkdir -p "$GENERATED"
RUNNER=$GENERATED/runner.cpp
if grep -q 'phobos_measure::' "$ROOT/tools/psp-runner/runner.cpp"; then
  cp "$ROOT/tools/psp-runner/runner.cpp" "$RUNNER.part-$$"
else
  if ! awk '
    /^  auto start = std::chrono::steady_clock::now\(\);$/ { print "  phobos_measure::begin();"; begins++ }
    { print }
    /^    root->run\(\);$/ { print "    phobos_measure::frame();"; frames++ }
    /^  auto end = std::chrono::steady_clock::now\(\);/ { print "  phobos_measure::end();"; ends++ }
    END { exit !(begins == 1 && frames == 1 && ends == 1) }
  ' "$ROOT/tools/psp-runner/runner.cpp" > "$RUNNER.part-$$"; then
    rm -f "$RUNNER.part-$$"
    echo "tools/psp-runner/runner.cpp hasn't the frame loop's lines the measurements go at (its clock's start and" \
         "end, root->run()): build-android.sh needs them, once each" >&2
    exit 1
  fi
fi
if cmp -s "$RUNNER.part-$$" "$RUNNER"; then rm -f "$RUNNER.part-$$"; else mv -f "$RUNNER.part-$$" "$RUNNER"; fi
REPORT=no
grep -q '"--report"' "$RUNNER" && REPORT=yes

OUT=$WORK/objects
PHOBOS_JOBS=$JOBS
source "$SELF/tests/objects.sh"
object "$CXX" "$ROOT/ares/psp/psp.cpp" "${CXXFLAGS[@]}" "${ARES_HEADER[@]}"
object "$CXX" "$ROOT/ares/ares/android_globals.cpp" "${CXXFLAGS[@]}" "${ARES_HEADER[@]}"
object "$CXX" "$ROOT/tests/psp/ares/ares-runtime.cpp" "${CXXFLAGS[@]}" "${ARES_HEADER[@]}"
object "$CXX" "$ROOT/nall/nall/nall.cpp" "${CXXFLAGS[@]}"
object "$CXX" "$ROOT/thirdparty/sljitAllocator.cpp" "${CXXFLAGS[@]}"
object "$CC" "$ROOT/thirdparty/sljit/sljit_src/sljitLir.c" "${CFLAGS[@]}"
#libchdr, and the zlib, LZMA and zstd decoders it carries (by whichever versions the tree has)
ZSTD=$(find "$CHDR/deps" -maxdepth 1 -type d -name 'zstd-*' | head -1)
for source in "$CHDR"/src/libchdr_*.c "$CHDR"/deps/lzma-*/src/LzmaDec.c "$CHDR"/deps/miniz-*/miniz.c \
  "$ZSTD/zstddeclib.c"; do
  object "$CC" "$source" "${CFLAGS[@]}" -I"$CHDR/include" -I"$ZSTD"
done
object "$CC" "$ROOT/libco/libco.c" "${CFLAGS[@]}"
object "$CXX" "$RUNNER" "${CXXFLAGS[@]}" -include "$HERE/android/measure.hpp" \
  "-DPHOBOS_RUNNER_SOURCE=\"$SOURCE\"" "-DPHOBOS_MEASURE_SOURCE=\"$SOURCE\"" \
  "-DPHOBOS_MEASURE_BUILD=\"android $FLAVOR, NDK $REVISION\""
objects_done

#The link, as the app's (its build.ninja's: the language's flags, then the NDK's for API 26, -static-libstdc++ for its
#default c++_static, --gc-sections for release, and CMakeLists.txt's ThinLTO backend at -O3), into a
#position-independent executable that finds FFmpeg's libraries beside it
LINK=("${NDK_FLAGS[@]}" -std=c++20 "$MARCH" "${RELEASE_FLAGS[@]}" -static-libstdc++ -Wl,--build-id=sha1
  -Wl,--no-rosegment -Wl,--no-undefined-version -Wl,--fatal-warnings -Wl,--no-undefined -Qunused-arguments
  -Wl,--gc-sections -flto=thin -O3 -pie -Wl,--thinlto-jobs="$JOBS" "-Wl,-rpath,\$ORIGIN/lib")
mkdir -p "$WORK/thinlto-cache"
objects_program psp-runner-android "${LINK[@]}" "${LIBRARIES[@]}"
echo "  linking (ThinLTO, $JOBS jobs)" >&2
$CXX "${LINK[@]}" "-Wl,--thinlto-cache-dir=$WORK/thinlto-cache" -o "$REPLY.part-$$" "${OBJECTS[@]}" "${LIBRARIES[@]}"
mv -f "$REPLY.part-$$" "$REPLY"

#The build's folder: put together beside its place and renamed into it, so a folder there is a whole one
mkdir -p "$BUILDS"
FOLDER=$BUILDS/$NAME
PART=$FOLDER.part-$$
rm -rf "$PART"
mkdir -p "$PART/symbols" "$PART/lib"
cp "$REPLY" "$PART/symbols/psp-runner-android"
"$TOOLS/bin/llvm-strip" --strip-all -o "$PART/psp-runner-android" "$REPLY"
if [[ -n $FFMPEG ]]; then
  for library in libavcodec.so libavutil.so; do
    "$TOOLS/bin/llvm-strip" --strip-unneeded -o "$PART/lib/$library" "$FFMPEG/lib/$library"
  done
fi
sha() {
  if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi
}
{
  echo "source: $SOURCE"
  echo "commit: $COMMIT"
  echo "flavor: $FLAVOR ($MARCH)"
  echo "ndk: $REVISION ($("$TOOLS/bin/clang" --version | head -1))"
  echo "api: $API"
  echo "report: $REPORT"
  echo "ffmpeg: ${FFMPEG:+$(basename "$FFMPEG")}"
  echo "patches: ${PATCHED[*]:-}"
  echo "measure: $(sha "$HERE/android/measure.hpp")"
  echo "built: $(date -u '+%Y-%m-%dT%H:%M:%SZ') on $(uname -sm)," \
    "by $(git -C "$SELF" describe --always --dirty 2> /dev/null || echo '?')'s build-android.sh"
  echo "sha256: $(sha "$PART/psp-runner-android")"
  echo "compile (C++): $CXX ${CXXFLAGS[*]}"
  echo "link: $CXX ${LINK[*]} ${LIBRARIES[*]}"
} > "$PART/build.txt"
rm -rf "$FOLDER"
mv "$PART" "$FOLDER"
echo "$FOLDER"
