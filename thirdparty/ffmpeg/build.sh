#!/usr/bin/env bash
#Builds FFmpeg's decoders for the PSP's music and movies (docs/psp-core.md, parts 26 and 27): ATRAC3, ATRAC3plus, MP3
#and H.264, as two shared libraries, libavcodec and libavutil, from FFmpeg's official release, unmodified.
#
#Licensing: FFmpeg is LGPL 2.1 or later, so long as nothing GPL or non-free goes in, and this enables nothing but the
#decoders below (no GPL, no non-free, no version 3 upgrade, no external libraries: --disable-autodetect). Phobos links
#the two as shared libraries, which its packages carry beside it (the APK's lib/arm64-v8a, the AppImage's usr/lib,
#Phobos.app's Contents/Frameworks, the folder of Phobos.exe), so anyone can build their own from this script, changed
#as they like, and swap them in: the LGPL's terms for a program that uses a library. The source is the release's own
#tarball, checked against its SHA-256 (the same as Homebrew's formula for this release records); the notice is in the
#repository's LICENSE (Settings, About, Open-source licenses in the app; LICENSE in each desktop package).
#
#usage: thirdparty/ffmpeg/build.sh host                   (the host's own: the tests and runners, and the desktop
#                                                          program on Linux, and on Windows under MSYS2)
#       thirdparty/ffmpeg/build.sh macos ARCH...          (the desktop program on macOS: arm64, x86_64 or both, each
#                                                          built alone, then combined by lipo; for macOS 11 and later,
#                                                          or $MACOSX_DEPLOYMENT_TARGET)
#       thirdparty/ffmpeg/build.sh windows TOOL-PREFIX    (the desktop program for Windows, built on Linux with
#                                                          MinGW-w64's tools: x86_64-w64-mingw32-, and a C
#                                                          compiler for Linux itself, which configure builds with)
#       thirdparty/ffmpeg/build.sh android NDK API-LEVEL  (arm64-v8a, with that NDK's clang, as the app's CMake does)
#       thirdparty/ffmpeg/build.sh --name TARGET ...      (only print the build's name: CI's cache key)
#The last line printed is the folder the build was installed in (lib/, include/, and bin/ with Windows's DLLs), as the
#host names it (D:/... under MSYS2, for CMake); the build's own output goes to a log beside it. Builds are kept in
#$PHOBOS_FFMPEG_CACHE (else .cache/ffmpeg in the repository), each under a name hashed from this script and the
#compiler, so a build is made once and made again only when one of those changes. It needs bash, make, curl and xz
#(tar -J), and the network the first time, for the release's tarball. Offline, put the tarball in the cache folder (or
#name it in $PHOBOS_FFMPEG_TARBALL) and it isn't downloaded.
set -euo pipefail

VERSION=9.0.2
SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
URL=https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz
#Decoders: atrac3 (ATRAC3), atrac3p (ATRAC3plus), mp3float (MP3) and h264: what the libraries open. No parsers,
#demuxers or other libraries: Phobos takes the PSP's containers apart itself, and converts samples and pixels itself.
CONFIGURE=(
  --disable-gpl --disable-nonfree --disable-version3
  --disable-programs --disable-doc --disable-everything --disable-autodetect --disable-network
  --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample
  --enable-shared --disable-static --enable-pic
  --enable-decoder=atrac3,atrac3p,mp3float,h264
)

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CACHE=${PHOBOS_FFMPEG_CACHE:-$ROOT/.cache/ffmpeg}
NAME_ONLY=
if [[ ${1:-} == --name ]]; then NAME_ONLY=1; shift; fi
TARGET=${1:-}
mkdir -p "$CACHE"

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
usage() {
  echo "usage: $0 [--name] host | macos ARCH... | windows TOOL-PREFIX | android NDK API-LEVEL" >&2
  exit 2
}

COMBINE=
case $TARGET in
host)
  if [[ -z ${CC:-} ]]; then
    CC=cc
    if ! command -v cc >/dev/null; then CC=gcc; fi
  fi
  IDENTITY="host $(uname -sm) $($CC --version 2>&1 | head -1)"
  CROSS=(--cc="$CC")
  #x86's assembly needs nasm; without it the C versions are used (slower, the same output)
  if [[ $(uname -m) == x86_64 ]] && ! command -v nasm >/dev/null; then
    CROSS+=(--disable-x86asm)
    IDENTITY+=" no nasm"
  fi
  #Windows under MSYS2: FFmpeg's configure knows MINGW64's name for the system but not UCRT64's or CLANG64's, so it's
  #told; and the compiler's own libraries (libgcc, and the winpthreads it needs) are linked into the DLLs statically,
  #as into Phobos.exe, so their only other needs are Windows's own
  case $(uname -s) in MINGW*|UCRT*|CLANG*|MSYS*) CROSS+=(--target-os=mingw32 --extra-ldflags=-static);; esac
  ;;
