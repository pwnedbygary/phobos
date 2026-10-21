#!/usr/bin/env python3
"""Write rasterizer_patched.hpp: parallel-rdp's SMALL_TYPES=0 rasterizer in a form Qualcomm's compiler accepts.

Qualcomm's proprietary Vulkan driver (Adreno 740, driver 0676.53, compiler E031.41.03.47) fails every pipeline made
from the rasterizer that slangmosh.hpp, the shader bank vendored from ares, carries for SMALL_TYPES=0
(VK_ERROR_UNKNOWN, "Shader compilation failed"), so N64 games draw nothing. Variants of the module compiled on that
device located what it rejects: shade_pixel's RASTERIZER_SPEC_CONSTANT blocks store values that are OpSpecConstantOp
results (the static state flags, the dither mode, the 16 combiner inputs, the texture formats and sizes: 24 stores),
and the compiler rejects the module for them even where specialization leaves those stores dead. With each of those
values computed by the ordinary instruction instead, the module compiles there: its generic pipeline, and the
specialization the app makes for fills.

This script takes that module from slangmosh.hpp (and refuses any other: its SHA-256 must be RASTERIZER_SHA256),
rewrites those 24 stores and nothing else, and writes the result, with the bank's resource layout record for the
module, into rasterizer_patched.hpp beside slangmosh.hpp, which stays as it is. CommandProcessor::init_renderer
(rdp_device.cpp) puts the result in the bank's place wherever the bank chooses the SMALL_TYPES=0 rasterizer.

The rewrite is exact. SPIR-V's specification (version 1.6, revision 8, section 2.12, "Specialization") says: "The
OpSpecConstantOp instruction is specialized by executing the operation and replacing the instruction with the
result." The rewrite executes the same operation, as the ordinary instruction of that opcode, on the same operands:
each OpSpecConstantOp result in the value's operands is computed the same way first, down to the specialization
constants themselves (OpSpecConstant), which stay as they are. So every specialization stores the same bits as
before; only when they're computed differs.

The module is written as the one tested on the device was: that one was made by disassembling the bank's module with
SPIRV-Tools, editing the text and assembling it again, which numbers the ids in the order they first appear in the
disassembly (each instruction's result id first) and puts the assembler's generator id in the header. Writing the same
bytes (PATCHED_SHA256) keeps that test about this module.

    python3 tools/parallel-rdp/patch_rasterizer.py            write rasterizer_patched.hpp
    python3 tools/parallel-rdp/patch_rasterizer.py --check    write nothing; exit 1 unless the file is what it writes

Python 3 standard library only. The same slangmosh.hpp always gives the same file, byte for byte.
"""

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "ares" / "n64" / "vulkan" / "parallel-rdp" / "parallel-rdp" / "shaders"
BANK = SHADERS / "slangmosh.hpp"
OUT = SHADERS / "rasterizer_patched.hpp"

# The SMALL_TYPES=0 rasterizer in slangmosh.hpp as vendored from ares, and the module this script makes from it.
RASTERIZER_SHA256 = "2787b409266d65f03f69729c384a81736626cb9d81781dd316cd9ce701b45636"
PATCHED_SHA256 = "e3b620ef57de25d7ce8856cf97b0aeaa5f55ca43139b97fad967d87b6a83bb49"
STORES = 24

MAGIC = 0x07230203
GENERATOR_SPIRV_TOOLS_ASSEMBLER = 0x00070000

OP_NAME = 5
OP_SPEC_CONSTANT_OP = 52
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_LOAD = 61
OP_STORE = 62
OP_VECTOR_SHUFFLE = 79
OP_COMPOSITE_EXTRACT = 81
OP_COMPOSITE_INSERT = 82
OP_SWITCH = 251

