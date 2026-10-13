#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034
#Checks tests/objects.sh, which the host tests' build scripts keep their objects with, on a small C program built in a
#scratch folder, counting the compiles each build makes and running the program it links: built cold, then nothing
#compiled; a header it includes changed: that object alone again, and the program sees the change, a system header
#(-isystem) too; a define changed: everything again, and back: nothing (the first objects kept); the same compiler
#reporting another version, or named with another word (CC="cc -DX"): everything; a header saved while its includer
#compiled: compiled again next time; a header changed within the second of the build, where times have fractions;
#a compile cut short: nothing kept from it; a header found first on the include path gone: compiled again with the
#next one; a compile failing among others at once (PHOBOS_JOBS=2): the build fails; a header found through CPATH,
#then another CPATH: compiled again; a program for each configuration; two checkouts built at once into one folder:
#each its own objects and program.
#usage: tests/objects-test.sh   (CC: the compiler, cc by default)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/phobos-objects-test.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
export CC=${CC:-cc} OBJECTS_SH=$HERE/objects.sh OUT=$SCRATCH/out
REAL_CC=$CC
failures=0

#a checkout: main.c (including a.h, c.h from first/ else second/, and s.h from sys/ as a system header) and other.c,
#and its build script, which compiles both with EXTRA's flags and runs the program
checkout() {  #folder message
  mkdir -p "$1/first" "$1/second" "$1/sys"
  printf '#define VALUE 1\n' > "$1/a.h"
  printf '#define WHICH 2\n' > "$1/second/c.h"
  printf '#define WHICH 1\n' > "$1/first/c.h"
  printf '#define SYSTEM 1\n' > "$1/sys/s.h"
  printf '#include <stdio.h>\n#include "a.h"\n#include <c.h>\n#include <s.h>\nint other(void);\n' > "$1/main.c"
  printf '#ifndef EXTRA\n#define EXTRA 0\n#endif\n' >> "$1/main.c"
  printf 'int main(void) { printf("%s %%d %%d %%d %%d %%d\\n", VALUE, WHICH, EXTRA, SYSTEM, other()); return 0; }\n' \
    "$2" >> "$1/main.c"
  printf 'int other(void) { return 7; }\n' > "$1/other.c"
  cat > "$1/build.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
source "$OBJECTS_SH"
# shellcheck disable=SC2086
object "$CC" "$ROOT/main.c" -O1 -I"$ROOT/first" -I"$ROOT/second" -isystem "$ROOT/sys" ${EXTRA:-}
# shellcheck disable=SC2086
object "$CC" "$ROOT/other.c" -O1 ${EXTRA:-}
objects_done
objects_program program
$CC "${OBJECTS[@]}" -o "$REPLY.part-$$"
mv -f "$REPLY.part-$$" "$REPLY"
"$REPLY"
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
sleep 1  #(the sources older than any object, where times are whole seconds)
build "$A"
expect "cold: both compiled" '[[ $built == ok && $compiles == 2 && $said == "a 1 1 0 1 7" ]]'
build "$A"
expect "warm: nothing compiled" '[[ $built == ok && $compiles == 0 && $said == "a 1 1 0 1 7" ]]'

sleep 1  #(some file systems keep modification times in whole seconds)
printf '#define VALUE 5\n' > "$A/a.h"
build "$A"
expect "a header changed: its includer alone compiled, the change seen" \
  '[[ $built == ok && $compiles == 1 && $said == "a 5 1 0 1 7" ]]'

sleep 1
printf '#define SYSTEM 3\n' > "$A/sys/s.h"
build "$A"
expect "a system header (-isystem) changed: its includer compiled, the change seen" \
  '[[ $built == ok && $compiles == 1 && $said == "a 5 1 0 3 7" ]]'

EXTRA=-DEXTRA=3 build "$A"
expect "a define added: both compiled" '[[ $built == ok && $compiles == 2 && $said == "a 5 1 3 3 7" ]]'
build "$A"
expect "the define taken away: nothing compiled (the first objects kept)" \
  '[[ $built == ok && $compiles == 0 && $said == "a 5 1 0 3 7" ]]'

build "$A" "$CC -DEXTRA=9"
expect "the compiler named with another word: both compiled, the word taken" \
  '[[ $built == ok && $compiles == 2 && $said == "a 5 1 9 3 7" ]]'

#the same compiler, at the same path, reporting another version (the compiler behind a script whose --version is a
#file's contents)
mkdir -p "$SCRATCH/tools"
echo "test compiler 1" > "$SCRATCH/tools/version"
printf '#!/bin/sh\nif [ "$1" = --version ]; then cat "%s"; exit 0; fi\nexec %s "$@"\n' "$SCRATCH/tools/version" \
  "$REAL_CC" > "$SCRATCH/tools/versioned-cc"
chmod +x "$SCRATCH/tools/versioned-cc"
build "$A" "$SCRATCH/tools/versioned-cc"
expect "a compiler behind a script: both compiled" '[[ $built == ok && $compiles == 2 ]]'
echo "test compiler 2" > "$SCRATCH/tools/version"
build "$A" "$SCRATCH/tools/versioned-cc"
expect "the same compiler reporting another version: both compiled" '[[ $built == ok && $compiles == 2 ]]'

#a header saved while its includer compiled: the compiler, having read it, holds before it writes the object (HOLD
#set) while a.h changes, so the object is written after the change; the next build, the same compiler not holding,
#compiles main.c again and the program sees the change
cat > "$SCRATCH/tools/holding-cc" <<EOF
#!/usr/bin/env bash
if [[ \$1 == --version ]]; then echo "a holding compiler"; exit 0; fi
args=(); out=
while ((\$#)); do if [[ \$1 == -o ]]; then out=\$2; args+=(-o "\$out.held"); shift 2; else args+=("\$1"); shift; fi; done
$REAL_CC "\${args[@]}" || exit \$?
if [[ -n \${HOLD:-} ]]; then
  touch "\$HOLD/compiled"
  n=0; while [[ ! -f \$HOLD/go && \$n -lt 300 ]]; do sleep 0.1; n=\$((n + 1)); done
fi
cp "\$out.held" "\$out" && rm -f "\$out.held"
EOF
chmod +x "$SCRATCH/tools/holding-cc"
mkdir -p "$SCRATCH/hold"
(HOLD=$SCRATCH/hold build "$A" "$SCRATCH/tools/holding-cc"; echo "$built" > "$SCRATCH/hold/built") &
n=0; while [[ ! -f $SCRATCH/hold/compiled && $n -lt 300 ]]; do sleep 0.1; n=$((n + 1)); done
printf '#define VALUE 6\n' > "$A/a.h"
touch "$SCRATCH/hold/go"
wait
build "$A" "$SCRATCH/tools/holding-cc"
expect "a header saved while its includer compiled: compiled again next time, the change seen" \
  '[[ $(cat "$SCRATCH/hold/built") == ok && $built == ok && $compiles == 1 && $said == "a 6 1 0 3 7" ]]'

#a header changed within the second the object was made, where the file system keeps fractions of a second: the
#object's time and the header's pinned at .2 and .7 of one second past (a whole-second comparison takes them as equal)
touch "$SCRATCH/early"; sleep 0.05; touch "$SCRATCH/late"
if [[ -n $(find "$SCRATCH/late" -newer "$SCRATCH/early") ]]; then
  build "$A"
  printf '#define VALUE 7\n' > "$A/a.h"
  second=$(date +%Y-%m-%dT%H:%M:%S)
  sleep 1
  for object in "$OUT"/*/objects/main-*.o; do touch -d "$second.2" "$object"; done
  touch -d "$second.7" "$A/a.h"
  build "$A"
  expect "a header changed within the second: compiled again, the change seen" \
    '[[ $built == ok && $compiles == 1 && $said == "a 7 1 0 3 7" ]]'
else
  echo "skip a header changed within the second: this file system keeps whole seconds"
  sleep 1
  printf '#define VALUE 7\n' > "$A/a.h"  #(as the case leaves it, for the cases after)
fi

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
printf '#!/bin/sh\nif [ "$1" = --version ]; then echo "a killed compiler"; exit 0; fi\nexec %s "$@"\n' "$REAL_CC" \
  > "$SCRATCH/tools/killed-cc"
EXTRA=-DEXTRA=4 build "$A" "$SCRATCH/tools/killed-cc"
expect "after it, the same command: compiled, nothing kept from the cut" \
  '[[ $built == ok && $compiles == 2 && $said == "a 7 1 4 3 7" ]]'

sleep 1
rm "$A/first/c.h"
build "$A"
expect "a header gone: compiled again, the next one on the include path taken" \
  '[[ $built == ok && $compiles == 1 && $said == "a 7 2 0 3 7" ]]'

#a compile failing among others at once: the build fails rather than linking what was there
sleep 1
cp "$A/other.c" "$SCRATCH/other.c"
printf 'int other(void) { return 7 }\n' > "$A/other.c"
PHOBOS_JOBS=2 build "$A"
expect "a compile failing among two at once: the build fails, no program run" \
  '[[ $built == failed && $said == "a compile failed" ]]'
cp "$SCRATCH/other.c" "$A/other.c"
PHOBOS_JOBS=2 build "$A"
expect "fixed, two at once: built" '[[ $built == ok && $compiles == 1 && $said == "a 7 2 0 3 7" ]]'

#a header found through CPATH, which the environment names, not the flags: another CPATH, another object
D=$SCRATCH/checkout-d
mkdir -p "$D/one" "$D/two"
printf '#define FROM 1\n' > "$D/one/from.h"
printf '#define FROM 2\n' > "$D/two/from.h"
printf '#include <stdio.h>\n#include <from.h>\nint main(void) { printf("d %%d\\n", FROM); return 0; }\n' > "$D/main.c"
cat > "$D/build.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
source "$OBJECTS_SH"
object "$CC" "$ROOT/main.c" -O1
objects_done
objects_program program
$CC "${OBJECTS[@]}" -o "$REPLY.part-$$"
mv -f "$REPLY.part-$$" "$REPLY"
"$REPLY"
EOF
chmod +x "$D/build.sh"
sleep 1
CPATH=$D/one build "$D"
expect "a header through CPATH: compiled" '[[ $built == ok && $compiles == 1 && $said == "d 1" ]]'
CPATH=$D/two build "$D"
expect "another CPATH: compiled again, the other header taken" '[[ $built == ok && $compiles == 1 && $said == "d 2" ]]'

#each configuration its own program: the checkout's builds above made several
folder=$OUT/$(cd "$A" && pwd | tr -d '\n' | cksum | cut -d' ' -f1)  #(the path as the build script sees it)
programs=$(find "$folder" -maxdepth 1 -type f -name 'program-*' ! -name '*.part-*' | wc -l | tr -d ' ')
expect "a program for each configuration" '[[ $programs -ge 4 ]]'

#two checkouts at once, into the same folder
B=$SCRATCH/checkout-b
C=$SCRATCH/checkout-c
checkout "$B" b
checkout "$C" c
("$B/build.sh" > "$SCRATCH/b.log" 2>&1; echo $? > "$SCRATCH/b.status") &
("$C/build.sh" > "$SCRATCH/c.log" 2>&1; echo $? > "$SCRATCH/c.status") &
wait
built="$(cat "$SCRATCH/b.status") $(cat "$SCRATCH/c.status")" compiles="?"
said="$(tail -1 "$SCRATCH/b.log") / $(tail -1 "$SCRATCH/c.log")"
expect "two checkouts at once: each built, each its own program" \
  '[[ $built == "0 0" && $said == "b 1 1 0 1 7 / c 1 1 0 1 7" ]]'
folders=$(find "$OUT" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
expect "two checkouts at once: a folder each (and checkouts a's and d's)" '[[ $folders == 4 ]]'

echo "$failures failure$([[ $failures == 1 ]] || echo s)"
((failures == 0))
