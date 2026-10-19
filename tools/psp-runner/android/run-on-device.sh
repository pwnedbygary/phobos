#!/usr/bin/env bash
#Runs a build of the runner for Android (tools/psp-runner/build-android.sh) on a device over adb, from adb shell and
#without the app: the build, the system fonts and the state to load pushed to the device's folder (each once), the
#game read where it is on the device, the run given a time limit, and its folder (the runner's output, its report,
#the measurements' JSON, the PNGs, what the device was like before and after) pulled back.
#
#usage: run-on-device.sh [options] BUILD GAME [runner options]
#  BUILD   a build's folder (build-android.sh's last line), its psp-runner-android, or a commit: its build in
#          $PHOBOS_ANDROID_OUT, made first if it isn't there, or again if its measure.hpp isn't this checkout's (so
#          the builds a comparison runs are measured alike)
#  GAME    the game on the device: its path, or a name in $PHOBOS_DEVICE_ROMS ("Killzone - Liberation" takes
#          Killzone - Liberation.chd, .iso or .cso there)
#  runner options: the runner's (tools/psp-runner/README.md), with files and folders on this computer or the device's
#          folder: --load-state and --script files here are pushed; --out, --wav, --report and --save-state-at's file
#          go in the run's folder; --memory-stick and --pipeline-cache by a bare name are kept in the device's folder
#          (memory-sticks/, cache/) between runs; --fonts is the pushed fonts unless given; --report is added where the
#          runner has it (since e56c760f2)
#options:
#  -s, --serial SERIAL   the device ($ANDROID_SERIAL)
#  --name NAME           the run's name (the build's, the game's, the renderer's and the time otherwise)
#  --timeout SECONDS     the run's time limit on the device (600)
#  --repeat N            N runs, one after another, NAME-1 to NAME-N
#  --cooldown SECONDS    a pause before each run after the first (0)
#  --pin MODE            the emulation thread's pinning (PHOBOS_PIN: app, the default, as the app pins it; fastest;
#                        none; or CPUs, "7"); --nice N its nice value (-10); --no-perf no hardware counters
#  --ge-pin CPUS         the GE's own thread's CPUs ("GE Thread" On; PHOBOS_GE_PIN, "7", "4-6"), every frame; unset,
#                        it's left as it is (the CPU it was on is kept either way)
#  --ge-place            the GE's own thread and the emulation thread placed as the app places them with its "PSP
#                        thread placement" on (PHOBOS_GE_PLACE=1: the GE's on the fastest core the runner may use, the
#                        emulation thread on the next, by the cores' capacities, every frame)
#  --perf-harden-off     hardware counters allowed for the runs: security.perf_harden set to 0, and back to what it
#                        was after (it changes a system property; without it the counters are tried as the device is)
#  --no-fonts            no system fonts
#  --dry-run             the adb commands and the device's script printed, not run
#  --list                what the device's folder holds; --clean-runs, --clean-builds: its runs' or builds' folders
#                        removed; --clean: the whole folder removed
#Environment: ADB (adb, else the SDK's), ANDROID_SERIAL, PHOBOS_DEVICE_DIR (the device's folder,
#/data/local/tmp/phobos-runner: everything it writes is in it), PHOBOS_DEVICE_ROMS, PHOBOS_FONTS (the fonts' folder
#here), PHOBOS_RUNS (the runs' folders here, ${TMPDIR:-/tmp}/phobos-psp-runner-android/runs), PHOBOS_ANDROID_OUT and
#PHOBOS_FLAVOR (as build-android.sh takes them).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPOSITORY=$(cd "$HERE/../../.." && pwd)