# The operands of each opcode the module has, in SPIR-V's grammar: T the result type, R the result, I another <id>,
# L a literal word, S a literal string; after a '*', that kind repeats to the end. OpSwitch and OpSpecConstantOp,
# whose operands depend on other operands, are kinds() own cases.
SHAPES = {
    3: "LL",        # OpSource (the module has no file or source text)
    4: "S",         # OpSourceExtension
    5: "IS",        # OpName
    6: "ILS",       # OpMemberName
    10: "S",        # OpExtension
    11: "RS",       # OpExtInstImport
    12: "TRIL*I",   # OpExtInst
    14: "LL",       # OpMemoryModel
    15: "LIS*I",    # OpEntryPoint
    16: "IL*L",     # OpExecutionMode
    17: "L",        # OpCapability
    19: "R",        # OpTypeVoid
    20: "R",        # OpTypeBool
    21: "RLL",      # OpTypeInt
    23: "RIL",      # OpTypeVector
    28: "RII",      # OpTypeArray
    29: "RI",       # OpTypeRuntimeArray
    30: "R*I",      # OpTypeStruct
    32: "RLI",      # OpTypePointer
    33: "RI*I",     # OpTypeFunction
    41: "TR",       # OpConstantTrue
    42: "TR",       # OpConstantFalse
    43: "TR*L",     # OpConstant
    44: "TR*I",     # OpConstantComposite
    50: "TR*L",     # OpSpecConstant
    51: "TR*I",     # OpSpecConstantComposite
    54: "TRLI",     # OpFunction
    55: "TR",       # OpFunctionParameter
    56: "",         # OpFunctionEnd
    57: "TRI*I",    # OpFunctionCall
    59: "TRL*I",    # OpVariable
    61: "TRI*L",    # OpLoad (memory operands without <id>s: checked)
    62: "II*L",     # OpStore (likewise)
    65: "TRI*I",    # OpAccessChain
    71: "IL*L",     # OpDecorate
    72: "ILL*L",    # OpMemberDecorate
    79: "TRII*L",   # OpVectorShuffle
    80: "TR*I",     # OpCompositeConstruct
    81: "TRI*L",    # OpCompositeExtract
    245: "TR*I",    # OpPhi
    247: "IL",      # OpSelectionMerge
    248: "R",       # OpLabel
    249: "I",       # OpBranch
    250: "III*L",   # OpBranchConditional
    253: "",        # OpReturn
    254: "I",       # OpReturnValue
}
# OpUConvert, OpSConvert, OpBitcast, OpSNegate, OpIAdd, OpISub, OpIMul, OpAny, OpAll, OpLogicalOr, OpLogicalAnd,
# OpLogicalNot, OpSelect, OpIEqual, OpINotEqual, OpSGreaterThan, OpUGreaterThanEqual, OpSGreaterThanEqual,
# OpSLessThan, OpSLessThanEqual, OpShiftRightLogical, OpShiftRightArithmetic, OpShiftLeftLogical, OpBitwiseOr,
# OpBitwiseXor, OpBitwiseAnd, OpNot, OpBitFieldSExtract and OpBitCount: a result and <id> operands.
SHAPES.update((op, "TR*I") for op in (113, 114, 124, 126, 128, 130, 132, 154, 155, 166, 167, 168, 169, 170, 171,
                                      173, 174, 175, 177, 179, 194, 195, 196, 197, 198, 199, 200, 202, 205))


def fail(message):
    raise SystemExit(f"patch_rasterizer.py: {message}")


