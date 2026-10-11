#!/usr/bin/env python3
# Writes ops.h: the instructions psp-measure's instruction recorder runs on a real PSP (see vfpu.c, "The
# instruction recorder"). Each entry is one VFPU instruction, sometimes after prefix instructions, that reads
# matrix 0 (and matrix 1) and writes matrix 2.
#
# A real PSP stops a program that runs an instruction it doesn't have, so every entry is checked by pspdev's own
# assembler (psp-as): an instruction, size or operand it rejects is left out, and the words it gives are the ones
# used. Instructions the assembler can't express (vrot's every placement, vfim and viim with chosen immediates,
# prefixes with chosen fields) are built from their fields here, from instructions it did accept.
#
# Round 3's entries (ops3.h) are what round 2's prefixed entries left open: the math functions with prefixes,
# swizzles past an operand's size in the instructions that don't work lane by lane (and in t alone, in ones that
# do), and vavg and vfad with t prefixes. They're drawn from their own seed, so ops.h stays as it was.
#
# usage: ops.py [ops.h [ops3.h]]   (needs psp-as and psp-objdump on the PATH: run it where pspdev is installed)

import random
import subprocess
import sys
import tempfile
from pathlib import Path

SIZES = ("s", "p", "t", "q")


def assemble(lines):
    """The words for each line, or None for a line psp-as rejects. Each line is assembled on its own."""
    words = []
    with tempfile.TemporaryDirectory() as folder:
        for line in lines:
            source = Path(folder, "op.s")
            source.write_text(f".set noreorder\n{line}\n")
            done = subprocess.run(["psp-as", "-march=allegrex", str(source), "-o", f"{folder}/op.o"],
                                  capture_output=True)
            if done.returncode:
                words.append(None)
                continue
            dump = subprocess.run(["psp-objdump", "-d", f"{folder}/op.o"], capture_output=True, text=True).stdout
            found = [l.split("\t")[1].strip() for l in dump.splitlines() if l.strip().startswith("0:")]
            words.append(int(found[0], 16) if found else None)
    return words


def first_valid(candidates):
    """The first of several spellings that assembles, as (text, word), or None."""
    for text in candidates:
        word = assemble([text])[0]
        if word is not None:
            return text, word
    return None


def register(kind, matrix):
    """A register of matrix 0, 1 or 2 for an operand of the given kind: single, vector or matrix."""
    return {"single": f"S{matrix}00", "vector": f"C{matrix}00", "matrix": f"M{matrix}00"}[kind]


