#!/usr/bin/env python3
"""The CPU beside the GE by its data cache's traffic (docs/psp-core.md, part 113), in Python alone: round 10's
time-contention fitted again with each pattern's traffic as Phobos's model of the CPU's data cache counts it
(Allegrex::Cache: 16 KiB of 64-byte lines, two ways, a line read in for a read or a write that misses). A turn of
each pattern: streaming reads and the chain read in a line; streaming writes read in a line and write back one;
copies read in 2048 (the 64 KiB read, and the 64 KiB written, each line read in before it's written) and write back
1024; uncached reads reach RAM 16 times, a word at a time; the mixes touch two random lines of their set, each
missing as often as a set past 16 KiB has them over the cache (1 - 16 KiB / its size), each written in its turn and so
written back as it goes; the rest move nothing (arithmetic, the cache's own hits, VRAM: not RAM). A pattern's traffic
u is its (lines read in + WritebackLine x written back + UncachedLine x uncached accesses) a microsecond alone on the
PSP, as a share of streaming's; each list beside each pattern takes 1 + sum(K_kind x rho_kind x u) as long, rho the
list's RAM-reading clocks by kind (contention10.py: the model's own count of it). K by kind, WritebackLine and
UncachedLine are fitted together over both bus clocks' pairs (least squares on the relative error, none below 0; the
two weights over a grid). Then the PSP's CPU time for a turn alone, in its cycles (its clock over its turns a
microsecond), against what the core counts for one (its own run of the round: its instructions, a cycle of its 333 a
microsecond each, and the traffic above): InstructionCycles x instructions + FillCycles x lines read in +
WritebackCycles x written back + UncachedCycles x uncached accesses, least squares on the relative error (VRAM's
patterns left out: their misses aren't RAM's, which the model doesn't count).

With --core, the core's own run of rounds 10 and 10b with the term on against the PSP's instead: each list's time beside
each pattern over its time alone, here and on the PSP (time-contention's ten lists and 17 patterns; time-contention3's
six lists beside a CPU polling three ways, writing before the list, writing and settling, and writing once beside it).

usage: contention11.py PSP_RESULTS TERMS OURS   (as contention10.py's)
       contention11.py --core CORE_R10 PSP_R10 [CORE_R10B PSP_R10B]   (folders of the rounds' files)
prints each pattern's traffic and u, the fitted K and weights with the fit's misses by list, the CPU's cycles a turn on
the PSP against the fit, and GE::Cost's lines; with --core, the slowdowns side by side and their misses by list"""
import importlib.util
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("contention10", os.path.join(HERE, "contention10.py"))
c10 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c10)

KINDS = ["vertices", "words", "texels", "transfers"]
CACHE_KIB = 16
VRAM = (12, 13)


def traffic(pattern):
    """(lines read in, lines written back, uncached accesses) a turn of the pattern moves, as the model counts them"""
    if pattern in (3, 4, 5, 6, 10):
        return 1.0, 0.0, 0.0
    if pattern in (7, 8):
        return 1.0, 1.0, 0.0
    if pattern == 9:
        return 2048.0, 1024.0, 0.0
    if pattern == 11:
        return 0.0, 0.0, 16.0
    if pattern in (14, 15, 16, 17):
        size = {14: 16, 15: 64, 16: 256, 17: 1024}[pattern]
        miss = 2 * max(0.0, 1 - CACHE_KIB / size)
        return miss, miss, 0.0
    return 0.0, 0.0, 0.0


def rate(entry):
    """turns a microsecond, from a grid entry (list time, turns, their time)"""
    return entry[1] / entry[2] if entry and entry[1] and entry[2] else 0.0


def nonnegative(rows, names):
    """least squares on the relative error for rows [(x by name, target)], 1 + sum(c x) against the target, none
    below 0: (the coefficients, the sum of squared relative errors)"""
    free = list(names)
    while free:
        A = [[0.0] * len(free) for _ in free]
        b = [0.0] * len(free)
        for x, s in rows:
            for i, ki in enumerate(free):
                b[i] += x[ki] * (s - 1) / s ** 2
                for j, kj in enumerate(free):
                    A[i][j] += x[ki] * x[kj] / s ** 2
        for i in range(len(free)):
            A[i][i] += 1e-12 * (A[i][i] + 1)
        c = dict(zip(free, c10.gefit.gauss(A, b)))
        negative = [k for k in free if c[k] < 0]
        if not negative:
            break
        free.remove(min(negative, key=lambda k: c[k]))
    c = {k: (c.get(k, 0.0) if k in free else 0.0) for k in names}
    error = sum(((1 + sum(c[k] * x[k] for k in names)) / s - 1) ** 2 for x, s in rows)
    return c, error


