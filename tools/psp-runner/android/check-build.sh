#!/usr/bin/env bash
#Checks a build of the runner for Android (build-android.sh's folder) is what a phone's adb shell can run: an ARM64
#position-independent executable for Android's linker, needing only the system's libraries and the FFmpeg beside it
#(its run path, $ORIGIN/lib; no shared libc++), with the measurements in it.
#usage: check-build.sh BUILD_FOLDER
#(with the NDK's llvm-readelf and llvm-nm: ANDROID_NDK_HOME, else the SDK's ndk/28.*, as build-android.sh finds it)
set -euo pipefail
FOLDER=${1:?usage: $0 BUILD_FOLDER}
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [[ -z $NDK ]]; then
  for SDK in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
    [[ -n $SDK && -d $SDK/ndk ]] || continue
    NDK=$(find "$SDK/ndk" -mindepth 1 -maxdepth 1 -name '28.*' | sort | tail -1)
    [[ -n $NDK ]] && break
  done
fi
TOOLS=$(find "$NDK/toolchains/llvm/prebuilt" -mindepth 1 -maxdepth 1 -type d | head -1)/bin
BINARY=$FOLDER/psp-runner-android
fail() { echo "check-build.sh: $*" >&2; exit 1; }
[[ -x $BINARY ]] || fail "no executable psp-runner-android in $FOLDER"
HEADER=$("$TOOLS/llvm-readelf" -h "$BINARY")
grep -q 'Machine: *AArch64' <<< "$HEADER" || fail "not ARM64"
grep -q 'Type: *DYN' <<< "$HEADER" || fail "not position-independent"
#(each tool's output taken whole: grep -q stopping early would end the tool with SIGPIPE, which pipefail counts)
SEGMENTS=$("$TOOLS/llvm-readelf" -l "$BINARY")
grep -q 'interpreter: /system/bin/linker64' <<< "$SEGMENTS" || fail "not for Android's linker"
DYNAMIC=$("$TOOLS/llvm-readelf" -d "$BINARY")
grep -q 'FLAGS_1.*PIE' <<< "$DYNAMIC" || fail "not a PIE"
grep -q 'RUNPATH.*\[\$ORIGIN/lib\]' <<< "$DYNAMIC" || fail "no run path \$ORIGIN/lib"
while read -r library; do
  case $library in
    libc.so|libm.so|libdl.so|libz.so|liblog.so|libandroid.so) ;;
    libavcodec.so|libavutil.so) [[ -f $FOLDER/lib/$library ]] || fail "needs $library, which isn't in lib/" ;;
    *) fail "needs $library, which isn't the system's or beside it" ;;
  esac
done < <(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<< "$DYNAMIC")
SYMBOLS=$("$TOOLS/llvm-nm" -C "$FOLDER/symbols/psp-runner-android")
grep -q 'phobos_measure::begin' <<< "$SYMBOLS" ||
  fail "no measurements in it (symbols/psp-runner-android has no phobos_measure::begin)"
grep -q '^sha256: ' "$FOLDER/build.txt" || fail "build.txt has no sha256"
echo "$FOLDER: ARM64 PIE for Android, needs $(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<< "$DYNAMIC" | tr '\n' ' ')"
