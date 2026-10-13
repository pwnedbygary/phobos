# shellcheck shell=bash
#Sourced by the host tests' build scripts and the runner's (tests/allegrex, tests/psp, tests/psp/ares, tests/ngcd,
#tools/psp-runner/build.sh): the objects they keep between runs, each compiled again only when it has to be.
#
#An object is named by its source and its compile command: the compiler as named (CC or CXX, every word), where it is
#and every line of its --version, every flag, the source's path, and the environment's include and SDK settings
#(CPATH and the like), so another checkout, compiler, sanitizer or define makes an object of its own. It's kept while
#nothing it was built from is newer than it or gone: the source and every header the compiler read for it, as the
#compiler listed them (-MD, which lists system headers too: the scripts include ares's and nall's as system headers).
#An object takes the time its compile began, so a header saved while it compiled is newer than it. An object is
#compiled beside its place and renamed into it, its list first, so runs at the same time share objects safely and a
#compile cut short leaves nothing that's taken for finished; a program is linked the same way, under a name of its
#configuration's (objects_program). Each checkout has its own folder in the suite's (cksum of the checkout's path,
#which the folder's .phobos-checkout file names), which can be deleted between runs; each configuration a checkout
#builds keeps a set of objects there (tens of MB each) until then.
#What times can't tell: a header added where it shadows another (a file the list doesn't name), and a file replaced
#by an older-dated copy (cp -p, an archive) in a checkout built before, are taken as unchanged.
#
#usage, in a script that has set ROOT (the checkout) and OUT (the suite's folder), or OBJECTS_BUILD (a folder to use):
#  source "$ROOT/tests/objects.sh"
#  object COMPILER SOURCE FLAGS...    the object made or kept, and added to OBJECTS (PHOBOS_JOBS compiles at once,
#                                     1 by default; each compile's line says whether its flags sanitize)
#  objects_done                       waits for the compiles still running, and fails if any did
#  objects_program NAME LINKFLAGS...  REPLY: the program's path, $BUILD/NAME-<its objects' and flags' hash>
#  then the program linked from "${OBJECTS[@]}" into "$REPLY.part-$$", and renamed to "$REPLY"

CHECKOUT=$(printf %s "$ROOT" | cksum | cut -d' ' -f1)
BUILD=${OBJECTS_BUILD:-$OUT/$CHECKOUT}
mkdir -p "$BUILD/objects"
printf '%s\n' "$ROOT" > "$BUILD/.phobos-checkout"
#(what compiles and links cut short left, once no run could still be writing it: objects, and programs by their names)
find "$BUILD/objects" -type f -name '*.part-*' -mmin +120 -exec rm -f {} + 2> /dev/null || true
find "$BUILD" -maxdepth 1 -type f -name '*-????????????????.part-*' -mmin +120 -exec rm -f {} + 2> /dev/null || true

PHOBOS_JOBS=${PHOBOS_JOBS:-1}
OBJECTS=()
OBJECT_JOBS=()
OBJECT_COUNT=0
OBJECT_COMPILERS=()
OBJECT_IDENTITIES=()

objects_hash() {
  if command -v sha256sum > /dev/null; then sha256sum | cut -c1-16; else shasum -a 256 | cut -c1-16; fi
}

#A compiler's identity in REPLY: the name it's given (every word), where it is, and every line it gives for
#--version (a compiler upgraded in its place reports another version), worked out once a run.
objects_identity() {  #compiler
  local n
  for ((n = 0; n < ${#OBJECT_COMPILERS[@]}; n++)); do
    if [[ ${OBJECT_COMPILERS[n]} == "$1" ]]; then REPLY=${OBJECT_IDENTITIES[n]}; return 0; fi
  done
  # shellcheck disable=SC2086
  REPLY="$1 $(command -v ${1%% *} || true) $($1 --version 2>&1 || true)"
  OBJECT_COMPILERS+=("$1")
  OBJECT_IDENTITIES+=("$REPLY")
}

#Whether an object and its list are there and nothing on the list is newer than the object, or gone (another file may
#stand in its place now); a list naming nothing is no list. The list is make's: "object: source header \", the names
#with their spaces escaped, which read without -r takes apart (a backslash at a line's end joins the next line). find
#compares the times to the nanosecond where the file system keeps them, where bash 3.2's -nt (macOS's) takes whole
#seconds; it fails on a file that's gone.
objects_fresh() {  #object list
  [[ -f $1 && -f $2 ]] || return 1
  local words word named=() newer
  # shellcheck disable=SC2162
  while read -a words; do
    for word in ${words[@]+"${words[@]}"}; do
      [[ $word == *: ]] || named+=("$word")
    done
  done < "$2"
  ((${#named[@]})) || return 1
  newer=$(find "${named[@]}" -prune -newer "$1" -print 2> /dev/null) || return 1
  [[ -z $newer ]]
}

objects_compile() {  #object list part compiler source flags...
  local object=$1 list=$2 part=$3 compiler=$4 source=$5
  shift 5
  touch "$part.start"
  # shellcheck disable=SC2086
  if $compiler "$@" -MD -MF "$part.d" -MQ "$object" -c "$source" -o "$part.o"; then
    touch -r "$part.start" "$part.o" && rm -f "$part.start" && mv -f "$part.d" "$list" && mv -f "$part.o" "$object"
  else
    rm -f "$part.o" "$part.d" "$part.start"
    return 1
  fi
}

object() {  #compiler source flags...
  local compiler=$1 source=$2
  shift 2
  objects_identity "$compiler"
  local name=${source#"$ROOT"/} key
  name=${name//\//-}
  key=$(printf '%s\n' "$REPLY" "$source" "$@" "CPATH=${CPATH-}" "C_INCLUDE_PATH=${C_INCLUDE_PATH-}" \
    "CPLUS_INCLUDE_PATH=${CPLUS_INCLUDE_PATH-}" "SDKROOT=${SDKROOT-}" \
    "MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET-}" | objects_hash)
  local object="$BUILD/objects/${name%.*}-$key.o"
  OBJECTS+=("$object")
  objects_fresh "$object" "${object%.o}.d" && return 0
  local sanitized="not sanitized"
  case " $* " in *" -fsanitize="*) sanitized="sanitized" ;; esac
  echo "  compiling ${source#"$ROOT"/} ($sanitized)"
  OBJECT_COUNT=$((OBJECT_COUNT + 1))
  local part="$object.part-$$-$OBJECT_COUNT"
  if ((PHOBOS_JOBS <= 1)); then
    objects_compile "$object" "${object%.o}.d" "$part" "$compiler" "$source" "$@" || exit 1
  else
    while (($(jobs -rp | wc -l) >= PHOBOS_JOBS)); do sleep 0.2; done
    objects_compile "$object" "${object%.o}.d" "$part" "$compiler" "$source" "$@" &
    OBJECT_JOBS+=($!)
  fi
}

objects_done() {
  local job failed=0
  for job in ${OBJECT_JOBS[@]+"${OBJECT_JOBS[@]}"}; do wait "$job" || failed=1; done
  OBJECT_JOBS=()
  if ((failed)); then
    echo "a compile failed" >&2
    exit 1
  fi
}

#A program's path in REPLY: its name, and the hash of the objects it's linked from and its link flags, so runs of
#other configurations at the same time each run their own.
objects_program() {  #name linkflags...
  local name=$1
  shift
  REPLY="$BUILD/$name-$(printf '%s\n' ${OBJECTS[@]+"${OBJECTS[@]}"} "$@" | objects_hash)"
}
