#!/usr/bin/env bash
#Builds tools/psp-runner, the headless PSP game runner, and runs the in-repo test program cube.elf
#(pspsdk's GU sample, which draws a cube and runs on) through it for 60 frames, checking the runner's
#report: the summary it prints (the frames it ran, that the program ran on, that no function was
#missing), the PNGs and the WAV it wrote, and its JSON report. Then the clock: two runs started at the
#same time (--start-time) save the same state, and a run started at another time a different one; a state loaded
#keeps its own clock. Numbers past 32 bits, or with more after them, are refused. Then captures with no new
#picture to take (--screen-stops-at: as a screen that stopped presenting): reported, not passed off as fresh, and
#with no picture at all, no PNG. Then a game's name that isn't UTF-8 (where the filesystem takes one): the report is
#still JSON.
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

#The summary: the frames ran, the program still running (cube never ends), no function missing, and the captures'
#time on a line of its own (scripts take the speed from the one line with "frames per second" in it).
for line in "frames run: 60" "program: still running" "unique missing functions: 0"; do
  grep -qF "$line" "$SCRATCH/summary.txt" || { cat "$SCRATCH/summary.txt" "$SCRATCH/errors.txt"; exit 1; }
done
if [[ $(grep -c "frames per second" "$SCRATCH/summary.txt") != 1 ]] ||
   ! grep -q "^capture seconds: " "$SCRATCH/summary.txt"; then
  cat "$SCRATCH/summary.txt"; exit 1
fi
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
#The report: JSON, the run as asked (what wasn't given null) and as it ran (the software renderer, the recompiler, 60
#frames, the program running on, both PNGs fresh, each picture arrived by its frame, their time counted).
python3 - "$SCRATCH/report.json" <<'EOF' || { cat "$SCRATCH/report.json"; exit 1; }
import json, sys
r = json.load(open(sys.argv[1], encoding="utf-8"))
assert r["report"] == "phobos-psp-runner 1" and r["runner"]["source"] and r["runner"]["compiler"], r["runner"]
assert r["runner"]["build"] == "debug" and r["runner"]["ffmpeg"] in (True, False), r["runner"]
asked, ran = r["asked"], r["ran"]
assert asked["renderer"] is None and asked["engine"] is None and asked["resolution"] is None, asked
assert asked["frames"] == 60 and asked["start time"] is None and asked["png every"] == 30, asked
assert asked["png at"] is None and asked["presses"] == 0 and asked["memory stick"] is None, asked
assert ran["renderer"] == "Software" and ran["device"] is None and ran["gpu lost"] is False, ran
assert ran["engine"] == "recompiler" and ran["resolution"] == 1 and ran["utc offset"], ran
assert r["game"]["kind"] == "program.elf" and r["game"]["bytes"] > 0
res = r["result"]
assert res["frames"] == 60 and res["program"] == "still running" and res["missing functions"] == []
assert res["stale captures"] == 0 and 0 < res["capture seconds"] <= res["seconds"] and r["gpu"] is None
assert res["clock restarts"] is None, res["clock restarts"]
assert [c["frame"] for c in res["captures"]] == [30, 60], res["captures"]
for c in res["captures"]:
    assert c["fresh"] and 1 <= c["arrived in frame"] <= c["frame"] and c["waited ms"] >= 0, c
EOF
#The clock: the same start time, the same state at frame 30; another start time, another state.
for run in a b c; do
  start=1234567890; [[ $run == c ]] && start=1234567999
  "$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 30 --start-time $start --save-state-at 30 \
    "$SCRATCH/state-$run" --report "$SCRATCH/report-$run.json" > "$SCRATCH/summary-$run.txt" 2>&1 ||
    { cat "$SCRATCH/summary-$run.txt"; exit 1; }
done
cmp -s "$SCRATCH/state-a" "$SCRATCH/state-b" ||
  { echo "two runs started at the same time saved different states"; exit 1; }
cmp -s "$SCRATCH/state-a" "$SCRATCH/state-c" && { echo "runs started at different times saved the same state"; exit 1; }
grep -qF '"clock start": 1234567890' "$SCRATCH/report-a.json" || { cat "$SCRATCH/report-a.json"; exit 1; }
grep -qF '"clock restarts": 0' "$SCRATCH/report-a.json" || { cat "$SCRATCH/report-a.json"; exit 1; }
#A state loaded keeps the clock it was saved with, said, and isn't set again as though the game had restarted.
"$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 5 --load-state "$SCRATCH/state-a" --start-time 1500000000 \
  --report "$SCRATCH/report-d.json" > "$SCRATCH/summary-d.txt" 2> "$SCRATCH/errors-d.txt" ||
  { cat "$SCRATCH/summary-d.txt" "$SCRATCH/errors-d.txt"; exit 1; }
grep -qF -- "--start-time: the loaded state keeps its own clock" "$SCRATCH/errors-d.txt" ||
  { cat "$SCRATCH/errors-d.txt"; exit 1; }
grep -q "started itself again" "$SCRATCH/errors-d.txt" && { cat "$SCRATCH/errors-d.txt"; exit 1; }
for line in '"clock start": 1234567890' '"clock restarts": 0' '"start time": 1500000000'; do
  grep -qF "$line" "$SCRATCH/report-d.json" || { cat "$SCRATCH/report-d.json"; exit 1; }
