#!/usr/bin/env python3
"""Check rasterizer_patched.hpp with SPIRV-Tools, beyond patch_rasterizer.py --check (CI's N64 Rasterizer job).

- spirv-val: the patched module is valid for Vulkan 1.1 and 1.3 (Granite asks for the instance's version, from 1.1 up
  to 1.3: vulkan/context.cpp).
- Structure: the patched module's shade_pixel stores no OpSpecConstantOp result; the bank's module, read the same
  way, stores 24, which shows the check sees them.
- The stored values: both modules specialized alike (--set-spec-const-default-value, --freeze-spec-const,
  --fold-spec-const-op-composite) and folded (--simplify-instructions), nothing removed, each of those 24 stores
  stores the same constant in both, whether or not anything reads it.
- The programs: specialized alike and optimized (-O, --strip-debug), the two modules give the same program.
Both for the app's generic pipeline (spec ids 0 to 2), its fill pipeline (0 to 7) and four seeded random
specializations with both bits that enable the 24 stores set. Ids are compared by what they are, not by their
numbers: types, constants and variables by their definitions and decorations, ids inside functions by the order of
their definitions; OpPhi's pairs are sorted by block.

The modules are read from the two headers here, independently of patch_rasterizer.py. A missing tool is a failure.

    python3 tools/parallel-rdp/check_rasterizer.py    (needs spirv-val, spirv-dis and spirv-opt: SPIRV-Tools 2025.1+)
"""

import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "ares" / "n64" / "vulkan" / "parallel-rdp" / "parallel-rdp" / "shaders"

GENERIC = {0: 8, 1: 8, 2: 0}
FILL = {0: 8, 1: 8, 2: (1 << 12) | (1 << 30), 3: 0, 4: 0, 5: 0, 6: 0, 7: 0}
# RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT and RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT (data_structures.h)
STORES_LIVE = (1 << 29) | (1 << 30)
SEEDS = (1, 2, 3, 4)
SPECIALIZE = ["--freeze-spec-const", "--fold-spec-const-op-composite"]
DECORATIONS = ("OpDecorate", "OpMemberDecorate", "OpDecorateId")
TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|\S+')


def words_of(text, array):
    m = re.search(rf"static const uint32_t {array}\[\] =\s*\{{([^}}]*)\}};", text)
    if not m:
        raise SystemExit(f"no {array} array")
    return [int(w, 16) for w in re.findall(r"0x([0-9a-f]{8})u", m.group(1))]


