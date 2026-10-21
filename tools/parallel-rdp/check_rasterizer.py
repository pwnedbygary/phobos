#!/usr/bin/env python3
"""Check rasterizer_patched.hpp with SPIRV-Tools, beyond patch_rasterizer.py --check (CI's N64 Rasterizer job).

- spirv-val: the patched module is valid for Vulkan 1.1 and 1.3 (Granite asks for the instance's version, from 1.1 up
  to 1.3: vulkan/context.cpp).
- The structure, which proves the patch for every specialization. The two modules are read side by side, each id of
  the bank's module paired with one id of the patched one. Each of shade_pixel's stores of an OpSpecConstantOp result
  must be preceded, in the patched module, by instructions that mirror that OpSpecConstantOp tree node for node (each
  OpSpecConstantOp as the instruction of its opcode, with the same result type and literal operands), down to the
  same leaves (OpSpecConstant, OpSpecConstantTrue/False or OpConstant, in both modules), and must store the tree's
  root. Every other instruction must be the same, apart from the ids those instructions add. 24 stores must be
  rewritten so, and the patched shade_pixel must store no OpSpecConstantOp result. SPIR-V specializes an
  OpSpecConstantOp "by executing the operation and replacing the instruction with the result" (version 1.6, revision
  8, section 2.12), so a mirrored tree stores the same bits in every specialization.
- Cross-checks, for the app's generic pipeline (spec ids 0 to 2), its fill pipeline (0 to 7), four seeded random
  specializations (values from all 32 bits, with both bits that enable the 24 stores set) and the odd case below:
  - the values took: each spec constant's default reads back as set;
  - the stored values: both modules specialized alike (--set-spec-const-default-value, --freeze-spec-const,
    --fold-spec-const-op-composite) and folded (--simplify-instructions), nothing removed, each of those 24 stores
    stores the same constant in both, whether or not anything reads it;
  - the programs: specialized alike, optimized (-O, --strip-debug) and then cleaned up (--unify-const
    --eliminate-dead-const --eliminate-dead-code-aggressive -O) until the module stops changing, at most four rounds,
    the two modules give the same program. Ids are compared by what they are, not by their numbers: types, constants
    and variables by their definitions and decorations, ids inside functions by the order of their definitions;
    OpPhi's pairs are sorted by block.

The modules are read from the two headers here, independently of patch_rasterizer.py, and their instructions from
spirv-dis --raw-id. A missing tool is a failure.

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
# One -O alone leaves an extra OpConstant 0 in the patched module's program here (SPIRV-Tools 2025.1 and 2026.4).
ODD = {0: 8, 1: 8, 2: 887693835, 3: 1001798289, 4: 1767336990, 5: 0, 6: 0, 7: 0}
# RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT and RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT (data_structures.h)
STORES_LIVE = (1 << 29) | (1 << 30)
SEEDS = (1, 2, 3, 4)
STORES = 24
SPECIALIZE = ["--freeze-spec-const", "--fold-spec-const-op-composite"]
CLEANUP = ["--unify-const", "--eliminate-dead-const", "--eliminate-dead-code-aggressive", "-O"]
ROUNDS = 4
LEAVES = ("OpSpecConstant", "OpSpecConstantTrue", "OpSpecConstantFalse", "OpConstant")
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


def spec_op_stores(code):
    """The positions, among shade_pixel's stores, of those that store an OpSpecConstantOp result."""
    spec_ops = {r for r, op, _ in code if op == "OpSpecConstantOp"}
    return [i for i, value in enumerate(shade_pixel_stores(code)) if value in spec_ops]


def written(instruction):
    r, op, args = instruction
    text = " ".join(([r, "="] if r else []) + [op] + args)
    return text if len(text) <= 160 else text[:157] + "..."


