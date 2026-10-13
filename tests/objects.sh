#Sourced by the host tests' build scripts and the runner's (tests/allegrex, tests/psp, tests/psp/ares, tests/ngcd,
#tools/psp-runner/build.sh): the objects they keep between runs, each compiled again only when it has to be.
#
#An object is named by its source and its compile command: the compiler (where it is and every line of its
#--version), every flag, and the source's path, so another checkout, compiler, sanitizer or define makes an object of
#its own. It's kept while nothing it was built from is newer than it or gone: the source and every header the
#compiler read for it, as the compiler listed them (-MD, which lists system headers too: the scripts include ares's
#and nall's as system headers). An object is compiled beside its place and renamed into it, its list first, so runs
#at the same time share objects safely and a compile cut short leaves nothing that's taken for finished; scripts
#link their programs the same way. Each checkout has its own folder in the suite's (cksum of the checkout's path),
#which can be deleted at any time.
#
#usage, in a script that has set ROOT (the checkout) and OUT (the suite's folder), or BUILD (the folder to use):
#  source "$ROOT/tests/objects.sh"
#  object COMPILER SOURCE FLAGS...   the object made or kept, and added to OBJECTS (PHOBOS_JOBS compiles at once,
#                                    1 by default; each compile's line says whether its flags sanitize)
#  objects_done                      waits for the compiles still running, and fails if any did
#  then the program linked from "${OBJECTS[@]}" into "$BUILD/NAME.part-$$", and renamed to "$BUILD/NAME"

CHECKOUT=$(printf %s "$ROOT" | cksum | cut -d' ' -f1)
BUILD=${BUILD:-$OUT/$CHECKOUT}
mkdir -p "$BUILD/objects"
#(what compiles cut short left, once no run could still be writing it)
find "$BUILD" -name '*.part-*' -mmin +120 -exec rm -f {} + 2> /dev/null || true

PHOBOS_JOBS=${PHOBOS_JOBS:-1}
OBJECTS=()
OBJECT_JOBS=()
OBJECT_COUNT=0
OBJECT_COMPILERS=()
OBJECT_IDENTITIES=()

objects_hash() {
  if command -v sha256sum > /dev/null; then sha256sum | cut -c1-16; else shasum -a 256 | cut -c1-16; fi
}

#A compiler's identity in REPLY: where it is, and every line it gives for --version (a compiler upgraded in its place
#reports another version), worked out once a run.
objects_identity() {  #compiler
  local n
  for ((n = 0; n < ${#OBJECT_COMPILERS[@]}; n++)); do
    if [[ ${OBJECT_COMPILERS[n]} == "$1" ]]; then REPLY=${OBJECT_IDENTITIES[n]}; return 0; fi
  done
  # shellcheck disable=SC2086
  REPLY="$(command -v ${1%% *} || true) $($1 --version 2>&1 || true)"
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
  # shellcheck disable=SC2086
  if $compiler "$@" -MD -MF "$part.d" -MQ "$object" -c "$source" -o "$part.o"; then
    mv -f "$part.d" "$list" && mv -f "$part.o" "$object"
  else
    rm -f "$part.o" "$part.d"
    return 1
  fi
}

object() {  #compiler source flags...
  local compiler=$1 source=$2
  shift 2
  objects_identity "$compiler"
  local name=${source#"$ROOT"/} key
  name=${name//\//-}
  key=$(printf '%s\n' "$REPLY" "$source" "$@" | objects_hash)
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
