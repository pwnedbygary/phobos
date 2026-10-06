#!/usr/bin/env bash
# Zips the Windows x64 build as dist/Phobos-<version>-windows-x64.zip (Phobos.exe, FFmpeg's DLLs in a build with them,
# the license notices, and the System and Database folders it copies into its data folder), after checking that
# Phobos.exe imports nothing but Windows' own DLLs and FFmpeg's: the C++ runtime, winpthreads and SDL are linked in,
# and Vulkan comes from the driver's vulkan-1.dll, opened at run time.
#   scripts/package-windows.sh [build directory]
# OBJDUMP picks the objdump that reads PE files (x86_64-w64-mingw32-objdump when cross-compiling).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/windows-x64}" && pwd)"
VERSION="$(git -C "$ROOT" describe --tags --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)"
VERSION="${VERSION:-0.0.0-dev}"
EXE="$BUILD/Phobos.exe"
test -f "$EXE" || { echo "No $EXE" >&2; exit 1; }

imports_of() { "${OBJDUMP:-objdump}" -p "$1" | sed -n 's/^[[:space:]]*DLL Name: //p'; }
imports="$(imports_of "$EXE")"
echo "Phobos.exe imports:"
echo "$imports" | sed 's/^/  /'
if echo "$imports" | grep -iE '^(lib.*|sdl3|vulkan-1)\.dll$'; then
  echo "Phobos.exe needs the DLLs above beside it; it should link them statically or load them at run time" >&2
  exit 1
fi

# FFmpeg's libavcodec and libavutil (the PSP's music and movies): CMake copies them beside Phobos.exe, and they go
# beside it in the zip, each a file of its own that can be swapped for another build (the LGPL's terms, as LICENSE
# describes). They may need each other and Windows' own DLLs, nothing else.
ffmpeg=()
for dll in $(echo "$imports" | grep -iE '^(avcodec|avutil)-[0-9]+\.dll$' || true); do
  test -f "$BUILD/$dll" || { echo "Phobos.exe needs $dll, which isn't beside it in $BUILD" >&2; exit 1; }
  needs="$(imports_of "$BUILD/$dll")"
  echo "$dll imports:"
  echo "$needs" | sed 's/^/  /'
  if echo "$needs" | grep -viE '^(avcodec|avutil)-[0-9]+\.dll$' | grep -iE '^(lib.*|sdl3|vulkan-1)\.dll$'; then
    echo "$dll needs the DLLs above beside it; FFmpeg should be built without them (thirdparty/ffmpeg/build.sh)" >&2
    exit 1
  fi
  ffmpeg+=("$BUILD/$dll")
done

DIST="$ROOT/dist"
STAGE="$DIST/Phobos-$VERSION-windows-x64"
rm -rf "$STAGE" "$STAGE.zip"
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/Phobos.exe"
for dll in ${ffmpeg[@]+"${ffmpeg[@]}"}; do cp "$dll" "$STAGE/"; done
cp -R "$BUILD/Database" "$BUILD/System" "$STAGE/"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
if [[ -f "$ROOT/COPYING" ]]; then cp "$ROOT/COPYING" "$STAGE/COPYING.txt"; fi
(cd "$DIST" && cmake -E tar cf "$STAGE.zip" --format=zip "$(basename "$STAGE")")
rm -rf "$STAGE"
echo "$STAGE.zip"
