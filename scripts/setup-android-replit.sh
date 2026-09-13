#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
SDK="$ROOT/.local/android-sdk"
if [[ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]]; then
  TEMP="$(mktemp -d)"
  trap 'rm -rf "$TEMP"' EXIT
  curl -fL --retry 3 https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip -o "$TEMP/tools.zip"
  unzip -q "$TEMP/tools.zip" -d "$TEMP"
  mkdir -p "$SDK/cmdline-tools"
  mv "$TEMP/cmdline-tools" "$SDK/cmdline-tools/latest"
fi
# Interactive acceptance avoids silently accepting licenses on future installs.
"$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" --licenses
"$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" "platforms;android-37.0" "build-tools;36.0.0" "ndk;28.2.13676358" "cmake;3.22.1" "platform-tools"
git -C "$ROOT" submodule update --init --recursive