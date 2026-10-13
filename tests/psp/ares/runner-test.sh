#!/usr/bin/env bash
#Builds tools/psp-runner, the headless PSP game runner, and runs the in-repo test program cube.elf
#(pspsdk's GU sample, which draws a cube and runs on) through it for 60 frames, checking the runner's
#report: the summary it prints (the frames it ran, that the program ran on, that no function was
#missing), the PNGs and the WAV it wrote, and its JSON report. Then the clock: two runs started at the
#same time (--start-time) save the same state, and a run started at another time a different one. Then a
#capture with no new picture to take (--screen-stops-at: as a screen that stopped presenting): reported, not passed
#off as fresh.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
export PSP_TEST_PROGRAMS=${PSP_TEST_PROGRAMS-$ROOT/tests/psp/programs}
OUT=${TMPDIR:-/tmp}/phobos-psp-runner
SCRATCH=$OUT/runner-test-$$
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

#(this checkout's runner, which the build's last line names, not the copy the last build anywhere left in $OUT)
RUNNER=$("$ROOT/tools/psp-runner/build.sh" | tail -1)
RUNNER=${RUNNER#built }
"$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 60 --out "$SCRATCH" --png-every 30 \
  --wav "$SCRATCH/sound.wav" --report "$SCRATCH/report.json" > "$SCRATCH/summary.txt" 2> "$SCRATCH/errors.txt"

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
#The report: JSON, the run as asked and as it ran (the software renderer, the recompiler, 60 frames, the program
#running on, both PNGs fresh, their time counted).
python3 - "$SCRATCH/report.json" <<'EOF' || { cat "$SCRATCH/report.json"; exit 1; }
import json, sys
r = json.load(open(sys.argv[1]))
assert r["report"] == "phobos-psp-runner 1" and r["runner"], r["report"]
assert r["asked"]["renderer"] == "Software" and r["ran"]["renderer"] == "Software" and r["ran"]["device"] is None
assert r["asked"]["engine"] == "recompiler" and r["asked"]["frames"] == 60 and r["asked"]["start time"] is None
assert r["game"]["kind"] == "program.elf" and r["game"]["bytes"] > 0
res = r["result"]
assert res["frames"] == 60 and res["program"] == "still running" and res["missing functions"] == []
assert res["stale captures"] == 0 and 0 < res["capture seconds"] <= res["seconds"] and r["gpu"] is None
EOF
#The clock: the same start time, the same state at frame 30; another start time, another state.
for run in a b c; do
  start=1234567890; [[ $run == c ]] && start=1234567999
  "$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 30 --start-time $start --save-state-at 30 "$SCRATCH/state-$run" \
    --report "$SCRATCH/report-$run.json" > "$SCRATCH/summary-$run.txt" 2>&1 || { cat "$SCRATCH/summary-$run.txt"; exit 1; }
done
cmp -s "$SCRATCH/state-a" "$SCRATCH/state-b" || { echo "two runs started at the same time saved different states"; exit 1; }
cmp -s "$SCRATCH/state-a" "$SCRATCH/state-c" && { echo "runs started at different times saved the same state"; exit 1; }
grep -qF '"clock start": 1234567890' "$SCRATCH/report-a.json" || { cat "$SCRATCH/report-a.json"; exit 1; }
#A capture with no new picture: the runner takes none from frame 20 on, so frame 20's PNG has the pictures before it
#(new since no capture) and frame 40's waits its 2 s for one that never comes.
"$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 40 --out "$SCRATCH/stopped" --png-at 20,40 --screen-stops-at 20 \
  > "$SCRATCH/summary-stopped.txt" 2> "$SCRATCH/errors-stopped.txt"
grep -qF "stale captures: 1" "$SCRATCH/summary-stopped.txt" || { cat "$SCRATCH/summary-stopped.txt"; exit 1; }
grep -qF "frame 40: the screen presented no new picture" "$SCRATCH/errors-stopped.txt" || { cat "$SCRATCH/errors-stopped.txt"; exit 1; }
grep -q "frame 20" "$SCRATCH/errors-stopped.txt" && { cat "$SCRATCH/errors-stopped.txt"; exit 1; }
[[ -s $SCRATCH/stopped/frame-000020.png && -s $SCRATCH/stopped/frame-000040.png ]] || { echo "no PNGs"; exit 1; }
echo "the runner runs cube.elf for 60 frames: its summary, PNGs, WAV and report check out; the same start time, the same state; a stale capture said"