def read_bank(text):
    """The words of slangmosh.hpp's spirv_bank, and where the SMALL_TYPES=0 rasterizer's module and layout are."""
    m = re.search(r"static const uint32_t spirv_bank\[\] =\s*\{([^}]*)\};", text)
    if not m:
        fail("no spirv_bank in slangmosh.hpp")
    words = [int(w, 16) for w in re.findall(r"0x([0-9a-f]{8})u", m.group(1))]
    m = re.search(r'if \(resolver\("rasterizer", "SMALL_TYPES"\) == 0\)\s*\{\s*'
                  r"layout\.unserialize\(reflection_bank \+ (\d+), (\d+)\);\s*"
                  r"this->rasterizer = device\.request_program\(spirv_bank \+ (\d+), (\d+), &layout\);\s*\}", text)
    if not m:
        fail("slangmosh.hpp doesn't choose a SMALL_TYPES=0 rasterizer the way this script reads it")
    layout_offset, layout_size, offset, size = map(int, m.groups())
    if size % 4 or offset + size // 4 > len(words):
        fail(f"the SMALL_TYPES=0 rasterizer's module (word {offset}, {size} bytes) isn't inside spirv_bank")
    return words[offset:offset + size // 4], layout_offset, layout_size


def to_bytes(words):
    return struct.pack(f"<{len(words)}I", *words)


def instructions(module):
    """The module's instructions after its five header words, each as [opcode, operand words...]."""
    out, i = [], 5
    while i < len(module):
        count, opcode = module[i] >> 16, module[i] & 0xffff
        if count == 0 or i + count > len(module):
            fail(f"a malformed instruction at word {i}")
        out.append([opcode] + module[i + 1:i + count])
        i += count
    return out


def string_words(operands, i):
    """How many words the literal string starting at operands[i] takes, its terminating 0 byte included."""
    for n, word in enumerate(operands[i:], 1):
        if any((word >> shift) & 0xff == 0 for shift in (0, 8, 16, 24)):
            return n
    fail("a literal string without its terminating 0")


def kinds(ins):
    """The kind of each of ins's operand words, as SHAPES gives them (a string's words are each an L)."""
    opcode, operands = ins[0], ins[1:]
    if opcode == OP_SWITCH:  # the selector, the default, then (literal, label) pairs: the module's selectors are 32-bit
        if len(operands) % 2:
            fail("an OpSwitch with an odd number of operand words")
        return "II" + "LI" * ((len(operands) - 2) // 2)
    if opcode == OP_SPEC_CONSTANT_OP:  # the operation's opcode, then its operands: indexes are literals
        shape = "TRL" + {OP_COMPOSITE_EXTRACT: "I*L", OP_VECTOR_SHUFFLE: "II*L",
                         OP_COMPOSITE_INSERT: "II*L"}.get(operands[2], "*I")
    elif opcode in SHAPES:
        shape = SHAPES[opcode]
    else:
        fail(f"opcode {opcode}, which this script doesn't know")
    mask = {OP_LOAD: 3, OP_STORE: 2}.get(opcode)
    if mask is not None and len(operands) > mask and operands[mask] & ~0x7:  # not only Volatile, Aligned, Nontemporal
        fail("a memory access with <id> operands, which this script doesn't know")
    fixed, _, repeat = shape.partition("*")
    out, i = "", 0
    for kind in fixed:
        n = string_words(operands, i) if kind == "S" else 1
        out, i = out + ("L" * n if kind == "S" else kind), i + n
    if i > len(operands) or (i < len(operands) and not repeat):
        fail(f"opcode {opcode} with {len(operands)} operand words, which its shape doesn't fit")
    return out + repeat * (len(operands) - i)


def name_of(ins):
    words = ins[2:]
    return to_bytes(words).split(b"\0", 1)[0].decode()


def rewrite_spec_stores(code, bound):
    """shade_pixel's stores of OpSpecConstantOp results, each made to store the same value computed by ordinary
    instructions placed just before it; returns the new instructions and the number of stores rewritten."""
    spec = {ins[2]: ins for ins in code if ins[0] == OP_SPEC_CONSTANT_OP}
    names = {ins[1]: name_of(ins) for ins in code if ins[0] == OP_NAME}
    shade_pixel = [ins[2] for ins in code if ins[0] == OP_FUNCTION and names.get(ins[2], "").startswith("shade_pixel(")]
    if len(shade_pixel) != 1:
        fail(f"{len(shade_pixel)} functions named shade_pixel")
    fresh = iter(range(bound, 1 << 32))

    def compute(value, before):
        """The <id> of `value` computed by ordinary instructions, appended to `before`; the same <id> for anything
        that isn't an OpSpecConstantOp result (a specialization constant or an ordinary one)."""
        if value not in spec:
            return value
        result_type, operation, operands = spec[value][1], spec[value][3], spec[value][4:]
        if operation == OP_COMPOSITE_EXTRACT:  # the composite, then literal indexes
            operands = [compute(operands[0], before)] + operands[1:]
        elif operation in (OP_VECTOR_SHUFFLE, OP_COMPOSITE_INSERT):
            fail("a stored value made by OpVectorShuffle or OpCompositeInsert, which this script doesn't rewrite")
        else:
            operands = [compute(operand, before) for operand in operands]
        result = next(fresh)
        before.append([operation, result_type, result] + operands)
        return result

    out, inside, rewritten = [], False, 0
    for ins in code:
        if ins[0] == OP_FUNCTION:
            inside = ins[2] == shade_pixel[0]
        elif ins[0] == OP_FUNCTION_END:
            inside = False
        elif inside and ins[0] == OP_STORE and ins[2] in spec:
            value = compute(ins[2], out)
            ins = [OP_STORE, ins[1], value] + ins[3:]
            rewritten += 1
        out.append(ins)
    return out, rewritten


def renumber(code):
    """The instructions with their ids numbered from 1 in the order they first appear in a disassembly, where each
    instruction shows its result id before its result type; and the module's new bound."""
    numbers, slots = {}, []
    for ins in code:
        operand_kinds = kinds(ins)
        ids = [i for i, kind in enumerate(operand_kinds, 1) if kind in "TRI"]
        slots.append(ids)
        for i in sorted(ids, key=lambda i: operand_kinds[i - 1] != "R"):
            numbers.setdefault(ins[i], len(numbers) + 1)
    out = []
    for ins, ids in zip(code, slots):
        ins = list(ins)
        for i in ids:
            ins[i] = numbers[ins[i]]
        out.append(ins)
    return out, len(numbers) + 1


def patch(module):
    """The patched module's words."""
    if module[:1] != [MAGIC] or len(module) < 5:
        fail("the SMALL_TYPES=0 rasterizer isn't a SPIR-V module")
    code, rewritten = rewrite_spec_stores(instructions(module), module[3])
    if rewritten != STORES:
        fail(f"{rewritten} stores of specialization-constant operations in shade_pixel, not {STORES}")
    code, bound = renumber(code)
    words = [MAGIC, module[1], GENERATOR_SPIRV_TOOLS_ASSEMBLER, bound, 0]
    for ins in code:
        words += [(len(ins) << 16) | ins[0]] + ins[1:]
    return words


def header(bank_sha256, layout_offset, layout_size, words):
    lines = [
        "// Generated by tools/parallel-rdp/patch_rasterizer.py from slangmosh.hpp beside it: don't edit; run the",
        "// script again when slangmosh.hpp changes. slangmosh.hpp's SMALL_TYPES=0 rasterizer, with the values",
        "// shade_pixel stores from specialization-constant operations computed by ordinary instructions instead: the",
        "// same values, in a form Qualcomm's proprietary compiler accepts (the script says why). rdp_device.cpp puts",
        "// it in the bank's place, with the bank's resource layout record for that module.",
        f"// slangmosh.hpp: SHA-256 {bank_sha256}",
        f"// its SMALL_TYPES=0 rasterizer: SHA-256 {RASTERIZER_SHA256}",
        f"// this module: SHA-256 {hashlib.sha256(to_bytes(words)).hexdigest()}",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "namespace RDP",
        "{",
        f"static constexpr size_t rasterizer_patched_layout_offset = {layout_offset};",
        f"static constexpr size_t rasterizer_patched_layout_size = {layout_size};",
        "static const uint32_t rasterizer_patched_spirv[] =",
        "{",
    ]
    lines += ["\t" + " ".join(f"0x{w:08x}u," for w in words[i:i + 8]) for i in range(0, len(words), 8)]
    lines += ["};", "}"]
    return "\n".join(lines) + "\n"


def generate():
    """rasterizer_patched.hpp's text, from slangmosh.hpp."""
    data = BANK.read_bytes()
    module, layout_offset, layout_size = read_bank(data.decode())
    found = hashlib.sha256(to_bytes(module)).hexdigest()
    if found != RASTERIZER_SHA256:
        fail(f"slangmosh.hpp's SMALL_TYPES=0 rasterizer has SHA-256 {found}, not the {RASTERIZER_SHA256} this patch "
             "was made for: its shader changed, so whether Qualcomm's compiler needs the patch, and whether the "
             "patch still applies, must be established again")
    words = patch(module)
    made = hashlib.sha256(to_bytes(words)).hexdigest()
    if made != PATCHED_SHA256:
        fail(f"the patched module came out with SHA-256 {made}, not {PATCHED_SHA256}, the module tested")
    return header(hashlib.sha256(data).hexdigest(), layout_offset, layout_size, words)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="write nothing; exit 1 unless rasterizer_patched.hpp is what the script writes")
    args = parser.parse_args()
    text = generate()
    name = OUT.relative_to(ROOT)
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != text.encode():
            print(f"{name} isn't what tools/parallel-rdp/patch_rasterizer.py makes from slangmosh.hpp: run it")
            return 1
        print(f"{name}: what tools/parallel-rdp/patch_rasterizer.py makes from slangmosh.hpp, byte for byte")
        return 0
    OUT.write_bytes(text.encode())
    print(f"wrote {name}: {STORES} stores rewritten, the module's SHA-256 {PATCHED_SHA256}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