macos)
  shift
  (($#)) || usage
  #the desktop program's oldest macOS (CMakePresets.json's deployment target, Phobos.app's LSMinimumSystemVersion)
  MINIMUM=${MACOSX_DEPLOYMENT_TARGET:-11.0}
  CC=${CC:-cc}
  ARCHS="$*"
  if (($# > 1)); then
    #each architecture's build, made alone (below) and named in this one's, so a change to any makes this anew
    COMBINE=1
    IDENTITY="macos $ARCHS:"
    for ARCH in $ARCHS; do IDENTITY+=" $(bash "$0" --name macos "$ARCH")"; done
  else
    #built for the architecture whichever the Mac is (clang builds for both), and named, as dyld finds it, by the
    #program's run path (@rpath/libavcodec.63.dylib): Phobos.app's Frameworks, or the build's own folder
    IDENTITY="macos $ARCHS $MINIMUM $($CC --version 2>&1 | head -1)"
    CROSS=(--enable-cross-compile --target-os=darwin --arch="$ARCHS"
      --cc="$CC -arch $ARCHS -mmacosx-version-min=$MINIMUM" --install-name-dir=@rpath)
    if [[ $ARCHS == x86_64 ]] && ! command -v nasm >/dev/null; then
      CROSS+=(--disable-x86asm)
      IDENTITY+=" no nasm"
    fi
  fi
  TARGET=macos-${ARCHS// /-}
  ;;
windows)
  TOOLS=${2:?"the MinGW-w64 tools' prefix (x86_64-w64-mingw32-)"}
  IDENTITY="windows $TOOLS $(${TOOLS}gcc --version 2>&1 | head -1)"
  CROSS=(--enable-cross-compile --target-os=mingw32 --arch="${TOOLS%%-*}" --cross-prefix="$TOOLS"
    --extra-ldflags=-static)
  if ! command -v nasm >/dev/null; then
    CROSS+=(--disable-x86asm)
    IDENTITY+=" no nasm"
  fi
  ;;
android)
  NDK=${2:?the NDK folder}
  API=${3:?the Android API level}
  case $(uname -s) in Darwin) HOST=darwin-x86_64;; *) HOST=linux-x86_64;; esac
  TOOLS=$NDK/toolchains/llvm/prebuilt/$HOST
  REVISION=$(sed -n 's/^Pkg.Revision *= *//p' "$NDK/source.properties" 2>/dev/null || true)
  IDENTITY="android arm64-v8a api $API ndk $REVISION"
  #arm64-v8a as the app builds it: the API level of its minSdk, and 16 KiB pages (Android 15 on allows them)
  CROSS=(--enable-cross-compile --target-os=android --arch=aarch64 --sysroot="$TOOLS/sysroot"
    --cc="$TOOLS/bin/aarch64-linux-android$API-clang" --ar="$TOOLS/bin/llvm-ar" --nm="$TOOLS/bin/llvm-nm"
    --ranlib="$TOOLS/bin/llvm-ranlib" --strip="$TOOLS/bin/llvm-strip"
    --extra-ldflags="-Wl,-z,max-page-size=16384")
  ;;
*)
  usage
  ;;
esac

KEY=$( (cat "$0"; echo "$IDENTITY") | sha256 | cut -c1-12)
NAME=$TARGET-$VERSION-$KEY
if [[ -n $NAME_ONLY ]]; then echo "$NAME"; exit 0; fi
PREFIX=$CACHE/$NAME
LOG=$CACHE/$NAME.log