PATTERNS3 = ["polling a semaphore", "polling the list's sync", "polling the draw sync", "writes before",
             "writes settled", "a writer beside once"]
LISTS3 = ["A RAM 8888 wide", "C no texture", "D vertices", "E2 CALLed lists", "H small trilinear strips",
          "I skinned lit strips"]


def slowdowns(path, first, row):
    """{(list, pattern, size, bus): the list's time beside the pattern over its time alone}, lists from 1"""
    times = {}
    for case, size, cpu, bus, runs in c10.records(path):
        if case < first:
            continue
        ge, pattern = (case - first) // row + 1, (case - first) % row
        times[(ge, pattern, size, bus)] = c10.middle(r[0] for r in runs)
    return {k: t / times[(k[0], 0, k[2], k[3])] for k, t in times.items()
            if k[1] and t and times.get((k[0], 0, k[2], k[3]))}


def against_core(argv):
    """the core's slowdowns against the PSP's, by list"""
    for name, first, row, lists, patterns, at in [("time-contention", 17, 18, c10.LISTS, c10.PATTERNS[1:], 0),
                                                  ("time-contention3", 6, 7, LISTS3, PATTERNS3, 2)]:
        if len(argv) < at + 2:
            continue
        ours = slowdowns(os.path.join(argv[at], name + ".bin"), first, row)
        theirs = slowdowns(os.path.join(argv[at + 1], name + ".bin"), first, row)
        keys = sorted(k for k in ours if k in theirs)
        print("%s: %d list-pattern pairs, the core's slowdown against the PSP's (size 1 at 111 MHz shown)" %
              (name, len(keys)))
        for ge in sorted({k[0] for k in keys}):
            misses = [ours[k] / theirs[k] - 1 for k in keys if k[0] == ge]
            shown = [k for k in keys if k[0] == ge and k[2] == min(x[2] for x in keys if x[0] == ge) and k[3] == 111]
            print("  %-26s median %+6.1f%% (%+6.1f%% to %+6.1f%%):" % (lists[ge - 1], 100 * statistics.median(misses),
                                                                     100 * min(misses), 100 * max(misses)) +
                  "".join(" %s %.2f/%.2f" % (patterns[k[1] - 1][:12], ours[k], theirs[k]) for k in shown))
        every = [abs(ours[k] / theirs[k] - 1) for k in keys]
        print("  median miss %.1f%%, within 10%%: %d%%, within 25%%: %d%%" %
              (100 * statistics.median(every), 100 * sum(e <= 0.1 for e in every) // len(every),
               100 * sum(e <= 0.25 for e in every) // len(every)))
    return 0


def main(argv):
    if argv[:1] == ["--core"] and len(argv) in (3, 5):
        return against_core(argv[1:])
    if len(argv) != 3:
        print(__doc__)
        return 2
    psp, terms, ours = argv
    grid = c10.decode(os.path.join(psp, "time-contention.bin"), "time-contention")
    core = c10.decode(os.path.join(ours, "time-contention.bin"), "time-contention")
    rho = c10.shares(terms, ours, c10.coefficients())
    buses = sorted({k[3] for k in grid})
    #each pair: the list's rho by kind, the pattern, its traffic a microsecond over streaming's (read in, written back,
    #uncached), the list's slowdown
    pairs = []
    for bus in buses:
        stream = rate(grid.get((0, 3, 0, bus)))
        if not stream:
            continue
        for p in range(1, 18):
            r = rate(grid.get((0, p, 0, bus))) / stream
            f, w, c = traffic(p)
            for ge in range(1, 11):
                for size in sorted({k[2] for k in grid if k[0] == ge and k[3] == bus}):
                    base, beside = grid.get((ge, 0, size, bus)), grid.get((ge, p, size, bus))
                    if not base or not base[0] or not beside or not beside[0] or (ge, size) not in rho:
                        continue
                    pairs.append((ge, rho[(ge, size)], p, (r * f, r * w, r * c), beside[0] / base[0]))
    best = None
    saturations = [float(x) for x in os.environ.get("SATURATIONS", "0").split(",")]
    for saturation in saturations:
        for wi in range(41):
            for ui in range(41):
                weight, uncached = wi * 0.05, ui * 0.005
                rows = []
                for _, x, _, t, s in pairs:
                    v = t[0] + weight * t[1] + uncached * t[2]
                    rows.append(({k: x[k] * v / (1 + saturation * v) for k in KINDS}, s))
                K, error = nonnegative(rows, KINDS)
                if best is None or error < best[0]:
                    best = (error, weight, uncached, saturation, K)
    _, weight, uncached, saturation, K = best
    print("saturation %.2f (of %s)" % (saturation, ",".join("%g" % s for s in saturations)))
    share = lambda t: t[0] + weight * t[1] + uncached * t[2]
    effect = lambda t: share(t) / (1 + saturation * share(t))
    print("the data cache's traffic a turn (read in, written back, uncached) and u with the fitted weights:")
    for p in range(1, 18):
        us = []
        for bus in buses:
            stream = rate(grid.get((0, 3, 0, bus)))
            us.append(share(traffic(p)) * rate(grid.get((0, p, 0, bus))) / stream if stream else 0)
        print("  %-22s %7.2f %7.2f %5.1f   u %s" % ((c10.PATTERNS[p],) + traffic(p) +
                                                   (" ".join("%.3f" % x for x in us),)))
    miss = {}
    for ge, x, p, t, s in pairs:
        miss.setdefault(ge, []).append((1 + sum(K[k] * x[k] for k in KINDS) * effect(t)) / s - 1)
    every = [m for ms in miss.values() for m in ms]
    print("K by kind: " + ", ".join("%s %.3f" % (k, K[k]) for k in KINDS) +
          "; WritebackLine %.2f, UncachedLine %.3f; %d list-pattern pairs (bus %s), median miss %.1f%%" %
          (weight, uncached, len(pairs), "/".join(str(b) for b in buses), 100 * statistics.median(abs(m) for m in every)))
    for ge in sorted(miss):
        ms = miss[ge]
        print("  %-26s median %+6.1f%%, %+6.1f%% to %+6.1f%%" %
              (c10.LISTS[ge - 1], 100 * statistics.median(ms), 100 * min(ms), 100 * max(ms)))
    #the PSP's CPU time for a turn alone against the core's instructions for it and its traffic
    rows = []
    for bus in buses:
        for p in range(1, 18):
            if p in VRAM:
                continue
            ours_rate, theirs = rate(core.get((0, p, 0, bus))), rate(grid.get((0, p, 0, bus)))
            if not ours_rate or not theirs:
                continue
            rows.append((p, bus, 333.0 / ours_rate, traffic(p), 2 * bus / theirs))
    names = ["instruction", "fill", "writeback", "uncached"]
    A = [[0.0] * 4 for _ in names]
    b = [0.0] * 4
    for _, _, n, t, cycles in rows:
        x = [n, t[0], t[1], t[2]]
        for i in range(4):
            b[i] += x[i] * cycles / cycles ** 2
            for j in range(4):
                A[i][j] += x[i] * x[j] / cycles ** 2
    for i in range(4):
        A[i][i] += 1e-12 * (A[i][i] + 1)
    pace = c10.gefit.gauss(A, b)
    print("the CPU's cycles a turn alone, the PSP's against InstructionCycles %.3f, FillCycles %.1f, WritebackCycles "
          "%.1f, UncachedCycles %.1f (the core's instructions a turn):" % tuple(pace))
    for p, bus, n, t, cycles in rows:
        model = pace[0] * n + pace[1] * t[0] + pace[2] * t[1] + pace[3] * t[2]
        print("  %-22s bus %3d  %9.1f instructions  PSP %10.1f cycles  model %10.1f  %+6.1f%%" %
              (c10.PATTERNS[p], bus, n, cycles, model, 100 * (model / cycles - 1)))
    stream = statistics.mean(2 * bus / rate(grid.get((0, 3, 0, bus))) for bus in buses)
    print("GE::Cost: Contention[4] = {%.2f, %.2f, %.2f, %.2f}; StreamLines = 1 / %.1f, WritebackLine = %.2f, "
          "UncachedLine = %.3f; InstructionCycles = %.2f, FillCycles = %.0f, WritebackCycles = %.0f, UncachedCycles = "
          "%.0f" % (K["vertices"], K["words"], K["texels"], K["transfers"], stream, weight, uncached, *pace))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
