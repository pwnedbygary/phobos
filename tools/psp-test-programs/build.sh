#!/usr/bin/env bash
#Builds the PSP core's test programs with pspdev's toolchain (the phobos-linux container has it in /opt/pspdev)
#into a folder, for PSP_TEST_PROGRAMS (see tests/psp/loader.cpp): hello.elf (a static executable), hello.prx (a
#relocatable module) and EBOOT.PBP (holding the static one); system.elf, which uses files and the controls
#(tests/psp/files.cpp); gu.elf, which drives the GE through pspsdk's GU library, and pspsdk's own GU samples "copy",
#"blit", "clut", "blend" and "doublelist" (copy.elf and so on, from the toolchain's samples) (tests/psp/ge.cpp and
#draw.cpp). They're built from source each time, so no binary goes in the repository.
#usage: tools/psp-test-programs/build.sh <output folder>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[[ $# -eq 1 ]] || { echo "usage: $0 <output folder>" >&2; exit 1; }
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
export PATH="${PSPDEV:-/opt/pspdev}/bin:$PATH"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cp -r "$HERE/hello" "$WORK/static"
make -C "$WORK/static" >/dev/null
cp "$WORK/static/hello.elf" "$WORK/static/EBOOT.PBP" "$OUT/"

cp -r "$HERE/hello" "$WORK/prx"
make -C "$WORK/prx" BUILD_PRX=1 >/dev/null
cp "$WORK/prx/hello.prx" "$OUT/"

cp -r "$HERE/system" "$WORK/system"
make -C "$WORK/system" >/dev/null
cp "$WORK/system/system.elf" "$OUT/"

cp -r "$HERE/gu" "$WORK/gu"
make -C "$WORK/gu" >/dev/null
cp "$WORK/gu/gu.elf" "$OUT/"

SAMPLES="$(psp-config --pspsdk-path)/samples/gu"
mkdir "$WORK/samples"
cp -r "$SAMPLES/common" "$WORK/samples/"
for sample in copy blit clut blend doublelist; do
  cp -r "$SAMPLES/$sample" "$WORK/samples/"
  make -C "$WORK/samples/$sample" >/dev/null
  cp "$WORK/samples/$sample/$sample.elf" "$OUT/"
done
echo "built hello.elf, hello.prx, EBOOT.PBP, system.elf, gu.elf and the GU samples in $OUT"
