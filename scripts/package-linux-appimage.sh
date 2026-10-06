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
# (the LGPL's terms, as LICENSE describes).
ffmpeg=()
for library in "$BUILD"/libavcodec.so.* "$BUILD"/libavutil.so.*; do
  if [[ -f "$library" ]]; then ffmpeg+=(--library "$library"); fi
done
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
  --executable "$BUILD/phobos" ${ffmpeg[@]+"${ffmpeg[@]}"} \
  --desktop-file "$ROOT/desktop/linux/phobos.desktop" \
  --icon-file "$ROOT/build/phobos.png"
for needed in $(readelf -d "$BUILD/phobos" | sed -n 's/.*Shared library: \[\(libav[a-z]*\.so\.[0-9]*\)\]/\1/p'); do
  test -f "$APPDIR/usr/lib/$needed" || { echo "phobos needs $needed, which isn't in the AppImage's usr/lib" >&2; exit 1; }
done

rm -f "$OUTPUT" "$OUTPUT.zsync"
(cd "$DIST" && "$TOOLS/appimagetool-x86_64.AppImage" --no-appstream \
  -u "gh-releases-zsync|${REPOSITORY%%/*}|${REPOSITORY##*/}|latest|Phobos-*x86_64.AppImage.zsync" \
  "$APPDIR" "$OUTPUT")
test -f "$OUTPUT.zsync" || { echo "appimagetool wrote no $OUTPUT.zsync" >&2; exit 1; }
echo "$OUTPUT"
echo "$OUTPUT.zsync"