def structure(original, patched):
    """The two modules side by side (the docstring's structure): how many of shade_pixel's stores of an
    OpSpecConstantOp result the bank's module has, how many the patched one rewrites as their trees, what else
    differs, and how many ids were paired."""
    names = {args[0]: args[1].strip('"') for _, op, args in original if op == "OpName"}
    spec_ids = {args[0]: args[2] for _, op, args in original if op == "OpDecorate" and args[1:2] == ["SpecId"]}
    defined = {r: (op, args) for r, op, args in original if r}
    to_patched, to_original = {}, {}
    problems = []

    def pair(a, b):
        if a in to_patched or b in to_original:
            return to_patched.get(a) == b
        to_patched[a], to_original[b] = b, a
        return True

    def same(x, y):
        """Whether two instructions are the same, their ids paired (on first sight) or literally equal."""
        if x[1] != y[1] or (x[0] is None) != (y[0] is None) or len(x[2]) != len(y[2]):
            return False
        for a, b in ([(x[0], y[0])] if x[0] else []) + list(zip(x[2], y[2])):
            if a.startswith("%") != b.startswith("%"):
                return False
            if a.startswith("%"):
                if not pair(a, b):
                    return False
            elif a != b:
                return False
        return True

    def what(o):
        op, args = defined.get(o, ("?", []))
        label = names.get(o) or (f"SpecId {spec_ids[o]}" if o in spec_ids else " ".join([op] + args[1:]))
        return f"{o} ({label})"

    def mirror(o, p, inserted, nodes):
        """None if the patched id p computes the bank's id o node for node, else what differs. o is an
        OpSpecConstantOp, computed by inserted instructions, or a leaf, the same id in both modules."""
        op, args = defined.get(o, (None, []))
        if op in LEAVES:
            return None if to_patched.get(o) == p else f"the leaf {what(o)} became {p}"
        if op != "OpSpecConstantOp":
            return f"{what(o)} is neither an OpSpecConstantOp nor a constant"
        if p not in inserted:
            return f"{what(o)} is {p} in the patched module, not an instruction inserted before the store"
        _, opcode, operands = inserted[p]
        if opcode != "Op" + args[1]:
            return f"{opcode} ({p}) inserted for {what(o)}, an OpSpecConstantOp {args[1]}"
        if len(operands) != len(args) - 1 or to_patched.get(args[0]) != operands[0]:
            return f"{opcode} ({p}): not the result type or operand count of {what(o)}"
        if nodes.setdefault(p, o) != o:
            return f"{p} stands for both {what(nodes[p])} and {what(o)}"
        for a, b in zip(args[2:], operands[1:]):
            if a.startswith("%") and b.startswith("%"):
                problem = mirror(a, b, inserted, nodes)
                if problem:
                    return problem
            elif a != b:
                return f"{b} for {a} in {opcode} ({p})"
        return None

    function, sites, rewritten, inserted_ids = "", 0, 0, set()
    i = j = 0
    while i < len(original):
        x = original[i]
        if x[1] == "OpFunction":
            function = names.get(x[0], x[0])
        site = (function.startswith("shade_pixel(") and x[1] == "OpStore"
                and defined.get(x[2][1], ("",))[0] == "OpSpecConstantOp")
        if not site:
            if j == len(patched) or not same(x, patched[j]):
                problems.append(f"instruction {i} ({function or 'module level'}): {written(x)} against "
                                + (written(patched[j]) if j < len(patched) else "the end of the patched module"))
                return sites, rewritten, problems, len(to_patched)
            i, j = i + 1, j + 1
            continue
        sites += 1
        run_of = []
        while j < len(patched) and patched[j][1] != "OpStore":
            run_of.append(patched[j])
            j += 1
        if j == len(patched):
            problems.append(f"store {sites - 1} ({what(x[2][1])}): no store after it in the patched module")
            return sites, rewritten, problems, len(to_patched)
        y = patched[j]
        inserted = {r: (r, op, args) for r, op, args in run_of if r}
        if len(x[2]) != len(y[2]) or not same((None, "OpStore", x[2][:1] + x[2][2:]),
                                              (None, "OpStore", y[2][:1] + y[2][2:])):
            problem = "the pointer or the memory operands differ"
        elif len(inserted) != len(run_of) or any(r in to_original for r in inserted):
            problem = "an instruction without a new result id inserted before it"
        elif not run_of:
            problem = "left as the bank has it" if to_patched.get(x[2][1]) == y[2][1] else f"stores {y[2][1]}"
        else:
            nodes = {}
            problem = mirror(x[2][1], y[2][1], inserted, nodes)
            if problem is None and len(nodes) != len(run_of):
                problem = f"{len(run_of)} instructions inserted for a tree of {len(nodes)}"
        if problem:
            problems.append(f"store {sites - 1} ({what(x[2][1])}): {problem}")
        else:
            rewritten += 1
        inserted_ids.update(inserted)
        i, j = i + 1, j + 1
    if j != len(patched):
        problems.append(f"the patched module has {len(patched) - j} more instructions at its end")
    odd = {r for r, _, _ in patched if r and r not in to_original} ^ inserted_ids
    if odd:
        problems.append(f"{len(odd)} result ids of the patched module neither paired with the bank's nor inserted (or "
                        f"both), e.g. {sorted(odd)[0]}")
    return sites, rewritten, problems, len(to_patched)


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

    def form(r, op, args):
        args = [names.get(a, a) for a in args]
        if op == "OpPhi":
            pairs = sorted(zip(args[1::2], args[2::2]), key=lambda pair: pair[1])
            args = [args[0]] + [x for pair in pairs for x in pair]
        return ((names[r] + " = ") if r else "") + " ".join([op] + args)

    module = [form(r, op, args) for r, op, args in code[:first] if r is None and op not in DECORATIONS]
    definitions = sorted(names[r] for r, _, _ in code[:first] if r is not None)
    functions = [form(r, op, args) for r, op, args in code[first:]]
    return module, definitions, functions


def first_difference(a, b):
    for part, x, y in zip(("module-level instructions", "module-level definitions", "functions"), a, b):
        if x != y:
            i = next((i for i, (p, q) in enumerate(zip(x, y)) if p != q), min(len(x), len(y)))
            return (f"{part} differ ({len(x)} against {len(y)}), first at {i}:\n  "
                    f"{x[i] if i < len(x) else '(none)'}\n  {y[i] if i < len(y) else '(none)'}")
    return None


