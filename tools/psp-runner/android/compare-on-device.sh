#!/usr/bin/env bash
#Builds compared on a device: run-on-device.sh runs each in turn, round after round (so the device's warming spreads
#over all of them alike), and runs-table.sh tables the runs.
#usage: compare-on-device.sh [--rounds N] [--label PREFIX] [run-on-device.sh's options] BUILD... -- GAME \
#         [runner options]
#  BUILD: as run-on-device.sh takes it (a build's folder, a binary, or a commit, built first if it isn't yet)
#  --rounds N: rounds of every build (3); --label PREFIX: the runs' names begin with it (the time otherwise); each run
#  is PREFIX-BUILD-rROUND
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROUNDS=3
LABEL=compare-$(date '+%m%d-%H%M%S')
OPTIONS=()
while (($#)); do
  case $1 in
    --rounds) ROUNDS=$2; shift 2 ;;
    --label) LABEL=$2; shift 2 ;;
    -s|--serial|--timeout|--cooldown|--pin|--nice) OPTIONS+=("$1" "$2"); shift 2 ;;
    --no-perf|--perf-harden-off|--no-fonts|--dry-run) OPTIONS+=("$1"); shift ;;
    --repeat|--name) echo "compare-on-device.sh names and repeats the runs itself (--rounds, --label)" >&2; exit 2 ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^#//' >&2; exit 2 ;;
    -*) echo "no such option: $1" >&2; exit 2 ;;
    *) break ;;
  esac
done
BUILDS_GIVEN=()
while (($#)) && [[ $1 != -- ]]; do BUILDS_GIVEN+=("$1"); shift; done
(($# >= 2)) && [[ $1 == -- ]] || { echo "usage: $0 [options] BUILD... -- GAME [runner options]" >&2; exit 2; }
shift
[[ $ROUNDS =~ ^[1-9][0-9]*$ ]] || { echo "--rounds takes a number: $ROUNDS" >&2; exit 2; }
((${#BUILDS_GIVEN[@]})) || { echo "no builds to compare" >&2; exit 2; }
RUNS=${PHOBOS_RUNS:-${TMPDIR:-/tmp}/phobos-psp-runner-android/runs}
FOLDERS=()
for ((round = 1; round <= ROUNDS; round++)); do
  for build in "${BUILDS_GIVEN[@]}"; do
    name=$(basename "$build")
    [[ $name == psp-runner-android ]] && name=$(basename "$(dirname "$build")")
    name=$LABEL-${name//[^A-Za-z0-9._+-]/_}-r$round
    "$HERE/run-on-device.sh" ${OPTIONS[@]+"${OPTIONS[@]}"} --name "$name" "$build" "$@" || true
    FOLDERS+=("$RUNS/$name")
  done
done
EXISTING=()
for folder in "${FOLDERS[@]}"; do [[ -d $folder ]] && EXISTING+=("$folder"); done
((${#EXISTING[@]})) && "$HERE/runs-table.sh" "${EXISTING[@]}"