def entries():
    ops = []

    def add(text, words):
        ops.append((text, words if isinstance(words, list) else [words]))

    # Every spelling is tried with each operand a single or a vector, so the assembler picks what fits the size.
    kinds = ("single", "vector")

    def try_forms(mnemonic, size, operands):
        candidates = []
        for d in kinds:
            for s in kinds:
                for t in kinds:
                    regs = {"d": register(d, 2), "s": register(s, 0), "t": register(t, 1)}
                    candidates.append(f"{mnemonic}.{size} " + ", ".join(regs.get(o, o) for o in operands))
        found = first_valid(list(dict.fromkeys(candidates)))
        if found:
            add(*found)

    for mnemonic in ("vadd", "vsub", "vsbn", "vdiv", "vmul", "vmin", "vmax", "vscmp", "vsge", "vslt", "vdot", "vhdp",
                     "vdet", "vscl", "vcrs", "vcrsp", "vqmul"):
        for size in SIZES:
            try_forms(mnemonic, size, ("d", "s", "t"))
    conditions = ("FL", "EQ", "LT", "LE", "TR", "NE", "GE", "GT", "EZ", "EN", "EI", "ES", "NZ", "NN", "NI", "NS")
    for condition in conditions:
        for size in SIZES:
            found = first_valid([f"vcmp.{size} {condition}, {register(k, 0)}, {register(k, 1)}" for k in kinds] +
                                [f"vcmp.{size} {condition}, {register(k, 0)}" for k in kinds] +
                                [f"vcmp.{size} {condition}" ])
            if found:
                add(*found)
    #(vuc2ifs is pspdev's name for vuc2i)
    for mnemonic in ("vmov", "vabs", "vneg", "vsat0", "vsat1", "vrcp", "vrsq", "vsin", "vcos", "vexp2", "vlog2",
                     "vsqrt", "vasin", "vnrcp", "vnsin", "vrexp2", "vf2h", "vh2f", "vsbz", "vlgb", "vuc2ifs", "vc2i",
                     "vus2i", "vs2i", "vi2uc", "vi2c", "vi2us", "vi2s", "vsrt1", "vsrt2", "vsrt3", "vsrt4", "vbfy1",
                     "vbfy2", "vocp", "vsocp", "vfad", "vavg", "vsgn", "vt4444", "vt5551", "vt5650"):
        for size in SIZES:
            try_forms(mnemonic, size, ("d", "s"))
    for mnemonic in ("vidt", "vzero", "vone"):
        for size in SIZES:
            try_forms(mnemonic, size, ("d",))
    for mnemonic in ("vf2in", "vf2iz", "vf2iu", "vf2id", "vi2f"):
        for size in SIZES:
            for scale in (0, 1, 8, 16, 31):
                try_forms(mnemonic, size, ("d", "s", str(scale)))
    for mnemonic in ("vcmovt", "vcmovf"):
        for size in SIZES:
            for bit in range(7):
                try_forms(mnemonic, size, ("d", "s", str(bit)))
    for value in range(256):
        try_forms("vwbn", "s", ("d", "s", str(value)))
    for name in ("VFPU_HUGE", "VFPU_SQRT2", "VFPU_SQRT1_2", "VFPU_2_SQRTPI", "VFPU_2_PI", "VFPU_1_PI", "VFPU_PI_4",
                 "VFPU_PI_2", "VFPU_PI", "VFPU_E", "VFPU_LOG2E", "VFPU_LOG10E", "VFPU_LN2", "VFPU_LN10", "VFPU_2PI",
                 "VFPU_PI_6", "VFPU_LOG10TWO", "VFPU_LOG2TEN", "VFPU_SQRT3_2"):
        for size in SIZES:
            try_forms("vcst", size, ("d", name))
    for size in SIZES:
        found = first_valid([f"vmmul.{size} M200, M000, M100", f"vmmul.{size} M200, E000, M100"])
        if found:
            add(*found)
        for mnemonic in ("vmmov", "vmidt", "vmzero", "vmone"):
            found = first_valid([f"{mnemonic}.{size} M200, M000", f"{mnemonic}.{size} M200"])
            if found:
                add(*found)
        found = first_valid([f"vmscl.{size} M200, M000, S100"])
        if found:
            add(*found)
    for n in (2, 3, 4):
        for mnemonic in ("vtfm", "vhtfm"):
            for size in SIZES:
                found = first_valid([f"{mnemonic}{n}.{size} C200, M000, C100"])
                if found:
                    add(*found)

    # vrot: every placement (bits 16-20) for each size it has, from the word psp-as gave for one of them.
    for size in SIZES:
        found = first_valid([f"vrot.{size} C200, S000, [c,s,s,s]", f"vrot.{size} C200, S000, [c,s,s]",
                             f"vrot.{size} C200, S000, [c,s]"])
        if found:
            base = found[1] & ~(31 << 16)
            for placement in range(32):
                add(f"vrot.{size} C200, S000, placement {placement}", base | placement << 16)

    # vfim (half-float immediate) and viim (integer immediate) into S200, with chosen immediates.
    vfim = first_valid(["vfim.s S200, 1.5"])[1] & ~0xffff
    viim = first_valid(["viim.s S200, 7"])[1] & ~0xffff
    choose = random.Random(2026)
    halves = [0x0000, 0x8000, 0x0001, 0x8001, 0x03ff, 0x0400, 0x3c00, 0xbc00, 0x3555, 0x7bff, 0xfbff, 0x7c00, 0xfc00,
              0x7c01, 0x7e00, 0x7fff, 0xfc01, 0xffff, 0x4000, 0x3800]
    halves += [choose.randrange(1 << 16) for _ in range(108)]
    for half in halves:
        add(f"vfim.s S200, half {half:#06x}", vfim | half)
    for value in (0, 1, 0x7fff, 0x8000, 0xffff, 0x1234, 0x8001, 0x00ff):
        add(f"viim.s S200, {value - (1 << 16) if value & 0x8000 else value}", viim | value)

    # Prefixes before an instruction: random source (20-bit) and destination (12-bit) fields.
    bases = [first_valid([text])[1] for text in ("vadd.q C200, C000, C100", "vmul.q C200, C000, C100",
             "vmov.q C200, C000", "vdot.q S200, C000, C100", "vscl.q C200, C000, S100", "vrcp.q C200, C000",
             "vf2in.q C200, C000, 0", "vi2f.q C200, C000, 0", "vmin.q C200, C000, C100", "vadd.s S200, S000, S100",
             "vmov.p C200, C000", "vmov.t C200, C000", "vcmp.q LT, C000, C100", "vsat0.q C200, C000",
             "vhdp.q S200, C000, C100", "vfad.q S200, C000")]
    vpfxs, vpfxt, vpfxd = 0xdc000000, 0xdd000000, 0xde000000
    for i in range(240):
        base = bases[i % len(bases)]
        words = []
        if choose.random() < 0.8:
            words.append(vpfxs | choose.randrange(1 << 20))
        if choose.random() < 0.6:
            words.append(vpfxt | choose.randrange(1 << 20))
        if choose.random() < 0.6:
            words.append(vpfxd | choose.randrange(1 << 12))
        words.append(base)
        add("prefixed " + " ".join(f"{w:08x}" for w in words), words)
    return ops


