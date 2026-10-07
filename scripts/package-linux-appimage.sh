#!/usr/bin/env bash
# Packages the Linux x86-64 build as dist/Phobos-<version>-x86_64.AppImage plus its .zsync, the
# file AppImageUpdate reads to fetch only the changed blocks of a newer release.
#   scripts/package-linux-appimage.sh [build directory]
# GPU drivers stay on the host: Phobos opens the system's libvulkan.so.1 at run time. FFmpeg's
# libavcodec and libavutil (the PSP's music and movies), in a build with them, go in usr/lib.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/linux-x64}" && pwd)"
VERSION="$(git -C "$ROOT" describe --tags --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)"
VERSION="${VERSION:-0.0.0-dev}"
REPOSITORY="${PHOBOS_REPOSITORY:-pwnedbygary/phobos}"
TOOLS="$ROOT/build/appimage-tools"
DIST="$ROOT/dist"
APPDIR="$ROOT/build/AppDir"
OUTPUT="$DIST/Phobos-$VERSION-x86_64.AppImage"
mkdir -p "$TOOLS" "$DIST"

fetch() {
  if [[ ! -x "$TOOLS/$1" ]]; then
    curl -fL --retry 3 "$2" -o "$TOOLS/$1"
    chmod +x "$TOOLS/$1"
  fi
}
fetch linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch appimagetool-x86_64.AppImage https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

# Both tools are AppImages themselves; this runs them without FUSE (containers, CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/share/phobos" "$APPDIR/usr/share/doc/phobos"
cp -R "$BUILD/Database" "$BUILD/System" "$APPDIR/usr/share/phobos/"
cp "$ROOT/LICENSE" "$APPDIR/usr/share/doc/phobos/"
if [[ -f "$ROOT/COPYING" ]]; then cp "$ROOT/COPYING" "$APPDIR/usr/share/doc/phobos/"; fi
cp "$ROOT/ares/ares/resource/icon@2x.png" "$ROOT/build/phobos.png"
# FFmpeg's libraries sit beside the program in the build (its run path is $ORIGIN, CMakeLists.txt);
# linuxdeploy puts them in usr/lib, each a file of its own that can be swapped for another build
# (the LGPL's terms, as LICENSE describes). linuxdeploy looks for each library's own dependencies
# (libavcodec's libavutil) on the system's search path, so the build folder goes on it.
ffmpeg=()
for library in "$BUILD"/libavutil.so.* "$BUILD"/libavcodec.so.*; do
  if [[ -f "$library" ]]; then ffmpeg+=(--library "$library"); fi
done
LD_LIBRARY_PATH="$BUILD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
  --executable "$BUILD/phobos" ${ffmpeg[@]+"${ffmpeg[@]}"} \
  --desktop-file "$ROOT/desktop/linux/phobos.desktop" \
  --icon-file "$ROOT/build/phobos.png"
# Each FFmpeg library phobos needs is in usr/lib, a file of its own, and is found there at run time: the
# program's run path leads to usr/lib, and libavcodec's (linuxdeploy's $ORIGIN) to libavutil beside it.
needs() { readelf -d "$1" | sed -n 's/.*Shared library: \[\(libav[a-z]*\.so\.[0-9]*\)\]/\1/p'; }
runpath() { readelf -d "$1" | sed -n 's/.*R\(UN\)\{0,1\}PATH.*\[\(.*\)\]/\2/p'; }
for needed in $(needs "$APPDIR/usr/bin/phobos"); do
  test -f "$APPDIR/usr/lib/$needed" && ! test -L "$APPDIR/usr/lib/$needed" ||
    { echo "phobos needs $needed, which isn't a file in the AppImage's usr/lib" >&2; exit 1; }
  for library in "$APPDIR/usr/bin/phobos" "$APPDIR/usr/lib/$needed"; do
    for dependency in $(needs "$library"); do
      found=
      for folder in $(runpath "$library" | tr ':' ' '); do
        folder="${folder//\$ORIGIN/$(dirname "$library")}"
        if [[ -f "$folder/$dependency" ]]; then found=1; fi
      done
      test -n "$found" ||
        { echo "$(basename "$library") wouldn't find $dependency by its run path ($(runpath "$library"))" >&2; exit 1; }
    done
  done
done

rm -f "$OUTPUT" "$OUTPUT.zsync"
(cd "$DIST" && "$TOOLS/appimagetool-x86_64.AppImage" --no-appstream \
  -u "gh-releases-zsync|${REPOSITORY%%/*}|${REPOSITORY##*/}|latest|Phobos-*x86_64.AppImage.zsync" \
  "$APPDIR" "$OUTPUT")
test -f "$OUTPUT.zsync" || { echo "appimagetool wrote no $OUTPUT.zsync" >&2; exit 1; }
echo "$OUTPUT"
echo "$OUTPUT.zsync"
