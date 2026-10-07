#!/usr/bin/env bash
#Builds tools/psp-runner, the headless PSP game runner, and runs the in-repo test program cube.elf
#(pspsdk's GU sample, which draws a cube and runs on) through it for 60 frames, checking the runner's
#report: the summary it prints (the frames it ran, that the program ran on, that no function was
#missing), and the PNGs and the WAV it wrote.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
export PSP_TEST_PROGRAMS=${PSP_TEST_PROGRAMS-$ROOT/tests/psp/programs}
OUT=${TMPDIR:-/tmp}/phobos-psp-runner
SCRATCH=$OUT/runner-test-$$
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

"$ROOT/tools/psp-runner/build.sh" > /dev/null
"$OUT/psp-runner" "$PSP_TEST_PROGRAMS/cube.elf" --frames 60 --out "$SCRATCH" --png-every 30 \
  --wav "$SCRATCH/sound.wav" > "$SCRATCH/summary.txt" 2> "$SCRATCH/errors.txt"

#The summary: the frames ran, the program still running (cube never ends), no function missing.
for line in "frames run: 60" "program: still running" "unique missing functions: 0"; do
  grep -qF "$line" "$SCRATCH/summary.txt" || { cat "$SCRATCH/summary.txt" "$SCRATCH/errors.txt"; exit 1; }
done
#The frames, as PNGs (every 30th presented frame), and the sound, as a WAV.
for file in frame-000030.png frame-000060.png sound.wav; do
  [[ -s "$SCRATCH/$file" ]] || { echo "no $file"; exit 1; }
done
#The PNGs are the PSP's screen, 480 by 272.
for file in frame-000030.png frame-000060.png; do
  file "$SCRATCH/$file" | grep -q "480 x 272" || { file "$SCRATCH/$file"; exit 1; }
done
echo "the runner runs cube.elf for 60 frames: its summary, its PNGs and its WAV check out"
