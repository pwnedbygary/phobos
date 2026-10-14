#!/usr/bin/env bash
#Tests android/measure.hpp on this host: a program that works a while each "frame", with the measurements' calls where
#the runner has them, run twice (pinned to the first CPU, and not pinned), its JSON checked: the frames and their
#times, the thread's CPU time, and on Linux what only Linux has (the CPU each frame ran on, the pinning, the threads'
#times from /proc). The counters are said, not required: a VM, or perf_event_paranoid, may have none.
#usage: tools/psp-runner/android/measure-test.sh   (CXX: the compiler, c++ by default; needs jq)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/measure-test.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
cat > "$SCRATCH/test.cpp" << 'EOF'
#include <cmath>
#include <thread>
int main() {
  volatile double sink = 0, other = 0;
  std::thread helper([&] { for(int i = 0; i < 20'000'000; i++) other = other + 1; });
  phobos_measure::begin();
  for(int frame = 0; frame < 90; frame++) {
    for(int i = 0; i < 300'000; i++) sink = sink + std::sqrt(double(i));
    phobos_measure::frame();
  }
  phobos_measure::end();
  helper.join();
  std::printf("--\nframes run: 90\n");
}
EOF
${CXX:-c++} -std=c++20 -O1 -Wall -Wextra -Werror -pthread -include "$HERE/measure.hpp" "$SCRATCH/test.cpp" \
  -o "$SCRATCH/test"
fail() { echo "measure-test.sh: $*" >&2; cat "$SCRATCH/out.txt" >&2; exit 1; }
for pin in 0 none; do
  PHOBOS_PIN=$pin PHOBOS_FREQ_EVERY=10 PHOBOS_MEASURE_JSON="$SCRATCH/measure.json" "$SCRATCH/test" > "$SCRATCH/out.txt"
  JSON=$SCRATCH/measure.json
  [[ $(jq -r '.measure' "$JSON") == "phobos-psp-runner-measure 1" ]] || fail "no JSON"
  [[ $(jq '.window.frames' "$JSON") == 90 ]] || fail "not 90 frames"
  jq -e '.window.seconds > 0 and ."emulation thread"."cpu seconds" > 0 and ."frame times"."mean ms" > 0' "$JSON" \
    > /dev/null || fail "no times"
  grep -q '^measure window: 90 frames' "$SCRATCH/out.txt" || fail "no summary line"
  if [[ $(uname -s) == Linux ]]; then
    jq -e '."frames by cpu" | add == 90' "$JSON" > /dev/null || fail "the frames' CPUs don't add up to 90"
    jq -e '.threads.list | length >= 2' "$JSON" > /dev/null || fail "no threads from /proc"
    jq -e '."emulation thread"."waiting seconds" != null and ."emulation thread".rusage == "thread"' "$JSON" \
      > /dev/null || fail "no schedstat or per-thread usage"
    if [[ $pin == 0 ]]; then
      jq -e '.pin."affinity at end" == [0] and ."frames by cpu" == {"0": 90} and .pin.refused == 0' "$JSON" \
        > /dev/null || fail "not kept to CPU 0"
    fi
  fi
  echo "PHOBOS_PIN=$pin: $(grep -E '^measure (window|counters|cpus)' "$SCRATCH/out.txt" | tr '\n' ' ')"
done
echo "measure-test.sh: passed"
