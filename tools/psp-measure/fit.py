#!/usr/bin/env python3
# Works out how a PSP's VFPU computes its math functions, from what psp-measure recorded on one, and checks
# the result reproduces every recorded value. Needs numpy.
# usage: fit.py <results folder> (its vfpu/ when it has one, so results/ or results/vfpu)
#
# The model, as published about the hardware (PPSSPP's write-up of fp64's work) and confirmed here on our own data:
# each function reduces its argument to a 23-bit index i. The top 7 bits pick one of 128 segments, each with its
# own integers c0, m, n and base e. The low 16 bits, x2, enter a linear term in full; a squared term only sees
# x2's top 10 bits, as a distance t from the segment's middle, squared and rounded up to a multiple of 256 (u, in
# 256s). The result, in units of 2^(e - 150) (so a float's 24-bit significand when its exponent is e), is
#     c0 + floor(m * x2 / 2^17) + floor(n * u / 2^Q)
# truncated to a multiple of 4 (22 bits) in the segment's own binade, and made a float. Q is one constant, found
# here too. The table behind vsin and vcos is cosine's: sine is it read backwards. log2's table is fixed point
# instead: every segment works in units of 2^-24 (log2 over [1, 2) is between 0 and 1), and the core adds the
# input's exponent to it (see vfpuLog2() for that, and for the cheaper path the PSP takes below 1).
#
# Everything below is fitted from the measurements alone: nothing comes from PPSSPP's code or tables.

import os
import sys
import numpy as np

SEGMENT = 1 << 16
x2 = np.arange(SEGMENT)
t = (x2 >> 6) - 512
u_of_x2 = (t * t + 255) // 256
runs = x2 >> 6
t_of_run = np.arange(1024) - 512
u_of_run = (t_of_run * t_of_run + 255) // 256
u_values = np.unique(u_of_run)


def values_in_binade(bits, e):
    """The results as integers in units of 2^(e - 150), or None where that loses bits."""
    exponent = (bits >> 23) & 0xff
    significand = (bits & 0x7fffff) | 0x800000
    significand = np.where(exponent == 0, 0, significand)  # zero
    shift = exponent - e
    up = significand << np.maximum(shift, 0)
    down = significand >> np.maximum(-shift, 0)
    lost = np.where(shift < 0, significand & ((1 << np.maximum(-shift, 0)) - 1), 0)
    if (lost != 0).any():
        return None
    return np.where(shift >= 0, up, down)


def bit_length(values):
    return np.where(values > 0, np.floor(np.log2(np.maximum(values, 1))).astype(np.int64) + 1, 0)


def kept(values):
    """values with all but their top 22 significant bits cleared."""
    drop = np.maximum(bit_length(values) - 22, 0)
    return (values >> drop) << drop


def fit_segment(values, q, significant=False):
    """Finds c0, m, n reproducing a segment's 65536 values with scale q, or None."""
    a = np.stack([np.ones(SEGMENT), x2, u_of_x2], axis=1)
    coef, *_ = np.linalg.lstsq(a, values.astype(float), rcond=None)
    #what the truncation can have dropped
    granularity = 1 << np.maximum(bit_length(values) - 22, 0) if significant else np.full(SEGMENT, 4)
    for m in range(round(coef[1] * 2**17) - 2, round(coef[1] * 2**17) + 3):
        linear = np.floor(m * x2 / 2**17).astype(np.int64)
        low = values - linear  # c0 + Q(u) + linear lies in [value, value + granularity)
        high = low + granularity - 1
        run_low = np.full(1024, -2**62)
        run_high = np.full(1024, 2**62)
        np.maximum.at(run_low, runs, low)
        np.minimum.at(run_high, runs, high)
        if (run_low > run_high).any():
            continue
        u_low = np.array([run_low[u_of_run == v].max() for v in u_values])
        u_high = np.array([run_high[u_of_run == v].min() for v in u_values])
        center = coef[2] * 2**q
        for n in range(int(np.floor(center)) - 64, int(np.ceil(center)) + 65):
            square = np.floor(n * u_values / 2**q).astype(np.int64)
            c0_low = (u_low - square).max()
            c0_high = (u_high - square).min()
            if c0_low <= c0_high:
                return int(c0_low), m, n
    return None


def evaluate(c0, m, n, q, e, significant=False):
    """The model's results for one segment, as float bits: truncated to a multiple of 4 in the segment's binade,
    or with significant set, to its top 22 significant bits."""
    value = c0 + np.floor(m * x2 / 2**17).astype(np.int64) + np.floor(n * u_of_x2 / 2**q).astype(np.int64)
    value = kept(value) if significant else (value >> 2) << 2
    bits = np.zeros(SEGMENT, dtype=np.int64)
    exponent = np.full(SEGMENT, e, dtype=np.int64)
    v = value.copy()
    while True:  # normalize: values past 2^24 carry into the next binade, values below 2^23 lose leading zeros
        over = v >= 1 << 24
        if not over.any():
            break
        v = np.where(over, v >> 1, v)
        exponent = np.where(over, exponent + 1, exponent)
    while True:
        under = (v > 0) & (v < 1 << 23)
        if not under.any():
            break
        v = np.where(under, v << 1, v)
        exponent = np.where(under, exponent - 1, exponent)
    bits = np.where(v == 0, 0, (exponent << 23) | (v & 0x7fffff))
    return bits.astype(np.uint32)