done
#Numbers are decimal digits up to 32 bits: a start time past them is refused (not wrapped to 1970), and so are a
#number with more after it and a press's frame past 32 bits.
refused() {  #refused MESSAGE OPTION...: the runner refuses the options, saying MESSAGE
  local message=$1; shift
  if "$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 1 "$@" > /dev/null 2> "$SCRATCH/errors-e.txt" ||
     ! grep -qF -- "$message" "$SCRATCH/errors-e.txt"; then
    echo "not refused: $*"; cat "$SCRATCH/errors-e.txt"; exit 1
  fi
}
refused "--start-time is at most 4294967295" --start-time 4294967296
refused "--start-time needs a number" --start-time 12abc
refused "a press's frame is a number up to 4294967295" --press 4294967296:Cross
#Captures with no new picture: the runner takes none from frame 20 on. Frame 19's PNG waits for a new picture (one
#comes: the screen still presents); frame 20's has one only if it came while frame 19's was written; frame 40's
#waits its 2 s for one that never comes. Each is fresh or stale as it was, said and counted so, with a PNG.
"$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 40 --out "$SCRATCH/stopped" --png-at 19,20,40 --screen-stops-at 20 \
  --report "$SCRATCH/stopped.json" > "$SCRATCH/summary-stopped.txt" 2> "$SCRATCH/errors-stopped.txt"
python3 - "$SCRATCH/stopped.json" "$SCRATCH/summary-stopped.txt" "$SCRATCH/errors-stopped.txt" <<'EOF' ||
import json, sys
r = json.load(open(sys.argv[1], encoding="utf-8"))
summary, errors = open(sys.argv[2]).read(), open(sys.argv[3]).read()
captures = r["result"]["captures"]
assert [c["frame"] for c in captures] == [19, 20, 40] and captures[0]["fresh"] and not captures[2]["fresh"], captures
stale = [c["frame"] for c in captures if not c["fresh"]]
assert r["result"]["stale captures"] == len(stale) and "stale captures: %d\n" % len(stale) in summary, summary
for frame in (19, 20, 40):
    assert ("frame %d: the screen presented no new picture" % frame in errors) == (frame in stale), errors
EOF
  { cat "$SCRATCH/summary-stopped.txt" "$SCRATCH/errors-stopped.txt" "$SCRATCH/stopped.json"; exit 1; }
for png in frame-000019.png frame-000020.png frame-000040.png; do
  [[ -s $SCRATCH/stopped/$png ]] || { echo "no $png"; exit 1; }
done
#A capture with no picture at all (the screen stopped from the first frame): said, no PNG, and in the report (whose
#folder the runner makes).
"$RUNNER" "$PSP_TEST_PROGRAMS/cube.elf" --frames 1 --out "$SCRATCH/none" --png-at 1 --screen-stops-at 1 \
  --report "$SCRATCH/none/report/report.json" > "$SCRATCH/summary-none.txt" 2> "$SCRATCH/errors-none.txt"
grep -qF "frame 1: the screen presented no picture in 2 s, so it has no PNG" "$SCRATCH/errors-none.txt" ||
  { cat "$SCRATCH/errors-none.txt"; exit 1; }
[[ -e $SCRATCH/none/frame-000001.png ]] && { echo "a PNG with no picture"; exit 1; }
python3 - "$SCRATCH/none/report/report.json" <<'EOF' || { cat "$SCRATCH/none/report/report.json"; exit 1; }
import json, sys
r = json.load(open(sys.argv[1], encoding="utf-8"))
(capture,) = r["result"]["captures"]
assert capture["frame"] == 1 and capture["arrived in frame"] is None and capture["fresh"] is False, capture
assert r["result"]["stale captures"] == 1 and r["asked"]["screen stops at"] == 1 and r["asked"]["png at"] == [1]
EOF
#A game's name that isn't UTF-8 (a Latin-1 é): the report escapes the byte and stays JSON. macOS's filesystems take
#only UTF-8 names, so there the check is left out, said.
odd="$SCRATCH/cube-$(printf '\351').elf"
if cp "$PSP_TEST_PROGRAMS/cube.elf" "$odd" 2> /dev/null; then
  "$RUNNER" "$odd" --frames 1 --report "$SCRATCH/odd.json" > /dev/null 2>&1 ||
    { echo "the run of a name not in UTF-8 failed"; exit 1; }
  python3 - "$SCRATCH/odd.json" <<'EOF' || { cat "$SCRATCH/odd.json"; exit 1; }
import json, sys
r = json.load(open(sys.argv[1], encoding="utf-8"))
assert r["game"]["path"].endswith("/cube-\u00e9.elf"), r["game"]["path"]
EOF
  odd="a game's name not in UTF-8 escaped"
else
  odd="(a name not in UTF-8 left out: this filesystem takes none)"
fi
echo "the runner runs cube.elf for 60 frames: its summary, PNGs, WAV and report check out; the same start time, the" \
  "same state; a loaded state's clock kept; stale and empty captures said; $odd"
