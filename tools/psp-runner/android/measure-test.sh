#!/usr/bin/env bash
#Tests android/measure.hpp on this host: a program that works a while each "frame", with the measurements' calls where
#the runner has them, run twice (pinned to the first CPU, and not pinned), its JSON checked: the frames and their
#times, the thread's CPU time, and on Linux what only Linux has (the CPU each frame ran on, the pinning, the threads'
#times from /proc). The counters are said, not required: a VM, or perf_event_paranoid, may have none. Where there are
#counters, it runs again with 4 and then 5 of the PMU's counters taken first, as a phone's kernel keeps some for
#itself: each extra event (stalls, mispredicts, refills) is then counted in turns and scaled, or not counted at all,
#and never a zero it didn't count.
#usage: tools/psp-runner/android/measure-test.sh   (CXX: the compiler, c++ by default; needs jq)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/measure-test.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
cat > "$SCRATCH/test.cpp" << 'EOF'
#include <atomic>
#include <cmath>
#include <thread>
#if defined(__linux__)
#include <pthread.h>
#endif
int main() {
  int frames = std::getenv("TEST_FRAMES") ? std::atoi(std::getenv("TEST_FRAMES")) : 90;
  #if defined(__linux__)
  //(TEST_TAKE_COUNTERS: that many of the PMU's counters taken first, pinned, for this thread's whole run)
  int take = std::getenv("TEST_TAKE_COUNTERS") ? std::atoi(std::getenv("TEST_TAKE_COUNTERS")) : 0;
  for(int n = 0; n < take; n++) {
    perf_event_attr attr{};
    attr.size = sizeof(attr), attr.type = PERF_TYPE_HARDWARE, attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.pinned = 1, attr.exclude_kernel = 1, attr.exclude_hv = 1;
    syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
  }
  #endif
  volatile double sink = 0, other = 0;
  std::atomic<bool> done{false};
  //(a second thread, busy until the window's end, for the threads' times; with TEST_GE_THREAD named as the PSP's GE
  //thread is, for PHOBOS_GE_PLACE)
  std::thread helper([&] { while(!done) other = other + 1; });
  #if defined(__linux__)
  if(std::getenv("TEST_GE_THREAD")) pthread_setname_np(helper.native_handle(), "psp-ge-thread");
  #endif
  phobos_measure::begin();
  for(int frame = 0; frame < frames; frame++) {
    for(int i = 0; i < 300'000; i++) sink = sink + std::sqrt(double(i));
    phobos_measure::frame();
  }
  phobos_measure::end();
  done = true;
  helper.join();
  std::printf("--\nframes run: %d\n", frames);
}
EOF
${CXX:-c++} -std=c++20 -O1 -Wall -Wextra -Werror -pthread -include "$HERE/measure.hpp" "$SCRATCH/test.cpp" \
  -o "$SCRATCH/test"
fail() { echo "measure-test.sh: $*" >&2; cat "$SCRATCH/out.txt" >&2; exit 1; }
JSON=$SCRATCH/measure.json
for pin in 0 none; do
  PHOBOS_PIN=$pin PHOBOS_FREQ_EVERY=10 PHOBOS_MEASURE_JSON="$JSON" "$SCRATCH/test" > "$SCRATCH/out.txt"
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
  echo "PHOBOS_PIN=$pin:"
  grep -E '^measure (window|emulation|cpus|pinning|counter|threads)' "$SCRATCH/out.txt" | sed 's/^/  /'
done
#The app's placement (PHOBOS_GE_PLACE): with two cores or more ranked by capacity, every frame the GE's thread (the
#helper, named so) on the fastest and this thread on the next, and left there; with fewer (a VM with no capacities
#or clocks), nothing placed, and the app's pinning as without it.
if [[ $(uname -s) == Linux ]]; then
  TEST_GE_THREAD=1 PHOBOS_GE_PLACE=1 PHOBOS_MEASURE_JSON="$JSON" "$SCRATCH/test" > "$SCRATCH/out.txt"
  grep -q '^measure ge placement: ' "$SCRATCH/out.txt" || fail "no placement line"
  if jq -e '."ge placement".cores | length >= 2' "$JSON" > /dev/null; then
    jq -e '."ge placement" as $p | $p."placed frames" == 90 and $p."ge core" == $p.cores[0] and
           $p."emulation core" == $p.cores[1] and ."ge thread"."affinity at end" == [$p.cores[0]] and
           .pin."affinity at end" == [$p.cores[1]]' "$JSON" > /dev/null || fail "not placed"
  else
    jq -e '."ge placement"."placed frames" == 0' "$JSON" > /dev/null || fail "placed without cores ranked"
  fi
  echo "PHOBOS_GE_PLACE=1: $(grep '^measure ge placement' "$SCRATCH/out.txt")"
fi
#An extra event counted for some of the time has its share (above 0, at most 1) and a total; one never counted has
#neither, and the line says so.
extras_said() {
  jq -e '.counters.counted | all(.[]; . > 0 and . <= 1)' "$JSON" > /dev/null || fail "a share outside 0 to 1"
  jq -e '.counters as $c | $c."not counted" | all(.[]; $c.total[.] == null and $c.counted[.] == null)' "$JSON" \
    > /dev/null || fail "an event not counted has a total or a share"
  if jq -e '.counters."not counted" | length > 0' "$JSON" > /dev/null; then
    grep -q '; not counted: ' "$SCRATCH/out.txt" || fail "the events not counted aren't said"
  elif grep -q '; not counted: ' "$SCRATCH/out.txt"; then
    fail "events said not counted that were"
  fi
}
extras_said
if [[ $(uname -s) == Linux ]] && jq -e '.counters.available' "$JSON" > /dev/null; then
  for take in 4 5; do
    TEST_TAKE_COUNTERS=$take TEST_FRAMES=300 PHOBOS_PIN=none PHOBOS_MEASURE_JSON="$JSON" "$SCRATCH/test" \
      > "$SCRATCH/out.txt"
    jq -e '.counters.available' "$JSON" > /dev/null ||
      echo "with $take counters taken: no instructions or cycles (this PMU has too few counters for them too)"
    extras_said
    echo "with $take counters taken: $(jq -c '{counted: .counters.counted, "not counted": .counters."not counted"}' \
      "$JSON")"
    grep -E '^measure counters' "$SCRATCH/out.txt" | sed 's/^/  /'
  done
fi
echo "measure-test.sh: passed"