#Two builds of the app (its flavors) may configure at once: one builds, the other waits for it. The lock holds its
#build's process ID and host; a lock whose process is gone (a build killed part way) is taken over. Taking over is
#marked by a folder made in that lock (mkdir: of builds trying at once, one makes it), and the one that made it looks
#once more that the lock is still the dead build's: a lock made afresh meanwhile (one that took over first, whose
#owner is alive or not yet written) is left alone, its mark removed.
LOCK=$CACHE/$NAME.lock
DOWNLOAD=
cleanup() { rm -rf "$LOCK"; if [[ -n $DOWNLOAD ]]; then rm -f "$DOWNLOAD"; fi; }
for ((wait = 0; ; wait++)); do
  if mkdir "$LOCK" 2>/dev/null; then
    echo "$$ $(uname -n)" > "$LOCK/owner"
    break
  fi
  OWNER=$(cat "$LOCK/owner" 2>/dev/null || true)
  gone() { [[ -n $1 && ${1#* } == "$(uname -n)" ]] && ! ps -p "${1%% *}" >/dev/null 2>&1; }
  if gone "$OWNER" && mkdir "$LOCK/takeover" 2>/dev/null; then
    if [[ $(cat "$LOCK/owner" 2>/dev/null || true) == "$OWNER" ]] && gone "$OWNER"; then
      echo "FFmpeg: taking over $LOCK from process ${OWNER%% *}, which has gone" >&2
      rm -rf "$LOCK"
    else
      rmdir "$LOCK/takeover" 2>/dev/null || true
    fi
    continue
  fi
  if ((wait > 1800)); then
    echo "FFmpeg: $LOCK has been held for 30 minutes (by ${OWNER:-no process recorded}); remove it if no build" \
         "is running" >&2
    exit 1
  fi
  sleep 1
done
trap cleanup EXIT

#The release's tarball, checked before it's used: downloaded into a file of its own, and kept only if it's the
#release's (a proxy's or captive portal's page is thrown away, and downloaded once more).
matches() { [[ $(sha256 "$1" | cut -d' ' -f1) == "$SHA256" ]]; }
if [[ ! -f $PREFIX/.built && -n $COMBINE ]]; then
  #Several macOS architectures: each one's build (made now if it isn't yet), their libraries put together into
  #universal ones, file by file (the links between their names kept); the headers are the same for both.
  rm -rf "$PREFIX"
  mkdir -p "$PREFIX/lib"
  for ARCH in $ARCHS; do bash "$0" macos "$ARCH" >/dev/null; done
  FIRST=$CACHE/$(bash "$0" --name macos "${ARCHS%% *}")
  cp -R "$FIRST/include" "$PREFIX/"
  for LIBRARY in "$FIRST"/lib/*.dylib; do
    FILE=$(basename "$LIBRARY")
    if [[ -L $LIBRARY ]]; then
      cp -P "$LIBRARY" "$PREFIX/lib/$FILE"
      continue
    fi
    set --
    for ARCH in $ARCHS; do set -- "$@" "$CACHE/$(bash "$0" --name macos "$ARCH")/lib/$FILE"; done
    lipo -create "$@" -output "$PREFIX/lib/$FILE"
  done
  touch "$PREFIX/.built"
elif [[ ! -f $PREFIX/.built ]]; then
  TARBALL=${PHOBOS_FFMPEG_TARBALL:-$CACHE/ffmpeg-$VERSION.tar.xz}
  if [[ -f $TARBALL ]] && ! matches "$TARBALL"; then
    if [[ -n ${PHOBOS_FFMPEG_TARBALL:-} ]]; then
      echo "FFmpeg: $TARBALL (PHOBOS_FFMPEG_TARBALL) isn't FFmpeg $VERSION's release: its SHA-256 isn't $SHA256" >&2
      exit 1
    fi
    echo "FFmpeg: $TARBALL isn't FFmpeg $VERSION's release; downloading it again" >&2
    rm -f "$TARBALL"
  fi
  for ((attempt = 1; ; attempt++)); do
    if [[ -f $TARBALL ]]; then break; fi
    DOWNLOAD=$(mktemp "$CACHE/ffmpeg-$VERSION.download.XXXXXX")
    echo "FFmpeg: downloading $URL" >&2
    if curl -fsSL --retry 3 -o "$DOWNLOAD" "$URL" && matches "$DOWNLOAD"; then
      mv "$DOWNLOAD" "$TARBALL"
      DOWNLOAD=
      break
    fi
    rm -f "$DOWNLOAD"
    DOWNLOAD=
    if ((attempt == 2)); then
      echo "FFmpeg: couldn't download FFmpeg $VERSION's release from $URL (no network, or what came wasn't it)." >&2
      echo "  Download it some other way, check its SHA-256 is $SHA256, and put it at $TARBALL" >&2
      echo "  (or name it in PHOBOS_FFMPEG_TARBALL); or build without FFmpeg: -Pphobos.ffmpeg=OFF for Gradle," >&2
      echo "  -DPHOBOS_FFMPEG=OFF for CMake, PSP_FFMPEG=0 for the PSP tests." >&2
      exit 1
    fi
    echo "FFmpeg: the download wasn't FFmpeg $VERSION's release (its SHA-256 isn't $SHA256); trying once more" >&2
  done
  SOURCE=$CACHE/$NAME.source
  rm -rf "$SOURCE" "$PREFIX"
  mkdir -p "$SOURCE"
  tar -xJf "$TARBALL" -C "$SOURCE" --strip-components=1
  echo "FFmpeg: building $VERSION for $IDENTITY (a few minutes, once; log: $LOG)" >&2
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
  if ! (cd "$SOURCE" && ./configure --prefix="$PREFIX" "${CROSS[@]}" "${CONFIGURE[@]}" &&
        make -j"$JOBS" && make install) >"$LOG" 2>&1; then
    tail -40 "$LOG" >&2
    echo "FFmpeg: the build failed; the whole log is $LOG" >&2
    exit 1
  fi
  rm -rf "$SOURCE"
  touch "$PREFIX/.built"
fi
if command -v cygpath >/dev/null; then cygpath -m "$PREFIX"; else echo "$PREFIX"; fi
