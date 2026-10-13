#!/usr/bin/env bash
#The PSP renderer's shaders checked strictly (CI's PSP Shaders workflow; docs/psp-gpu-renderers.md): the glslang
#shaders.hpp was made with must be the one found, as compile.sh --check then compiles every shader again and compares
#the result with shaders.hpp byte for byte (beside the sources' hash, which tests/psp/run-tests.sh checks everywhere);
#and every SPIR-V module shaders.hpp carries, as the program will hand it to Vulkan, must pass spirv-val for Vulkan 1.0
#(compile.sh's target). A missing or other compiler is a failure here, not a skip.
#The version is pinned here and in the workflow (its version and archive SHA-256), which leaves this default alone, so
#the two must move together, with shaders.hpp made again by the new compiler and docs/psp-gpu-renderers.md's line.
#usage: .github/scripts/check-psp-shaders.sh   (PSP_GLSLANG_VERSION: another version to require, for a local trial)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SHADERS=$ROOT/ares/psp/ge/gpu/shaders
WANTED=${PSP_GLSLANG_VERSION:-16.6.0}

command -v glslangValidator > /dev/null || { echo "no glslangValidator: this check needs glslang $WANTED" >&2; exit 1; }
command -v spirv-val > /dev/null || { echo "no spirv-val: this check needs SPIRV-Tools" >&2; exit 1; }
version=$(glslangValidator --version) || { echo "glslangValidator --version failed" >&2; exit 1; }
found=$(sed -n 's/^Glslang Version: [0-9]*:\(.*\)$/\1/p' <<< "$version")
if [[ $found != "$WANTED" ]]; then
  echo "glslangValidator is glslang ${found:-of no version it names}; shaders.hpp is made with $WANTED (another" >&2
  echo "version is a toolchain change: shaders.hpp made with it, and this script's default, the PSP Shaders" >&2
  echo "workflow's version and archive SHA-256 and docs/psp-gpu-renderers.md moved in the same change)" >&2
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
#(every array declared, with or without '=', must be one read above, and every SPIR-V module's magic number one of
#theirs: none left unvalidated however it's written)
declared = re.findall(r'(\w+)\s*\[[^\]]*\]\s*=?\s*\{', text)
if len(declared) != len(modules):
    unread = sorted(set(declared) - {name for name, _ in modules})
    sys.exit('shaders.hpp: arrays this check doesn\'t read: ' + ', '.join(unread or declared))
if text.lower().count('0x07230203') != len(modules):
    sys.exit(f'shaders.hpp: {text.lower().count("0x07230203")} SPIR-V magic numbers, but {len(modules)} arrays read')
for name, body in modules:
    tokens = [token.strip() for token in body.split(',') if token.strip()]
    bad = [token for token in tokens if not re.fullmatch(r'0x[0-9a-fA-F]{8}', token)]
    if bad:
        sys.exit(f'shaders.hpp: {name} holds something other than 32-bit words: {bad[0]}')
    words = [int(token, 16) for token in tokens]
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