SERIAL=${ANDROID_SERIAL:-}
DEVICE=${PHOBOS_DEVICE_DIR:-/data/local/tmp/phobos-runner}
ROMS=${PHOBOS_DEVICE_ROMS:-}
FONTS=${PHOBOS_FONTS:-}
RUNS=${PHOBOS_RUNS:-${TMPDIR:-/tmp}/phobos-psp-runner-android/runs}
BUILDS=${PHOBOS_ANDROID_OUT:-${PHOBOS_ANDROID_WORK:-${TMPDIR:-/tmp}/phobos-psp-runner-android}/builds}
FLAVOR=${PHOBOS_FLAVOR:-modern}
NAME= TIMEOUT=600 REPEAT=1 COOLDOWN=0 DRY=0 HARDEN=0 USE_FONTS=1 ACTION=run
ENVIRONMENT=()
for variable in PHOBOS_PIN PHOBOS_GE_PIN PHOBOS_GE_PLACE PHOBOS_NICE PHOBOS_PERF PHOBOS_FREQ_EVERY PHOBOS_MEASURE \
                PSP_GPU_ON_CPU; do
  [[ -z ${!variable:-} ]] || ENVIRONMENT+=("$variable=${!variable}")
done

usage() { sed -n '/^#usage:/,/^#Environment/p' "$0" | sed 's/^#//' >&2; exit 2; }
while (($#)); do
  case $1 in
    -s|--serial) SERIAL=$2; shift 2 ;;
    --name) NAME=$2; shift 2 ;;
    --timeout) TIMEOUT=$2; shift 2 ;;
    --repeat) REPEAT=$2; shift 2 ;;
    --cooldown) COOLDOWN=$2; shift 2 ;;
    --pin) ENVIRONMENT+=("PHOBOS_PIN=$2"); shift 2 ;;
    --ge-pin) ENVIRONMENT+=("PHOBOS_GE_PIN=$2"); shift 2 ;;
    --ge-place) ENVIRONMENT+=("PHOBOS_GE_PLACE=1"); shift ;;
    --nice) ENVIRONMENT+=("PHOBOS_NICE=$2"); shift 2 ;;
    --no-perf) ENVIRONMENT+=("PHOBOS_PERF=0"); shift ;;
    --perf-harden-off) HARDEN=1; shift ;;
    --no-fonts) USE_FONTS=0; shift ;;
    --dry-run) DRY=1; shift ;;
    --list) ACTION=list; shift ;;
    --clean) ACTION=clean; shift ;;
    --clean-runs) ACTION=clean-runs; shift ;;
    --clean-builds) ACTION=clean-builds; shift ;;
    -h|--help) usage ;;
    --) shift; break ;;
    -*) echo "no such option: $1 (the runner's options go after BUILD and GAME)" >&2; exit 2 ;;
    *) break ;;
  esac
done
for number in "$TIMEOUT" "$REPEAT" "$COOLDOWN"; do
  [[ $number =~ ^[0-9]+$ ]] || { echo "not a number: $number" >&2; exit 2; }
done
[[ $NAME =~ ^[A-Za-z0-9._+-]*$ ]] || { echo "a run's name is letters, digits and ._+-: $NAME" >&2; exit 2; }
#Everything written on the device goes in its folder, one level inside /data/local/tmp
if [[ ! $DEVICE =~ ^/data/local/tmp/[A-Za-z0-9._-]+$ || $DEVICE == */. || $DEVICE == */.. ]]; then
  echo "PHOBOS_DEVICE_DIR must be a folder of /data/local/tmp's own (/data/local/tmp/NAME): $DEVICE" >&2
  exit 2
