#!/usr/bin/env bash
# Zips the Windows x64 build as dist/Phobos-<version>-windows-x64.zip (Phobos.exe with the System
# and Database folders it copies into its data folder), after checking that Phobos.exe imports
# nothing but Windows' own DLLs: the C++ runtime, winpthreads and SDL are linked in, and Vulkan
# comes from the driver's vulkan-1.dll, opened at run time.
#   scripts/package-windows.sh [build directory]
# OBJDUMP picks the objdump that reads PE files (x86_64-w64-mingw32-objdump when cross-compiling).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/windows-x64}" && pwd)"
VERSION="$(git -C "$ROOT" describe --tags --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)"
VERSION="${VERSION:-0.0.0-dev}"
EXE="$BUILD/Phobos.exe"
test -f "$EXE" || { echo "No $EXE" >&2; exit 1; }

imports="$("${OBJDUMP:-objdump}" -p "$EXE" | sed -n 's/^[[:space:]]*DLL Name: //p')"
echo "Phobos.exe imports:"
echo "$imports" | sed 's/^/  /'
if echo "$imports" | grep -iE '^(lib.*|sdl3|vulkan-1)\.dll$'; then
  echo "Phobos.exe needs the DLLs above beside it; it should link them statically or load them at run time" >&2
  exit 1
fi

DIST="$ROOT/dist"
STAGE="$DIST/Phobos-$VERSION-windows-x64"
rm -rf "$STAGE" "$STAGE.zip"
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/Phobos.exe"
cp -R "$BUILD/Database" "$BUILD/System" "$STAGE/"
(cd "$DIST" && cmake -E tar cf "$STAGE.zip" --format=zip "$(basename "$STAGE")")
rm -rf "$STAGE"
echo "$STAGE.zip"
