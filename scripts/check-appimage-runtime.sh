#!/usr/bin/env bash
# Checks that each AppImage begins with the runtime scripts/appimage-pins.sh pins, and that the runtime still carries
# what LICENSE's notice for it names: libfuse's and squashfuse's versions, and its commit, which it prints.
#   scripts/check-appimage-runtime.sh dist/Phobos-*-x86_64.AppImage
# The runtime is the AppImage's ELF file, which ends with its section headers; the squashfs image follows. appimagetool
# fills in four of its sections (.digest_md5, .upd_info, .sha256_sig and .sig_key: the image's MD5, the update
# information, a signature and its key), which are zeros in type2-runtime's build, so they are zeroed again before
# the runtime's SHA-256 is compared with the pinned build's. Needs readelf (binutils).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/appimage-pins.sh
source "$ROOT/scripts/appimage-pins.sh"
if [[ $# -eq 0 ]]; then echo "usage: $0 APPIMAGE..." >&2; exit 2; fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# field FILE OFFSET SIZE: an unsigned little-endian number from the ELF header.
field() { od -An -t "u$3" -j "$2" -N "$3" "$1" | tr -d ' '; }
# carries STRING: one of the runtime's strings is exactly STRING.
carries() { grep -q -x -F -e "$1" "$WORK/strings"; }
fail() { echo "$appimage: $*" >&2; failed=1; }

failed=0
for appimage in "$@"; do
  if [[ "$(od -An -c -N 4 "$appimage" | tr -d ' ')" != '177ELF' || "$(field "$appimage" 4 1)" != 2 ||
    "$(od -An -c -j 8 -N 3 "$appimage" | tr -d ' ')" != 'AI002' ]]; then
    fail "isn't a type 2 AppImage for a 64-bit system (its first bytes aren't ELF's and AI 2's)"
    continue
  fi
  size=$(($(field "$appimage" 40 8) + $(field "$appimage" 58 2) * $(field "$appimage" 60 2)))
  runtime="$WORK/runtime"
  head -c "$size" "$appimage" > "$runtime"
  zeroed=0
  while read -r _ offset length; do
    dd if=/dev/zero of="$runtime" bs=1 seek=$((16#$offset)) count=$((16#$length)) conv=notrunc status=none
    zeroed=$((zeroed + 1))
  done < <(readelf -S -W "$runtime" | sed -n 's/^ *\[ *[0-9]*\] //p' |
    awk '$1 ~ /^\.(digest_md5|upd_info|sha256_sig|sig_key)$/ { print $1, $4, $5 }')
  tr '\0' '\n' < "$runtime" > "$WORK/strings"
  commit="$(sed -n '\|^https://github.com/AppImage/type2-runtime/commit/[0-9a-f]*$|{s|.*/||p;q;}' "$WORK/strings")"
  sha256="$(sha256sum < "$runtime" | cut -d' ' -f1)"
  echo "$appimage: runtime of $size bytes, type2-runtime/commit/${commit:-(none)}," \
    "SHA-256 $sha256 with appimagetool's $zeroed sections zeroed"
  if [[ "$zeroed" -ne 4 ]]; then fail "its runtime has $zeroed of appimagetool's 4 sections, not all"; fi
  if [[ "$sha256" != "$RUNTIME_SHA256" ]]; then
    fail "its runtime isn't the build pinned, type2-runtime $RUNTIME_COMMIT's (SHA-256 $RUNTIME_SHA256)"
  fi
  if [[ -z "$commit" || "$RUNTIME_COMMIT" != "$commit"* ]]; then
    fail "its runtime names commit ${commit:-(none)}, not the pinned $RUNTIME_COMMIT"
  fi
  if ! carries "$LIBFUSE_VERSION"; then fail "its runtime doesn't carry libfuse's $LIBFUSE_VERSION"; fi
  if ! carries "squashfuse $SQUASHFUSE_VERSION"; then
    fail "its runtime doesn't carry squashfuse $SQUASHFUSE_VERSION"
  fi
done
if [[ "$failed" -ne 0 ]]; then
  echo "The AppImage's runtime and LICENSE's notice for it (\"AppImage runtime\") must agree with" \
    "scripts/appimage-pins.sh" >&2
  exit 1
fi
echo "The runtime is the pinned build, with libfuse $LIBFUSE_VERSION and squashfuse $SQUASHFUSE_VERSION"
