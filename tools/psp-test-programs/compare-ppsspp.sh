#!/usr/bin/env bash
#Compares the pictures Phobos's PSP core draws for pspsdk's GU samples "clut", "blend" and "cube" with those PPSSPP's
#software renderer draws after the same second of the PSP's time: a check, from outside, of texturing, the palette,
#filtering, blending and 3D. The two start programs in different times, so PPSSPP's picture is compared with
#Phobos's a frame before, at and a frame after the second, and the closest counts. Run in the phobos-linux
#container, which has PPSSPPHeadless, after build.sh has built the programs.
#PPSSPP is only run here, as a reference: none of its code is in Phobos.
#usage: tools/psp-test-programs/compare-ppsspp.sh <programs folder>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
[[ $# -eq 1 ]] || { echo "usage: $0 <programs folder>" >&2; exit 1; }
PROGRAMS=$(cd "$1" && pwd)
PPSSPP=${PPSSPP:-/opt/tools/ppsspp/build-headless/PPSSPPHeadless}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for sample in clut blend cube; do
  "$PPSSPP" --graphics=software --timeout-emulated=1 --screenshot-save="$WORK/$sample.bmp" "$PROGRAMS/$sample.elf" >/dev/null 2>&1 || true
done
SANITIZE= PSP_TEST_PROGRAMS="$PROGRAMS" PSP_PICTURES="$WORK" "$ROOT/tests/psp/run-tests.sh" >/dev/null

python3 - "$WORK" <<'EOF'
import struct, sys
folder = sys.argv[1]
def ours(name):  #480x272 RGB
    data = open(f"{folder}/{name}.ppm", "rb").read()
    pixels = data[data.index(b"255\n") + 4:]
    return [tuple(pixels[i * 3:i * 3 + 3]) for i in range(480 * 272)]
def theirs(name):  #a BMP of 32-bit pixels (blue, green, red, alpha), rows bottom up unless the height is negative
    data = open(f"{folder}/{name}.bmp", "rb").read()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    rows = []
    for y in range(abs(height)):
        row = (abs(height) - 1 - y) if height > 0 else y
        base = offset + row * width * 4
        rows.append([(data[base + x * 4 + 2], data[base + x * 4 + 1], data[base + x * 4]) for x in range(480)])
    return [pixel for row in rows[:272] for pixel in row]
for name in ("clut", "blend", "cube"):
    b = theirs(name)
    best = None
    for frame in range(3):
        a = ours(f"{name}-{frame}")
        differences = [max(abs(p - q) for p, q in zip(x, y)) for x, y in zip(a, b)]
        if best is None or sum(differences) < sum(best[1]): best = (frame - 1, differences)
    frame, differences = best
    close = sum(d <= 2 for d in differences) / len(differences)
    print(f"{name} (frame {frame:+d}): largest difference {max(differences)}, "
          f"mean {sum(differences) / len(differences):.2f}, within 2 levels {close * 100:.2f}% of pixels")
EOF
