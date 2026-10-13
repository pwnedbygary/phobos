#!/usr/bin/env bash
#Checks tests/objects.sh, which the host tests' build scripts keep their objects with, on a small C program built in a
#scratch folder, counting the compiles each build makes and running the program it links: built cold, then nothing
#compiled; a header it includes changed: that object alone again, and the program sees the change; a define changed:
#everything again, and back: nothing (the first objects kept); another compiler version: everything; a compile cut
#short: nothing kept from it; a header found first on the include path gone: compiled again with the next one; two
#checkouts built at once into one folder: each its own objects and program.
#usage: tests/objects-test.sh   (CC: the compiler, cc by default)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/phobos-objects-test.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
export CC=${CC:-cc} OBJECTS_SH=$HERE/objects.sh OUT=$SCRATCH/out
failures=0

#a checkout: main.c (including a.h, and c.h from first/, else from second/) and other.c, and its build script, which
#compiles both with EXTRA's flags and runs the program
checkout() {  #folder message
  mkdir -p "$1/first" "$1/second"
  printf '#define VALUE 1\n' > "$1/a.h"
  printf '#define WHICH 2\n' > "$1/second/c.h"
  printf '#define WHICH 1\n' > "$1/first/c.h"
  printf '#include <stdio.h>\n#include "a.h"\n#include <c.h>\nint other(void);\n#ifndef EXTRA\n#define EXTRA 0\n#endif\n' > "$1/main.c"
  printf 'int main(void) { printf("%s %%d %%d %%d %%d\\n", VALUE, WHICH, EXTRA, other()); return 0; }\n' "$2" >> "$1/main.c"
  printf 'int other(void) { return 7; }\n' > "$1/other.c"
  cat > "$1/build.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
source "$OBJECTS_SH"
# shellcheck disable=SC2086
object "$CC" "$ROOT/main.c" -O1 -I"$ROOT/first" -I"$ROOT/second" ${EXTRA:-}
# shellcheck disable=SC2086
object "$CC" "$ROOT/other.c" -O1 ${EXTRA:-}
objects_done
$CC "${OBJECTS[@]}" -o "$BUILD/program.part-$$"
mv -f "$BUILD/program.part-$$" "$BUILD/program"
"$BUILD/program"
EOF
  chmod +x "$1/build.sh"
}

#a build: its compiles counted, its program's output kept
build() {  #folder [compiler]
  local log=$SCRATCH/build.log
  #(in a subshell whose own errors go nowhere: the killed build's "Killed" notice; its exit keeps it from becoming the
  #build itself)
  if (CC=${2:-$CC} "$1/build.sh" > "$log" 2>&1; exit $?) 2> /dev/null; then built=ok; else built=failed; fi
  compiles=$(grep -c '^  compiling ' "$log" || true)
  said=$(tail -1 "$log")
}

expect() {  #what condition
  if eval "$2"; then
    echo "pass $1"
  else
    echo "FAIL $1: built $built, $compiles compiles, the program said \"$said\""
    failures=$((failures + 1))
  fi
}

A=$SCRATCH/checkout-a
checkout "$A" a
build "$A"
expect "cold: both compiled" '[[ $built == ok && $compiles == 2 && $said == "a 1 1 0 7" ]]'
build "$A"
expect "warm: nothing compiled" '[[ $built == ok && $compiles == 0 && $said == "a 1 1 0 7" ]]'

sleep 1  #(some file systems keep modification times in whole seconds)
printf '#define VALUE 5\n' > "$A/a.h"
build "$A"
expect "a header changed: its includer alone compiled, the change seen" \
  '[[ $built == ok && $compiles == 1 && $said == "a 5 1 0 7" ]]'

EXTRA=-DEXTRA=3 build "$A"
expect "a define added: both compiled" '[[ $built == ok && $compiles == 2 && $said == "a 5 1 3 7" ]]'
build "$A"
expect "the define taken away: nothing compiled (the first objects kept)" \
  '[[ $built == ok && $compiles == 0 && $said == "a 5 1 0 7" ]]'

#another compiler version: the same compiler behind a script that reports another --version
mkdir -p "$SCRATCH/tools"
printf '#!/bin/sh\nif [ "$1" = --version ]; then echo "another compiler 99"; exit 0; fi\nexec %s "$@"\n' "$CC" \
  > "$SCRATCH/tools/other-cc"
chmod +x "$SCRATCH/tools/other-cc"
build "$A" "$SCRATCH/tools/other-cc"
expect "another compiler version: both compiled" '[[ $built == ok && $compiles == 2 && $said == "a 5 1 0 7" ]]'

#a compile cut short: the compiler writes its list of what it read and half an object where it's told, then the build
#is killed with it (as a cancelled job or ^C kills it), so nothing of the build's own cleans up after it
printf '#!/bin/sh\nif [ "$1" = --version ]; then echo "a killed compiler"; exit 0; fi\n' > "$SCRATCH/tools/killed-cc"
printf 'while [ $# -gt 0 ]; do case "$1" in -MF) list=$2 ;; -o) object=$2 ;; -c) source=$2 ;; esac; shift; done\n' \
  >> "$SCRATCH/tools/killed-cc"
printf 'echo "x: $source" > "$list"; echo half > "$object"; kill -9 $PPID $$\n' >> "$SCRATCH/tools/killed-cc"
chmod +x "$SCRATCH/tools/killed-cc"
EXTRA=-DEXTRA=4 build "$A" "$SCRATCH/tools/killed-cc"
expect "a compile cut short: the build fails" '[[ $built == failed ]]'
#(the same command once more, now with a compiler that works and calls itself the killed one)
printf '#!/bin/sh\nif [ "$1" = --version ]; then echo "a killed compiler"; exit 0; fi\nexec %s "$@"\n' "$CC" \
  > "$SCRATCH/tools/killed-cc"
EXTRA=-DEXTRA=4 build "$A" "$SCRATCH/tools/killed-cc"
expect "after it, the same command: compiled, nothing kept from the cut" \
  '[[ $built == ok && $compiles == 2 && $said == "a 5 1 4 7" ]]'

sleep 1
rm "$A/first/c.h"
build "$A"
expect "a header gone: compiled again, the next one on the include path taken" \
  '[[ $built == ok && $compiles == 1 && $said == "a 5 2 0 7" ]]'

#two checkouts at once, into the same folder
B=$SCRATCH/checkout-b
C=$SCRATCH/checkout-c
checkout "$B" b
checkout "$C" c
("$B/build.sh" > "$SCRATCH/b.log" 2>&1; echo $? > "$SCRATCH/b.status") &
("$C/build.sh" > "$SCRATCH/c.log" 2>&1; echo $? > "$SCRATCH/c.status") &
wait
built="$(cat "$SCRATCH/b.status") $(cat "$SCRATCH/c.status")" compiles="?" said="$(tail -1 "$SCRATCH/b.log") / $(tail -1 "$SCRATCH/c.log")"
expect "two checkouts at once: each built, each its own program" '[[ $built == "0 0" && $said == "b 1 1 0 7 / c 1 1 0 7" ]]'
folders=$(find "$OUT" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
expect "two checkouts at once: a folder each (and checkout a's)" '[[ $folders == 3 ]]'

echo "$failures failure$([[ $failures == 1 ]] || echo s)"
((failures == 0))