def modules():
    """The bank's SMALL_TYPES=0 rasterizer and the patched one, as bytes."""
    bank = (SHADERS / "slangmosh.hpp").read_text()
    m = re.search(r'if \(resolver\("rasterizer", "SMALL_TYPES"\) == 0\)\s*\{[^}]*'
                  r"request_program\(spirv_bank \+ (\d+), (\d+), &layout\);", bank)
    if not m:
        raise SystemExit("slangmosh.hpp: no SMALL_TYPES=0 rasterizer")
    offset, size = int(m.group(1)), int(m.group(2))
    original = words_of(bank, "spirv_bank")[offset:offset + size // 4]
    patched = words_of((SHADERS / "rasterizer_patched.hpp").read_text(), "rasterizer_patched_spirv")
    return [struct.pack(f"<{len(w)}I", *w) for w in (original, patched)]


def run(*command):
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode:
        raise SystemExit(f"{' '.join(command)} failed:\n{done.stdout}{done.stderr}")
    return done.stdout


def parse(path):
    """The module's instructions as (result id or None, opcode, operand tokens), from spirv-dis --raw-id."""
    out = []
    for line in run("spirv-dis", "--raw-id", "--no-header", "--no-color", str(path)).splitlines():
        tokens = TOKEN.findall(line)
        if not tokens:
            continue
        if len(tokens) > 2 and tokens[1] == "=":
            out.append((tokens[0], tokens[2], tokens[3:]))
        else:
            out.append((None, tokens[0], tokens[1:]))
    return out


def shade_pixel_stores(code):
    """What each OpStore in shade_pixel stores, in order."""
    names = {args[0]: args[1].strip('"') for _, op, args in code if op == "OpName"}
    stored, inside, found = [], False, 0
    for r, op, args in code:
        if op == "OpFunction":
            inside = names.get(r, "").startswith("shade_pixel(")
            found += inside
        elif op == "OpFunctionEnd":
            inside = False
        elif inside and op == "OpStore":
            stored.append(args[1])
    if found != 1:
        raise SystemExit(f"{found} functions named shade_pixel")
    return stored


def spec_op_stores(path):
    """The positions, among shade_pixel's stores, of those that store an OpSpecConstantOp result."""
    code = parse(path)
    spec_ops = {r for r, op, _ in code if op == "OpSpecConstantOp"}
    return [i for i, value in enumerate(shade_pixel_stores(code)) if value in spec_ops]


def names_of(code):
    """Every id named by what it is: module-level ones by their definitions and decorations (variables alike
    numbered in order), functions and the ids defined in them by their order; and where the functions begin."""
    first = next(i for i, (_, op, _) in enumerate(code) if op == "OpFunction")
    decorations = defaultdict(list)
    for _, op, args in code:
        if op in DECORATIONS:
            decorations[args[0]].append(" ".join([op] + args[1:]))

    def decorated(r, name):
        return name + ("[" + ";".join(sorted(decorations[r])) + "]" if decorations[r] else "")

    names, seen, functions = {}, defaultdict(int), 0
    for r, op, args in code[first:]:
        if op == "OpFunction":
            names[r] = decorated(r, f"%F{functions}")
            functions += 1
        elif r is not None:
            names[r] = decorated(r, f"%L{len(names)}")
    for r, op, args in code[:first]:
        if r is None:
            continue
        name = "{" + " ".join([op] + [names.get(a, a) for a in args]) + "}"
        if op == "OpVariable":  # distinct objects even when described alike
            seen[name] += 1
            name += f"#{seen[name]}"
        names[r] = decorated(r, name)
    return names, first


def canonical(code):
    """The module with every id written by what it is: (the module-level instructions, the sorted module-level
    definitions, the functions)."""
    names, first = names_of(code)

    def written(r, op, args):
        args = [names.get(a, a) for a in args]
        if op == "OpPhi":
            pairs = sorted(zip(args[1::2], args[2::2]), key=lambda pair: pair[1])
            args = [args[0]] + [x for pair in pairs for x in pair]
        return ((names[r] + " = ") if r else "") + " ".join([op] + args)

    module = [written(r, op, args) for r, op, args in code[:first] if r is None and op not in DECORATIONS]
    definitions = sorted(names[r] for r, _, _ in code[:first] if r is not None)
    functions = [written(r, op, args) for r, op, args in code[first:]]
    return module, definitions, functions


def first_difference(a, b):
    for part, x, y in zip(("module-level instructions", "module-level definitions", "functions"), a, b):
        if x != y:
            i = next((i for i, (p, q) in enumerate(zip(x, y)) if p != q), min(len(x), len(y)))
            return (f"{part} differ ({len(x)} against {len(y)}), first at {i}:\n  "
                    f"{x[i] if i < len(x) else '(none)'}\n  {y[i] if i < len(y) else '(none)'}")
    return None


def stored_values(path, spec, out):
    """What each of shade_pixel's stores stores once the module is specialized and folded, nothing removed: a
    module-level id (a constant) by what it is, anything else as "computed"."""
    run("spirv-opt", "--target-env=vulkan1.1", "--set-spec-const-default-value", spec, *SPECIALIZE,
        "--simplify-instructions", str(path), "-o", str(out))
    code = parse(out)
    names, first = names_of(code)
    module_level = {r for r, _, _ in code[:first] if r is not None}
    return [names[v] if v in module_level else "computed" for v in shade_pixel_stores(code)]


def program(path, spec, out):
    run("spirv-opt", "--target-env=vulkan1.1", "--set-spec-const-default-value", spec, *SPECIALIZE, "-O",
        "--strip-debug", str(path), "-o", str(out))
    return canonical(parse(out))


def main():
    missing = [tool for tool in ("spirv-val", "spirv-dis", "spirv-opt") if not shutil.which(tool)]
    if missing:
        print(f"no {', '.join(missing)}: this check needs SPIRV-Tools", file=sys.stderr)
        return 1
    version = subprocess.run(["spirv-opt", "--version"], capture_output=True, text=True)
    print((version.stdout + version.stderr).strip().splitlines()[0])
    status = 0
    with tempfile.TemporaryDirectory(prefix="n64-rasterizer.") as work:
        work = Path(work)
        original, patched = work / "bank.spv", work / "patched.spv"
        for path, data in zip((original, patched), modules()):
            path.write_bytes(data)

        environments = ("vulkan1.1", "vulkan1.3")
        valid = 0
        for env in environments:
            done = subprocess.run(["spirv-val", "--target-env", env, str(patched)], capture_output=True, text=True)
            print(f"spirv-val --target-env {env}: {'valid' if done.returncode == 0 else 'NOT VALID'}")
            if done.returncode:
                print(done.stdout + done.stderr)
                status = 1
            else:
                valid += 1

        sites, left = spec_op_stores(original), len(spec_op_stores(patched))
        print(f"shade_pixel's stores of OpSpecConstantOp results: {len(sites)} in the bank's module, {left} patched")
        if left or not sites:
            status = 1

        specializations = [("generic", GENERIC), ("fill", FILL)]
        for seed in SEEDS:
            rng = random.Random(seed)
            values = {0: 8, 1: 8, 2: rng.randrange(1 << 31) | STORES_LIVE}
            values.update((i, rng.randrange(1 << 31)) for i in range(3, 8))
            specializations.append((f"random {seed}", values))
        same_values = same_programs = 0
        for label, values in specializations:
            spec = " ".join(f"{k}:{v}" for k, v in sorted(values.items()))
            print(f"{label} ({spec}):")
            stored = [stored_values(path, spec, work / f"{path.stem}.folded.spv") for path in (original, patched)]
            wrong = [i for i in sites if stored[0][i] != stored[1][i] or stored[0][i] == "computed"]
            if len(stored[0]) != len(stored[1]) or wrong:
                status = 1
                print(f"  the {len(sites)} stored values: DIFFERENT, {len(stored[0])} against {len(stored[1])} stores"
                      + (f"; first at store {wrong[0]}: {stored[0][wrong[0]]} against {stored[1][wrong[0]]}"
                         if wrong else ""))
            else:
                same_values += 1
                print(f"  the {len(sites)} stored values: the same constants")
            forms = [program(path, spec, work / f"{path.stem}.opt.spv") for path in (original, patched)]
            difference = first_difference(*forms)
            if difference:
                status = 1
                print(f"  the program: DIFFERENT: {difference}")
            else:
                same_programs += 1
                print(f"  the program: the same, {len(forms[0][2])} instructions in its functions")
    print(f"tools/parallel-rdp/check_rasterizer.py: {'passed' if status == 0 else 'FAILED'}: valid for {valid} of "
          f"{len(environments)} environments; {left} of {len(sites)} stores left; the stored values the same in "
          f"{same_values} and the program in {same_programs} of {len(specializations)} specializations")
    return status


if __name__ == "__main__":
    sys.exit(main())
