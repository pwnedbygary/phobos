#!/usr/bin/env bash
#Builds FFmpeg's decoders for the PSP's music and movies (docs/psp-core.md, part 26): ATRAC3, ATRAC3plus, MP3, AAC
#and H.264, as two shared libraries, libavcodec and libavutil, from FFmpeg's official release, unmodified.
#
#Licensing: FFmpeg is LGPL 2.1 or later, so long as nothing GPL or non-free goes in, and this enables nothing but the
#decoders below (no GPL, no non-free, no version 3 upgrade, no external libraries: --disable-autodetect). Phobos's
#native library links the two as shared libraries, which the APK carries beside it (lib/arm64-v8a/libavcodec.so,
#libavutil.so), so anyone can build their own from this script, changed as they like, and swap them in: the LGPL's
#terms for a program that uses a library. The source is the release's own tarball, checked against its SHA-256 (the
#same as Homebrew's formula for this release records); the notice is in the repository's LICENSE (Settings, About,
#Open-source licenses in the app).
#
#usage: thirdparty/ffmpeg/build.sh host                   (for the host tests and runners: macOS or Linux)
#       thirdparty/ffmpeg/build.sh android NDK API-LEVEL  (arm64-v8a, with that NDK's clang, as the app's CMake does)
#The last line printed is the folder the build was installed in (lib/, include/); the build's own output goes to a
#log beside it. Builds are kept in $PHOBOS_FFMPEG_CACHE (else .cache/ffmpeg in the repository), each under a name
#hashed from this script and the compiler, so a build is made once and made again only when one of those changes.
#Offline, put the tarball in the cache folder (or name it in $PHOBOS_FFMPEG_TARBALL) and it isn't downloaded.
set -euo pipefail

VERSION=9.0.2
SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
URL=https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz
#Decoders: atrac3 and atrac3al (ATRAC3 and its "AL" variant), atrac3p and atrac3pal (ATRAC3plus), mp3float (MP3),
#aac and h264. No parsers, demuxers or other libraries: Phobos takes the PSP's containers apart itself, and converts
#samples and pixels itself.
CONFIGURE=(
  --disable-gpl --disable-nonfree --disable-version3
  --disable-programs --disable-doc --disable-everything --disable-autodetect --disable-network
  --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample
  --enable-shared --disable-static --enable-pic
  --enable-decoder=atrac3,atrac3al,atrac3p,atrac3pal,mp3float,aac,h264
)

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CACHE=${PHOBOS_FFMPEG_CACHE:-$ROOT/.cache/ffmpeg}
TARGET=${1:-}
mkdir -p "$CACHE"

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }

case $TARGET in
host)
  CC=${CC:-cc}
  IDENTITY="host $(uname -sm) $($CC --version 2>&1 | head -1)"
  CROSS=(--cc="$CC")
  #x86's assembly needs nasm; without it the C versions are used (slower, the same output)
  if [[ $(uname -m) == x86_64 ]] && ! command -v nasm >/dev/null; then CROSS+=(--disable-x86asm); fi
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
  echo "usage: $0 host | android NDK API-LEVEL" >&2
  exit 2
  ;;
esac

KEY=$( (cat "$0"; echo "$IDENTITY") | sha256 | cut -c1-12)
NAME=$TARGET-$VERSION-$KEY
PREFIX=$CACHE/$NAME
LOG=$CACHE/$NAME.log

#Two builds of the app (its flavors) may configure at once: one builds, the other waits for it.
LOCK=$CACHE/$NAME.lock
for ((wait = 0; ; wait++)); do
  if mkdir "$LOCK" 2>/dev/null; then break; fi
  if ((wait > 1800)); then
    echo "FFmpeg: $LOCK has been held for 30 minutes; remove it if no build is running" >&2
    exit 1
  fi
  sleep 1
done
trap 'rm -rf "$LOCK"' EXIT

if [[ ! -f $PREFIX/.built ]]; then
  TARBALL=${PHOBOS_FFMPEG_TARBALL:-$CACHE/ffmpeg-$VERSION.tar.xz}
  if [[ ! -f $TARBALL ]]; then
    echo "FFmpeg: downloading $URL" >&2
    curl -fsSL --retry 3 -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  if [[ $(sha256 "$TARBALL" | cut -d' ' -f1) != "$SHA256" ]]; then
    echo "FFmpeg: $TARBALL isn't FFmpeg $VERSION's release (its SHA-256 differs from $SHA256)" >&2
    exit 1
  fi
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
echo "$PREFIX"