def spelling(mnemonic, size, operands):
    """The first spelling psp-as accepts, each operand tried as a single or a vector, as (text, word), or None."""
    kinds = ("single", "vector")
    candidates = []
    for d in kinds:
        for s in kinds:
            for t in kinds:
                regs = {"d": register(d, 2), "s": register(s, 0), "t": register(t, 1)}
                candidates.append(f"{mnemonic}.{size} " + ", ".join(regs.get(o, o) for o in operands))
    return first_valid(list(dict.fromkeys(candidates)))


def entries3():
    ops = []
    choose = random.Random(2027)
    vpfxs, vpfxt, vpfxd = 0xdc000000, 0xdd000000, 0xde000000
    lanes = {"s": 1, "p": 2, "t": 3, "q": 4}

    def add(words):
        ops.append(("prefixed " + " ".join(f"{w:08x}" for w in words), words))

    def outside(size, which):
        """A random source prefix whose lanes in which take a lane past the operand's size (no constant there)."""
        p = choose.randrange(1 << 20)
        for lane in which:
            p &= ~(1 << (12 + lane))
            p = (p & ~(3 << (2 * lane))) | choose.randrange(size, 4) << (2 * lane)
        return p

    def inside(size):
        """A random source prefix that stays within the operand: each lane a constant or a lane below the size."""
        p = choose.randrange(1 << 20)
        for lane in range(4):
            if not p >> (12 + lane) & 1 and (p >> (2 * lane) & 3) >= size:
                p = (p & ~(3 << (2 * lane))) | choose.randrange(size) << (2 * lane)
        return p

    # The math functions with random prefixes: do they take them as vrcp does, on the last lane alone?
    for mnemonic in ("vrcp", "vnrcp", "vrsq", "vsqrt", "vexp2", "vrexp2", "vlog2", "vsin", "vnsin", "vcos", "vasin"):
        for size, count in (("q", 10), ("t", 3), ("p", 3)):
            found = spelling(mnemonic, size, ("d", "s"))
            if not found:
                continue
            for _ in range(count):
                words = [vpfxs | choose.randrange(1 << 20)]
                if choose.random() < 0.5:
                    words.append(vpfxd | choose.randrange(1 << 12))
                add(words + [found[1]])

    # Swizzles past the operand's size in the instructions that don't work lane by lane, and in t alone in some
    # that do: is the lane's result 0 there too? roles says which prefixes get such a swizzle.
    shapes = [("vdot", "p", ("d", "s", "t"), "st"), ("vdot", "t", ("d", "s", "t"), "st"),
              ("vhdp", "p", ("d", "s", "t"), "st"), ("vhdp", "t", ("d", "s", "t"), "st"),
              ("vfad", "p", ("d", "s"), "s"), ("vfad", "t", ("d", "s"), "s"),
              ("vavg", "p", ("d", "s"), "s"), ("vavg", "t", ("d", "s"), "s"),
              ("vcrs", "t", ("d", "s", "t"), "st"), ("vcrsp", "t", ("d", "s", "t"), "st"),
              ("vdet", "p", ("d", "s", "t"), "st"), ("vscl", "p", ("d", "s", "t"), "s"), ("vscl", "t", ("d", "s", "t"), "s"),
              ("vf2h", "p", ("d", "s"), "s"), ("vh2f", "s", ("d", "s"), "s"), ("vh2f", "p", ("d", "s"), "s"),
              ("vi2f", "p", ("d", "s", "0"), "s"), ("vf2iz", "t", ("d", "s", "0"), "s"), ("vbfy1", "p", ("d", "s"), "s"),
              ("vsocp", "s", ("d", "s"), "s"), ("vsocp", "p", ("d", "s"), "s"),
              ("vadd", "p", ("d", "s", "t"), "t"), ("vadd", "t", ("d", "s", "t"), "t"), ("vmul", "s", ("d", "s", "t"), "t"),
              ("vmin", "p", ("d", "s", "t"), "t"), ("vsub", "t", ("d", "s", "t"), "st")]
    forms = [(mnemonic, size, spelling(mnemonic, size, operands), "t" in operands, roles)
             for mnemonic, size, operands, roles in shapes]
    for size in ("p", "t"):
        forms.append(("vcmp", size, first_valid([f"vcmp.{size} LT, C000, C100"]), True, "st"))
    for mnemonic, size, found, has_t, roles in forms:
        if not found:
            continue
        n = lanes[size]
        cases = (["s", "s2"] if "s" in roles else []) + (["t"] if "t" in roles else []) + (["st"] if roles == "st" else [])
        for case in cases:
            words = []
            if "s" in case:
                words.append(vpfxs | outside(n, choose.sample(range(n), min(2 if case == "s2" else 1, n))))
            elif choose.random() < 0.5:
                words.append(vpfxs | inside(n))
            if "t" in case:
                words.append(vpfxt | outside(n, choose.sample(range(n), 1)))
            elif has_t and choose.random() < 0.5:
                words.append(vpfxt | inside(n))
            if choose.random() < 0.3:
                words.append(vpfxd | choose.randrange(1 << 12))
            add(words + [found[1]])

    # vavg and vfad with t prefixes (vfad's are forced to constants, round 2 found; vavg's?).
    for mnemonic, count in (("vavg", 8), ("vfad", 4)):
        for size in ("p", "t", "q"):
            found = spelling(mnemonic, size, ("d", "s"))
            if not found:
                continue
            n = lanes[size]
            for _ in range(count):
                words = [vpfxs | inside(n)] if choose.random() < 0.7 else []
                words.append(vpfxt | choose.randrange(1 << 20))
                if choose.random() < 0.3:
                    words.append(vpfxd | choose.randrange(1 << 12))
                add(words + [found[1]])
    return ops


