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
#The WAV is the standard one players read: its 44-byte header with RIFF at 0, WAVE and "fmt " at 8, data at 36, its
#two sizes those of the file past them (a terminator after a tag would move them all).
WAV=$SCRATCH/sound.wav
SIZE=$(wc -c < "$WAV")
at() { dd if="$WAV" bs=1 skip="$1" count="$2" 2> /dev/null; }
word() { od -An -tu4 -j"$1" -N4 "$WAV" | tr -d ' '; }
[[ $(at 0 4) == RIFF && $(at 8 8) == "WAVEfmt " && $(at 36 4) == data && $(word 4) == $((SIZE - 8)) &&
   $(word 40) == $((SIZE - 44)) ]] || { od -c -N48 "$WAV"; exit 1; }
echo "the runner runs cube.elf for 60 frames: its summary, its PNGs and its WAV check out"