def spec_text(values):
    # In hex: spirv-opt leaves a spec constant at its default, with no error, when it can't read the value (a decimal
    # above 2^31 - 1 for an int).
    return " ".join(f"{k}:0x{v:08x}" for k, v in sorted(values.items()))


def not_taken(path, values, out):
    """The spec ids whose default doesn't read back as set."""
    run("spirv-opt", "--set-spec-const-default-value", spec_text(values), str(path), "-o", str(out))
    code = parse(out)
    spec_ids = {args[0]: int(args[2]) for _, op, args in code if op == "OpDecorate" and args[1:2] == ["SpecId"]}
    defaults = {spec_ids[r]: int(args[-1]) & 0xffffffff
                for r, op, args in code if op == "OpSpecConstant" and r in spec_ids}
    return [k for k, v in sorted(values.items()) if defaults.get(k) != v]


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
    """The program specialized and optimized, then cleaned up until it stops changing (at most ROUNDS rounds), and
    how many cleanup rounds changed it."""
    run("spirv-opt", "--target-env=vulkan1.1", "--set-spec-const-default-value", spec, *SPECIALIZE, "-O",
        "--strip-debug", str(path), "-o", str(out))
    cleaned, changed = out.with_suffix(".clean.spv"), 0
    for _ in range(ROUNDS):
        run("spirv-opt", "--target-env=vulkan1.1", *CLEANUP, str(out), "-o", str(cleaned))
        if cleaned.read_bytes() == out.read_bytes():
            break
        cleaned.replace(out)
        changed += 1
    return canonical(parse(out)), changed


def main():
    missing = [tool for tool in ("spirv-val", "spirv-dis", "spirv-opt") if not shutil.which(tool)]
    if missing:
        print(f"no {', '.join(missing)}: this check needs SPIRV-Tools", file=sys.stderr)
        return 1
    version = subprocess.run(["spirv-opt", "--version"], capture_output=True, text=True)
    tools = (version.stdout + version.stderr).strip().splitlines()[0]
    print(tools)
    status = 0
    with tempfile.TemporaryDirectory(prefix="n64-rasterizer.") as work:
        work = Path(work)
        original, patched = work / "bank.spv", work / "patched.spv"
        data = modules()
        for path, module in zip((original, patched), data):
            path.write_bytes(module)

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

        code = [parse(path) for path in (original, patched)]
        found, rewritten, problems, paired = structure(*code)
        if data[0][4:8] != data[1][4:8]:
            problems.insert(0, "the headers' SPIR-V versions differ")
        holds = not problems and found == rewritten == STORES
        print(f"the structure: {'holds' if holds else 'FAILS'}: {rewritten} of {found} stores of an OpSpecConstantOp "
              f"result in shade_pixel ({STORES} expected) rewritten as their trees, node for node; "
              f"{len(problems)} problems; {paired} ids paired")
        for problem in problems[:10]:
            print(f"  {problem}")
        sites, left = spec_op_stores(code[0]), len(spec_op_stores(code[1]))
        print(f"shade_pixel's stores of OpSpecConstantOp results: {len(sites)} in the bank's module, {left} patched")
        if not holds or left:
            status = 1

        specializations = [("generic", GENERIC), ("fill", FILL)]
        for seed in SEEDS:
            rng = random.Random(seed)
            values = {0: 8, 1: 8, 2: rng.getrandbits(32) | STORES_LIVE}
            values.update((i, rng.getrandbits(32)) for i in range(3, 8))
            specializations.append((f"random {seed}", values))
        specializations.append(("the odd case", ODD))
        took = same_values = same_programs = 0
        for label, values in specializations:
            spec = spec_text(values)
            print(f"{label} ({spec}):")
            wrong = not_taken(original, values, work / "set.spv")
            if wrong:
                status = 1
                print(f"  the values: NOT TAKEN for spec ids {wrong}")
            else:
                took += 1
                print("  the values: read back as set")
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
            results = [program(path, spec, work / f"{path.stem}.opt.spv") for path in (original, patched)]
            rounds = f"cleanup rounds that changed it: {results[0][1]} and {results[1][1]}"
            difference = first_difference(results[0][0], results[1][0])
            if difference:
                status = 1
                print(f"  the program: DIFFERENT ({rounds}): {difference}")
            else:
                same_programs += 1
                print(f"  the program: the same, {len(results[0][0][2])} instructions in its functions ({rounds})")
    short = re.match(r"SPIRV-Tools (\S+)", tools)
    print(f"tools/parallel-rdp/check_rasterizer.py: {'passed' if status == 0 else 'FAILED'} "
          f"({short.group(1) if short else tools}): the structure {'holds' if holds else 'FAILS'}, {rewritten} of "
          f"{found} stores rewritten as their trees; {left} stores of an OpSpecConstantOp result left; valid for "
          f"{valid} of {len(environments)} environments; of {len(specializations)} specializations, the values took "
          f"in {took}, the stored values are the same in {same_values} and the program in {same_programs}")
    return status


if __name__ == "__main__":
    sys.exit(main())
