#!/usr/bin/env bash
# Packages the macOS build as Phobos.app, with MoltenVK (macOS has no Vulkan loader) and, in a
# build with them, FFmpeg's libavcodec and libavutil (the PSP's music and movies) in
# Contents/Frameworks, and zips it as dist/Phobos-<version>-macos.zip.
#   scripts/package-macos-app.sh [build directory]
# Signing is optional: MACOS_CERTIFICATE_NAME names a Developer ID identity in the keychain, and
# NOTARY_KEYCHAIN_PROFILE a `notarytool store-credentials` profile. Without them the app is
# signed ad hoc, which runs locally but not from a download without Gatekeeper's warning.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/macos-universal}" && pwd)"
VERSION="$(git -C "$ROOT" describe --tags --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)"
VERSION="${VERSION:-0.0.0-dev}"
MOLTENVK_VERSION="${MOLTENVK_VERSION:-v1.4.2}"
CACHE="$ROOT/build/moltenvk-$MOLTENVK_VERSION"
DIST="$ROOT/dist"
APP="$DIST/Phobos.app"
mkdir -p "$CACHE" "$DIST"

if [[ ! -f "$CACHE/MoltenVK-macos.tar" ]]; then
  curl -fL --retry 3 "https://github.com/KhronosGroup/MoltenVK/releases/download/$MOLTENVK_VERSION/MoltenVK-macos.tar" -o "$CACHE/MoltenVK-macos.tar"
  tar -xf "$CACHE/MoltenVK-macos.tar" -C "$CACHE"
fi
MOLTENVK="$(find "$CACHE" -path '*dylib/macOS/libMoltenVK.dylib' -print -quit)"
test -n "$MOLTENVK" || { echo "No libMoltenVK.dylib in the MoltenVK $MOLTENVK_VERSION archive" >&2; exit 1; }
MOLTENVK_LICENSE="$(find "$CACHE" -path '*/MoltenVK/LICENSE' -print -quit)"
test -n "$MOLTENVK_LICENSE" || { echo "No LICENSE in the MoltenVK $MOLTENVK_VERSION archive" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BUILD/phobos" "$APP/Contents/MacOS/Phobos"
cp "$MOLTENVK" "$APP/Contents/Frameworks/libMoltenVK.dylib"
# FFmpeg's libraries sit beside the program in the build, which finds them by its run path
# (@rpath/libavcodec.63.dylib, CMakeLists.txt); in the app they're in Frameworks, each a file of
# its own that can be swapped for another build (the LGPL's terms, as LICENSE describes).
if otool -L "$BUILD/phobos" | grep -q '@rpath/libav'; then
  for library in avcodec avutil; do
    file="$(cd "$BUILD" && ls lib$library.[0-9]*.dylib 2>/dev/null || true)"
    test -n "$file" || { echo "phobos needs FFmpeg's lib$library, which isn't beside it in $BUILD" >&2; exit 1; }
    for arch in $(lipo -archs "$BUILD/phobos"); do
      lipo -archs "$BUILD/$file" | tr ' ' '\n' | grep -qx "$arch" ||
        { echo "$file has no $arch, which phobos is built for" >&2; exit 1; }
    done
    cp "$BUILD/$file" "$APP/Contents/Frameworks/$file"
  done
  install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/Phobos"
  install_name_tool -delete_rpath @executable_path "$APP/Contents/MacOS/Phobos"
fi
cp -R "$BUILD/Database" "$BUILD/System" "$APP/Contents/Resources/"
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
if [[ -f "$ROOT/COPYING" ]]; then cp "$ROOT/COPYING" "$APP/Contents/Resources/COPYING"; fi
cp "$MOLTENVK_LICENSE" "$APP/Contents/Resources/LICENSE-MoltenVK"

ICONSET="$ROOT/build/Phobos.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for size in 16 32 64 128 256; do
  sips -z "$size" "$size" "$ROOT/ares/ares/resource/icon@2x.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z "$double" "$double" "$ROOT/ares/ares/resource/icon@2x.png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Phobos.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Phobos</string>
  <key>CFBundleDisplayName</key><string>Phobos</string>
  <key>CFBundleIdentifier</key><string>com.phobos.emulator</string>
  <key>CFBundleExecutable</key><string>Phobos</string>
  <key>CFBundleIconFile</key><string>Phobos</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

if [[ -n "${MACOS_CERTIFICATE_NAME:-}" ]]; then
  for library in "$APP"/Contents/Frameworks/*.dylib; do
    codesign --force --options runtime --timestamp --sign "$MACOS_CERTIFICATE_NAME" "$library"
  done
  codesign --force --options runtime --timestamp --sign "$MACOS_CERTIFICATE_NAME" "$APP"
else
  for library in "$APP"/Contents/Frameworks/*.dylib; do codesign --force --sign - "$library"; done
  codesign --force --sign - "$APP"
fi

OUTPUT="$DIST/Phobos-$VERSION-macos.zip"
rm -f "$OUTPUT"
if [[ -n "${MACOS_CERTIFICATE_NAME:-}" && -n "${NOTARY_KEYCHAIN_PROFILE:-}" ]]; then
  ditto -c -k --keepParent "$APP" "$OUTPUT"
  xcrun notarytool submit "$OUTPUT" --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait
  xcrun stapler staple "$APP"
  rm -f "$OUTPUT"
fi
ditto -c -k --keepParent "$APP" "$OUTPUT"

echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/Phobos")"
for library in "$APP"/Contents/Frameworks/libav*.dylib; do
  if [[ -f "$library" ]]; then echo "$(basename "$library"): $(lipo -archs "$library")"; fi
done
echo "$OUTPUT"
