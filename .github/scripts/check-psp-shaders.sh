#!/usr/bin/env bash
#The PSP renderer's shaders checked strictly (CI's PSP Shaders workflow; docs/psp-gpu-renderers.md): the glslang
#shaders.hpp was made with must be the one found, as compile.sh --check then compiles every shader again and compares
#the result with shaders.hpp byte for byte (beside the sources' hash, which tests/psp/run-tests.sh checks everywhere);
#and every SPIR-V module shaders.hpp carries, as the program will hand it to Vulkan, must pass spirv-val for Vulkan 1.0
#(compile.sh's target). A missing or other compiler is a failure here, not a skip.
#usage: .github/scripts/check-psp-shaders.sh   (PSP_GLSLANG_VERSION: the version shaders.hpp is made with, 16.6.0)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SHADERS=$ROOT/ares/psp/ge/gpu/shaders
WANTED=${PSP_GLSLANG_VERSION:-16.6.0}

command -v glslangValidator > /dev/null || { echo "no glslangValidator: this check needs glslang $WANTED" >&2; exit 1; }
command -v spirv-val > /dev/null || { echo "no spirv-val: this check needs SPIRV-Tools" >&2; exit 1; }
found=$(glslangValidator --version | sed -n 's/^Glslang Version: [0-9]*:\(.*\)$/\1/p')
if [[ $found != "$WANTED" ]]; then
  echo "glslangValidator is glslang ${found:-of no version it names}; shaders.hpp is made with $WANTED (another" >&2
  echo "version is a toolchain change: compile.sh with it, and PSP_GLSLANG_VERSION moved in the same change)" >&2
  exit 1
fi
echo "glslang $found ($(command -v glslangValidator)); $(spirv-val --version | head -1)"

status=0
#the sources' hash, then every shader compiled again and compared with shaders.hpp byte for byte
if "$SHADERS/compile.sh" --check; then
  echo "shaders.hpp: its GLSL's hash, and what glslang $found compiles that GLSL to, byte for byte"
else
  status=1
fi

#each module in shaders.hpp as its bytes (the words as they are in memory, little-endian, as od read them), validated
work=$(mktemp -d "${TMPDIR:-/tmp}/psp-shaders-check.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 - "$SHADERS/shaders.hpp" "$work" <<'EOF' || exit 1
import pathlib, re, struct, sys
text = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
modules = re.findall(r'static const u32 (\w+)\[\] = \{([^}]*)\};', text)
if not modules:
    sys.exit('shaders.hpp: no SPIR-V arrays found')
for name, body in modules:
    if not re.fullmatch(r'[\s0-9a-fA-Fx,]*', body):
        sys.exit(f'shaders.hpp: {name} holds something other than words')
    words = [int(word, 16) for word in re.findall(r'0x([0-9a-fA-F]{8})', body)]
    if not words or words[0] != 0x07230203:
        sys.exit(f'shaders.hpp: {name} doesn\'t begin with SPIR-V\'s magic number')
    (out / f'{name}.spv').write_bytes(struct.pack(f'<{len(words)}I', *words))
print(f'{len(modules)} modules in shaders.hpp: ' + ', '.join(name for name, _ in modules))
EOF
valid=0
for module in "$work"/*.spv; do
  if spirv-val --target-env vulkan1.0 "$module"; then
    valid=$((valid + 1))
  else
    echo "$(basename "$module" .spv): not valid SPIR-V for Vulkan 1.0" >&2
    status=1
  fi
done
echo "$valid valid for Vulkan 1.0"
exit $status
