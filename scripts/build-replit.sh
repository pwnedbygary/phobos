#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
export ANDROID_HOME="$ROOT/.local/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
if [[ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]]; then
  echo "Android SDK missing. Run: bash scripts/setup-android-replit.sh" >&2
  exit 1
fi
if [[ ! -f "$ROOT/thirdparty/libadrenotools/CMakeLists.txt" ]]; then
  echo "Missing native dependency. Run: git submodule update --init --recursive" >&2
  exit 1
fi
cd "$ROOT/android"
if [[ $# -eq 0 ]]; then set -- assembleRelease; fi
exec ./gradlew --no-daemon --max-workers=2 --console=plain "$@"