fi
inside() { [[ ($1 == "$DEVICE" || $1 == "$DEVICE"/*) && $1 != *..* ]]; }

ADB=${ADB:-$(command -v adb || true)}
[[ -n $ADB ]] || ADB=$HOME/Library/Android/sdk/platform-tools/adb
if ((!DRY)) && [[ ! -x $ADB ]]; then echo "no adb: name it in ADB" >&2; exit 1; fi
if [[ -z $SERIAL ]]; then echo "no device named: -s SERIAL or ANDROID_SERIAL" >&2; exit 2; fi

#A word quoted for the device's shell.
q() { local s=${1//\'/\'\\\'\'}; printf "'%s'" "$s"; }
#adb, or with --dry-run what it would do (on standard error, whatever the output's redirected to); out() gives a
#command's output (none in a dry run).
dry() { { printf '+ adb -s %s' "$SERIAL"; printf ' %q' "$@"; printf '\n'; } >&2; }
run() {
  if ((DRY)); then dry "$@"; return 0; fi
  "$ADB" -s "$SERIAL" "$@"
}
out() {
  if ((DRY)); then dry "$@"; return 0; fi
  "$ADB" -s "$SERIAL" "$@" | tr -d '\r'
}
shell() { run shell "$*"; }
remove() {  #device folder: only inside the device's folder
  inside "$1" || { echo "refusing to remove $1 (outside $DEVICE)" >&2; exit 1; }
  shell "rm -rf $(q "$1")"
}
sha() {
  if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi
}

if ((!DRY)); then
  STATE=$("$ADB" -s "$SERIAL" get-state 2>&1 || true)
  [[ $STATE == device ]] || { echo "the device $SERIAL isn't there for adb ($STATE)" >&2; exit 1; }
fi

case $ACTION in
  list)
    shell "du -sh $(q "$DEVICE") $(q "$DEVICE")/* 2> /dev/null; ls $(q "$DEVICE/bin") $(q "$DEVICE/runs") 2> /dev/null"
    exit 0 ;;
  clean) remove "$DEVICE"; exit 0 ;;
  clean-runs) remove "$DEVICE/runs"; remove "$DEVICE/tmp"; exit 0 ;;
  clean-builds) remove "$DEVICE/bin"; exit 0 ;;
esac

(($# >= 2)) || usage
BUILD=$1 GAME=$2
shift 2

#The build: a folder, a binary in one, or a commit's (made now if it isn't yet)
if [[ -d $BUILD && -f $BUILD/psp-runner-android ]]; then
  LOCAL_BUILD=$(cd "$BUILD" && pwd)
elif [[ -f $BUILD ]]; then
  LOCAL_BUILD=$(cd "$(dirname "$BUILD")" && pwd)
else
  if ! COMMIT=$(git -C "$REPOSITORY" rev-parse --verify --quiet "$BUILD^{commit}"); then
    echo "not a build's folder, a binary, or a commit here: $BUILD" >&2
    exit 1
  fi
  LOCAL_BUILD=$BUILDS/${COMMIT:0:9}-$FLAVOR
  MEASURED=$(sed -n 's/^measure: //p' "$LOCAL_BUILD/build.txt" 2> /dev/null || true)
  STALE=0
  if [[ -f $LOCAL_BUILD/psp-runner-android && $MEASURED != "$(sha "$HERE/measure.hpp")" ]]; then
    echo "$(basename "$LOCAL_BUILD") was built with another measure.hpp than this checkout's: building it again" >&2
    STALE=1
  fi
  if [[ ! -f $LOCAL_BUILD/psp-runner-android ]] || ((STALE)); then
    if ((DRY)); then
      echo "+ $REPOSITORY/tools/psp-runner/build-android.sh $BUILD" >&2
    else
      LOCAL_BUILD=$(PHOBOS_ANDROID_OUT=$BUILDS PHOBOS_FLAVOR=$FLAVOR \
        "$REPOSITORY/tools/psp-runner/build-android.sh" "$BUILD" | tail -1)
    fi
  fi
fi
if ((!DRY)) || [[ -f $LOCAL_BUILD/build.txt ]]; then
  for file in psp-runner-android build.txt; do
    [[ -f $LOCAL_BUILD/$file ]] || { echo "$LOCAL_BUILD has no $file: not a build-android.sh build" >&2; exit 1; }
  done
  BINARY_SHA=$(sed -n 's/^sha256: //p' "$LOCAL_BUILD/build.txt")
  HAS_REPORT=$(sed -n 's/^report: //p' "$LOCAL_BUILD/build.txt")
  SOURCE=$(sed -n 's/^source: //p' "$LOCAL_BUILD/build.txt")
  if [[ -z ${COMMIT:-} && $(sed -n 's/^measure: //p' "$LOCAL_BUILD/build.txt") != "$(sha "$HERE/measure.hpp")" ]]
  then
    echo "warning: $(basename "$LOCAL_BUILD") was built with another measure.hpp than this checkout's: its" \
         "measurements may differ from other builds' (a commit given rather than a folder is built again)" >&2
  fi
else
  BINARY_SHA=unbuilt HAS_REPORT=yes SOURCE=unbuilt
fi
BUILD_NAME=$(basename "$LOCAL_BUILD")
if [[ ! $BUILD_NAME =~ ^[A-Za-z0-9._+-]+$ ]]; then
  echo "a build's folder is named in letters, digits and ._+-: $BUILD_NAME" >&2
  exit 1
fi
DEVICE_BUILD=$DEVICE/bin/$BUILD_NAME

#The game, read where it is (only read: the runner opens it read-only)
case $GAME in
  /*) CANDIDATES=("$GAME") ;;
  *.chd|*.iso|*.cso|*.zso|*.dax|*.jso|*.pbp|*.PBP|*.elf|*.prx)
    [[ -n $ROMS ]] || { echo "a game by its name needs PHOBOS_DEVICE_ROMS" >&2; exit 2; }
    CANDIDATES=("$ROMS/$GAME") ;;
  *)
    [[ -n $ROMS ]] || { echo "a game by its name needs PHOBOS_DEVICE_ROMS" >&2; exit 2; }
    CANDIDATES=("$ROMS/$GAME.chd" "$ROMS/$GAME.iso" "$ROMS/$GAME.cso") ;;
esac
DEVICE_GAME=
for candidate in "${CANDIDATES[@]}"; do
  if ((DRY)); then DEVICE_GAME=$candidate; break; fi
  if [[ $(out shell "[ -f $(q "$candidate") ] && [ -r $(q "$candidate") ] && echo yes") == yes ]]; then
    DEVICE_GAME=$candidate
    break
  fi
done
[[ -n $DEVICE_GAME ]] || { echo "no such game the shell can read on the device: ${CANDIDATES[*]}" >&2; exit 1; }

#The device's folder, and what each run needs there (each pushed once, kept by its hash)
shell "mkdir -p $(q "$DEVICE")/bin $(q "$DEVICE")/runs $(q "$DEVICE")/tmp $(q "$DEVICE")/states $(q "$DEVICE")/scripts"
pushed() {  #local file, device file: pushed unless the device has it with the same hash (a .sha256 beside it)
  local hash
  hash=$(sha "$1")
  if [[ $(out shell "cat $(q "$2.sha256") 2> /dev/null") != "$hash" ]]; then
    run push "$1" "$2" > /dev/null
    shell "echo $hash > $(q "$2.sha256")"
  fi
}
if [[ $(out shell "cat $(q "$DEVICE_BUILD/.sha256") 2> /dev/null") != "$BINARY_SHA" ]]; then
  echo "pushing $BUILD_NAME ($SOURCE)" >&2
  remove "$DEVICE_BUILD"
  shell "mkdir -p $(q "$DEVICE_BUILD/lib")"
  run push "$LOCAL_BUILD/psp-runner-android" "$DEVICE_BUILD/psp-runner-android" > /dev/null
  for library in "$LOCAL_BUILD"/lib/*.so; do
    [[ -f $library ]] && run push "$library" "$DEVICE_BUILD/lib/" > /dev/null
  done
  run push "$LOCAL_BUILD/build.txt" "$DEVICE_BUILD/build.txt" > /dev/null
  shell "chmod 755 $(q "$DEVICE_BUILD/psp-runner-android") && echo $BINARY_SHA > $(q "$DEVICE_BUILD/.sha256")"
fi
pushed "$HERE/device-info.sh" "$DEVICE/device-info.sh"

#The runner's options: files here pushed, outputs in the run's folder, the device's paths kept inside its folder.
#RUN_FOLDER stands for the run's folder until each run's is known.
RUN_FOLDER=@RUN@
ARGS=() GIVEN_FONTS=0 GIVEN_OUT=0 WANTS_OUT=0 GIVEN_REPORT=0 RENDERER=software
devicePath() {  #option, value: a device path inside the folder, or a bare name in the folder's place for it
  local place=$1 value=$2
  if [[ $value != */* ]]; then echo "$DEVICE/$place/$value"; return; fi
  inside "$value" || { echo "$value: a device path for this must be in $DEVICE (or a bare name)" >&2; exit 2; }
  echo "$value"
}
pushFile() {  #local file, device folder: the device's path for it
  [[ -f $1 ]] || { echo "no such file here: $1" >&2; exit 2; }
  local name hash
  name=$(basename "$1")
  name=${name//[^A-Za-z0-9._+-]/_}
  hash=$(sha "$1")
  pushed "$1" "$DEVICE/$2/${hash:0:12}-$name" >&2
  echo "$DEVICE/$2/${hash:0:12}-$name"
}
while (($#)); do
  option=$1
  case $option in
    --load-state|--script)
      (($# >= 2)) || { echo "$option needs a value" >&2; exit 2; }
      if inside "$2"; then ARGS+=("$option" "$2")
      else ARGS+=("$option" "$(pushFile "$2" "$([[ $option == --script ]] && echo scripts || echo states)")"); fi
      shift 2 ;;
    --out) GIVEN_OUT=1; ARGS+=(--out "$RUN_FOLDER"); shift 2 ;;
    --png-every|--png-at) WANTS_OUT=1; ARGS+=("$option" "$2"); shift 2 ;;
    --wav) ARGS+=(--wav "$RUN_FOLDER/$(basename "$2")"); shift 2 ;;
    --report) GIVEN_REPORT=1; ARGS+=(--report "$RUN_FOLDER/report.json"); shift 2 ;;
    --save-state-at) ARGS+=(--save-state-at "$2" "$RUN_FOLDER/$(basename "$3")"); shift 3 ;;
    --memory-stick) ARGS+=(--memory-stick "$(devicePath memory-sticks "$2")"); shift 2 ;;
    --pipeline-cache) ARGS+=(--pipeline-cache "$(devicePath cache "$2")"); shift 2 ;;
    --fonts) GIVEN_FONTS=1; ARGS+=(--fonts "$(devicePath fonts "$2")"); shift 2 ;;
    --renderer) RENDERER=$2; ARGS+=("$option" "$2"); shift 2 ;;
    *) ARGS+=("$option"); shift ;;
  esac