def fit_function(name, indexed, q, fixed=None, significant=False):
    """Fits all 128 segments of a function whose results are given by 23-bit index; returns the table or None.
    fixed: the one base every segment works in (fixed point), instead of each segment's own binade."""
    table = []
    for segment in range(128):
        bits = indexed[segment * SEGMENT:(segment + 1) * SEGMENT].astype(np.int64)
        exponents = (bits >> 23) & 0xff
        top = int(exponents.max())
        found = None
        for e in ((fixed,) if fixed else (top, top - 1)):
            values = values_in_binade(bits, e)
            if values is None:
                continue
            found = fit_segment(values, q, significant)
            if found:
                c0, m, n = found
                if (evaluate(c0, m, n, q, e, significant) == bits.astype(np.uint32)).all():
                    table.append((c0, m, n, e))
                    break
                found = None
        if not found:
            print(f"  {name}: segment {segment} doesn't fit")
            return None
    return table


def check_log2_below_one(table, results):
    """Below 1 the PSP takes a straight line through each segment of log2's table: its first value cut to 17 bits
    after the point, its slope with the low 9 bits dropped, no squared term; and it truncates the magnitude
    |log2 x| to 15 bits after the point. True if that reproduces every result from 1/2 up to 1 (as vfpuLog2()
    computes it), so the table still serves both paths."""
    index = np.arange(1 << 23, dtype=np.int64)
    c0, m, n, _ = (np.array(column, dtype=np.int64)[index >> 16] for column in zip(*table))
    line = (((c0 + 2 * n) >> 7) << 15) + (m >> 9) * (index & 0xffff)  # units of 2^-32
    magnitude = ((1 << 32) - line) >> 17  # |log2 x| in units of 2^-15, for x = (1 + index / 2^23) / 2
    expected = (magnitude.astype(np.float64) * 2.0**-15).astype(np.float32).view(np.uint32) | 0x80000000
    return bool((expected == results).all())


def write_header(path, q, tables):
    with open(path, "w") as out:
        out.write(
            "//The VFPU's math functions as a PSP computes them: per function, 128 segments of {c0, m, n, e} for its\n"
            "//quadratic interpolator (see interpolate() in interpreter-vfpu.cpp). Generated by\n"
            "//tools/psp-measure/fit.py from results recorded on a real PSP with tools/psp-measure\n"
            "//(docs/psp-vfpu-measurements.md); every one of each function's 2^23 recorded results is reproduced\n"
            "//exactly. Don't edit: run fit.py again.\n\n"
        )
        out.write(f"static constexpr u32 InterpolatorScale = {q};  //Q: the squared term is floor(n * u / 2^Q)\n")
        for name, table in tables.items():
            out.write(f"\nstatic constexpr VFPUSegment {name}Segments[128] = {{\n")
            for i in range(0, 128, 2):
                row = ", ".join(f"{{{c0}, {m}, {n}, {e}}}" for c0, m, n, e in table[i:i + 2])
                out.write(f"  {row},\n")
            out.write("};\n")


def main():
    folder = sys.argv[1]
    if os.path.isdir(f"{folder}/vfpu"):  # psp-measure's results/ keeps the VFPU's files in vfpu/
        folder = f"{folder}/vfpu"
    header = sys.argv[2] if len(sys.argv) > 2 else None
    load = lambda name: np.fromfile(f"{folder}/{name}.bin", dtype="<u4")
    #name: results by 23-bit index, and the one base of a fixed-point table (the others are read from these:
    #vrexp2 is exp2's backwards, vsin cos's, and vnrcp and vnsin are negations)
    functions = {
        "rcp": (load("vrcp-1-2"), None),
        "rsq": (load("vrsq-1-4")[0::2], None),
        "sqrt": (load("vsqrt-1-4")[0::2], None),
        "exp2": (load("vexp2-1-2"), None),
        "cos": (load("vcos-fixed"), None),
        "asin": (load("vasin-fixed")[: 1 << 23], None),
        "log2": (load("vlog2-half-2")[1 << 23:], 126),  #from 1 up to 2; units of 2^-24 (2^(126 - 150))
    }
    for q in range(9, 15):
        tables = {}
        for name, (indexed, fixed) in functions.items():
            table = fit_function(name, indexed, q, fixed)
            print(f"Q = {q}: {name}: {'all 2^23 results reproduced' if table else 'no fit'}")
            if not table:
                break
            tables[name] = table
        if len(tables) == len(functions):
            print(f"every function fits with Q = {q}")
            below = check_log2_below_one(tables["log2"], load("vlog2-half-2")[: 1 << 23])
            print(f"log2 below 1: {'all 2^23 results reproduced' if below else 'the straight line does not fit'}")
            if not below:
                return
            if header:
                write_header(header, q, tables)
                print(f"wrote {header}")
            return


if __name__ == "__main__":
    main()