def write(out, ops, count, table, about):
    with out.open("w") as f:
        f.write(about)
        f.write(f"#define {count} {len(ops)}\n\n")
        f.write(f"static const Op {table}[{count}] = {{\n")
        for text, words in ops:
            padded = ", ".join(f"0x{w:08x}" for w in words + [0] * (4 - len(words)))
            f.write(f"  {{{len(words)}, {{{padded}}}, \"{text}\"}},\n")
        f.write("};\n")
    print(f"{len(ops)} entries -> {out}")


def main():
    here = Path(__file__).parent
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "ops.h"
    out3 = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "ops3.h"
    write(out, entries(), "OP_COUNT", "ops",
          "//The instruction recorder's entries (vfpu.c): one VFPU instruction each, after up to three prefix\n"
          "//instructions, reading matrix 0 (and 1) and writing matrix 2. Generated by ops.py, which keeps only\n"
          "//what pspdev's assembler accepts. Don't edit: run ops.py again.\n\n")
    write(out3, entries3(), "OP3_COUNT", "ops3",
          "//Round 3's instruction recorder entries (vfpu.c, ops.py's entries3): the math functions with prefixes,\n"
          "//swizzles past an operand's size, vavg and vfad with t prefixes. Generated by ops.py, which keeps only\n"
          "//what pspdev's assembler accepts. Don't edit: run ops.py again.\n\n")


if __name__ == "__main__":
    main()