done
if ((WANTS_OUT && !GIVEN_OUT)); then ARGS+=(--out "$RUN_FOLDER"); fi
if ((!GIVEN_REPORT)) && [[ $HAS_REPORT == yes ]]; then ARGS+=(--report "$RUN_FOLDER/report.json"); fi
if ((USE_FONTS && !GIVEN_FONTS)); then
  [[ -n $FONTS && -d $FONTS ]] || { echo "no fonts' folder here (PHOBOS_FONTS), or --no-fonts" >&2; exit 2; }
  FONT_HASH=$(cd "$FONTS" && ls -l ./*.pgf | awk '{print $5, $NF}' | shasum -a 256 | cut -c1-64)
  if [[ $(out shell "cat $(q "$DEVICE/fonts/.sha256") 2> /dev/null") != "$FONT_HASH" ]]; then
    echo "pushing the fonts from $FONTS" >&2
    remove "$DEVICE/fonts"
    shell "mkdir -p $(q "$DEVICE/fonts")"
    for font in "$FONTS"/*.pgf; do run push "$font" "$DEVICE/fonts/" > /dev/null; done
    shell "echo $FONT_HASH > $(q "$DEVICE/fonts/.sha256")"
  fi
  ARGS+=(--fonts "$DEVICE/fonts")
fi

#security.perf_harden, if asked: 0 for the runs, then what it was
HARDEN_WAS=
restoreHarden() {
  if [[ -n $HARDEN_WAS ]]; then
    shell "setprop security.perf_harden $(q "$HARDEN_WAS")" || true
    echo "security.perf_harden set back to $HARDEN_WAS" >&2
    HARDEN_WAS=
  fi
}
trap restoreHarden EXIT
trap 'exit 130' INT TERM
if ((HARDEN)); then
  WAS=$(out shell "getprop security.perf_harden")
  if [[ $WAS != 0 ]] || ((DRY)); then
    HARDEN_WAS=${WAS:-1}
    shell "setprop security.perf_harden 0"
    echo "security.perf_harden set to 0 for the runs (it was ${WAS:-unset})" >&2
  fi
fi

slug() { local s=${1//[^A-Za-z0-9]/-}; s=${s//--/-}; echo "${s:0:40}"; }
GAME_NAME=$(basename "$DEVICE_GAME")
BASE_NAME=${NAME:-$BUILD_NAME-$(slug "${GAME_NAME%.*}")-$(slug "$RENDERER")-$(date '+%m%d-%H%M%S')}
mkdir -p "$RUNS"
for ((n = 1; n <= REPEAT; n++)); do
  RUN_NAME=$BASE_NAME
  ((REPEAT == 1)) || RUN_NAME=$BASE_NAME-$n
  RUN=$DEVICE/runs/$RUN_NAME
  LOCAL_RUN=$RUNS/$RUN_NAME
  if [[ -e $LOCAL_RUN ]] && ((!DRY)); then echo "$LOCAL_RUN is there already: another --name" >&2; exit 1; fi
  if ((n > 1 && COOLDOWN > 0)); then echo "cooling down for $COOLDOWN s" >&2; ((DRY)) || sleep "$COOLDOWN"; fi
  #the device's script for the run, kept with its results
  SCRIPT=$(mktemp "${TMPDIR:-/tmp}/run-on-device.XXXXXX")
  {
    echo "#!/system/bin/sh"
    echo "#run-on-device.sh, $(date '+%Y-%m-%d %H:%M:%S %z'): $SOURCE ($BUILD_NAME)"
    echo "DEVICE=$(q "$DEVICE")"
    echo "RUN=$(q "$RUN")"
    echo "BUILD=$(q "$DEVICE_BUILD")"
    echo 'cd "$RUN" || exit 1'
    echo '#(what a run cut short left of the runner'\''s own scratch)'
    echo 'rm -rf "$DEVICE"/tmp/phobos-psp-runner-*'
    echo 'sh "$DEVICE/device-info.sh" > "$RUN/device-before.txt" 2>&1'
    echo 'export TMPDIR="$DEVICE/tmp"'
    echo 'export PHOBOS_MEASURE_JSON="$RUN/measure.json"'
    echo 'export LD_LIBRARY_PATH="$BUILD/lib"'
    for variable in ${ENVIRONMENT[@]+"${ENVIRONMENT[@]}"}; do echo "export ${variable%%=*}=$(q "${variable#*=}")"; done
    printf 'timeout -k 10 %s "$BUILD/psp-runner-android" %s' "$TIMEOUT" "$(q "$DEVICE_GAME")"
    for argument in ${ARGS[@]+"${ARGS[@]}"}; do
      argument=${argument//$RUN_FOLDER/$RUN}
      printf ' %s' "$(q "$argument")"
    done
    printf ' > "$RUN/stdout.log" 2> "$RUN/stderr.log"\n'
    echo 'echo $? > "$RUN/exit-code"'
    echo 'sh "$DEVICE/device-info.sh" > "$RUN/device-after.txt" 2>&1'
    echo '#(SELinux'\''s refusals of the runner, if any: executable memory, perf events)'
    echo 'logcat -d -b all 2> /dev/null | grep "avc: *denied" | grep "psp-runner" | tail -50 \'
    echo '  > "$RUN/selinux-denials.txt"'
    echo 'cat "$RUN/exit-code"'
  } > "$SCRIPT"
  echo "== $RUN_NAME: $SOURCE, $GAME_NAME" >&2
  if ((DRY)); then
    echo "+ the device's script ($RUN/run.sh):" >&2
    sed 's/^/    /' "$SCRIPT" >&2
  fi
  remove "$RUN"
  shell "mkdir -p $(q "$RUN")"
  run push "$SCRIPT" "$RUN/run.sh" > /dev/null
  rm -f "$SCRIPT"
  STARTED=$(date +%s)
  run shell "sh $(q "$RUN/run.sh")" > /dev/null || true
  run pull "$RUN" "$RUNS/" > /dev/null
  ((DRY)) && continue
  CODE=$(cat "$LOCAL_RUN/exit-code" 2> /dev/null || echo "none")
  echo "-- $RUN_NAME: exit $CODE after $(($(date +%s) - STARTED)) s; $LOCAL_RUN"
  sed -n '/^--$/,$p' "$LOCAL_RUN/stdout.log" 2> /dev/null | sed 1d
  if [[ $CODE != 0 ]]; then
    echo "(stderr's end:)"
    tail -15 "$LOCAL_RUN/stderr.log" 2> /dev/null || true
  fi
done
