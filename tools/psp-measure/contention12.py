#!/usr/bin/env python3
"""The CPU beside the GE fitted to rounds 10, 10b and 10c in the two forms the Super Monkey Ball refit chooses between,
and the choice between them (docs/psp-core.md, part 113; the refit's design, step 2), in Python alone.

A pair is a list's time beside one of the CPU's patterns over its time alone (s), on the PSP: round 10's ten lists
beside 17 patterns (time-contention), round 10b's six beside a CPU polling three ways, writing before the list, writing
and settling, or writing once beside it (time-contention3), round 10c's list H seven ways beside three
(time-contention4). A list's reads of RAM by kind (x) are its clocks of the kind over GE::allClocks with a PRIM's own
clocks and CALL/RET/JUMP's refetches added, the PRIM's counted with its vertices and the refetches with the words (the
core's shares under the refit), from the model's own count of round 10's lists (ge-terms.py's file; round 10c's from
H's, its vertices out of RAM or its texels gone); u is the CPU's lines a microsecond while the list drew (the thread's
turns then, each moving the lines contention11.py's traffic() counts), over streaming's alone, a line written back
weighing WritebackLine and an uncached access UncachedLine. With A = K_vertices x_vertices u + K_words x_words u and B
the same over texels and transfers, the window owes passed x (1 - 1/s), and the forms take
  the rule:    s = 1 + A + B              (one K for every kind: a line costs less as the push grows)
  the hybrid:  s = 1 / (1/(1 + A) - B)    (the back end's reads a fixed time a line), its due at most CAP x passed
Each form is fitted at its own line weights (the choice's convention, L2): K by least squares on the relative error in
s, none below 0 (the rule's exactly, the hybrid's by Levenberg-Marquardt), at each WritebackLine 0-1 and UncachedLine
0-1.5 in steps of 0.05, the pair with the least error kept; every test of a form below uses those weights, the held-out
ones fitting K again on their part. --weights puts both forms at one pair instead (the design's prototypes' figures).

Its first line names every input with its sha256 and GE::Cost's pace as parsed from ge.hpp. For each form it prints:
its misses in s and s - 1 by round, list and rate; the held-out tests; the core simulated (each pattern's turn as the
kernel works it out from the core's own instructions a turn and the lines it counts, T with the CPU's stalls beside a
busy GE, fitted here with the measuring loop's time checks as a term of their own, held at 0 or more) against all of
tests/psp/measured8.cpp's bounds for rounds 10 and 10b and the round 10c group's; the windows at or past the cap; the
gate's judged frames predicted (#279's push in part 113, its line cost scaled by K' and the lines), and from step 1's
probe each scene's windows bound by T; and the choice, H1-H5:
  H1  out of sample no worse: in every held-out test the hybrid's median |miss in s| at most the rule's + TOL_S points;
  H2  better where the forms differ: round 10's game-like rates held out, the hybrid's median |miss in s - 1| at most
      GAME_RATIO x the rule's, and its worst list's (A, D, E1, E2, F, G, H) no worse than the rule's worst;
  H3  the simulated core within all of measured8's bounds and the round 10c group's, each median of the misses' sizes
      as measured8 takes it (the upper middle);
  H4  the hybrid's cap binding nowhere: no window at or past CAP among the PSP's pairs, the simulated rounds and the
      scenes' windows (step 1's log); in the code, none in step 4's counters (--core --counters);
  H5  every judged frame within BAR (Super Monkey Ball's exception, at EXCEPTION, kept where it isn't) and Activision
      Hits' 3D menu within LIMIT, all six scenes judged: --scenes needs each one's <id>.probe or <id>.log, else the run
      ends. A scene judged on provisional inputs (a log without a probe, or a probe without its terms file) is named in
      the header, and H5 then holds so far at most (waits for step 1). Without --scenes, H5 and the choice are not
      judged. A "work" scene with half or more of its windows bound by T makes H5 wait too ("waits: the kind of ..."),
      and the choice with it, till --kind ID=work or ID=spin gives its kind as checked (a probed scene's only).
The hybrid is built if H1-H5 hold; else the rule, if it meets H3 and H5; else stop and report. After the code, H6 (the
spin test, and the refused skips' cost in part 113's eight polling games, the host's instructions at step 4g; the
counters' share of the emulated instructions a proxy): if H6 fails, the rule, if it met H3 and H5 here; else stop and
report. A window at or past the cap in step 1's log or step 4's counters fails H4.

usage: contention12.py --terms R10.TXT --ours OURS10 --turns CORE_R10 CORE_R10B [--scenes DIR [--kind ID=work|spin ...]]
       [--limit 10]
       contention12.py --core CORE_R10 CORE_R10B CORE_R10C [--counters LOG ...]
       (--psp R10 R10B R10C: the PSP's files, tests/psp/measurements's by default, packed or not; see README.md)"""
import argparse
import collections
import hashlib
import importlib.util
import itertools
import lzma
import math
import os
import re
import statistics
import struct
import sys

sys.dont_write_bytecode = True  #(the tools loaded below would leave __pycache__ beside them, which git doesn't ignore)
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
GE_HPP = os.path.join(REPO, "ares", "psp", "ge", "ge.hpp")
_spec = importlib.util.spec_from_file_location("contention11", os.path.join(HERE, "contention11.py"))
c11 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c11)
c10 = c11.c10

KINDS = c11.KINDS
MEASUREMENTS = os.path.join(REPO, "tests", "psp", "measurements")
GRID = list(itertools.product([i * 0.05 for i in range(21)], [i * 0.05 for i in range(31)]))
CAP = 0.75          #the hybrid's due at most this share of the window, s at most 4 (the PSP's largest push: 0.688)
GAME = (4, 5)       #round 10's streaming reads at a half and a quarter: game-like rates (u beside 0.23-0.47)
FLOOR = 0.03        #a miss in s - 1 only where the PSP's push is 3% or more (below it, the timer's noise)
TOL_S = 0.5         #H1's points (two decoders of the same records differ by up to 0.7 in these medians)
GAME_RATIO = 0.75   #H2
BAR = 15.0          #the gate's bar, %
EXCEPTION = -21.0   #Super Monkey Ball's exception's limit (gate.py), %
WORST_LISTS = ("A", "D", "E1", "E2", "F", "G", "H")
LISTS3 = [1, 3, 4, 6, 9, 10]  #round 10b's lists by round 10's numbers: A, C, D, E2, H, I
PATTERNS3 = ["polling a semaphore", "polling the list's sync", "polling the draw sync", "writes before",
             "writes settled", "a writer beside once"]
LISTS4 = ["H (round 10's)", "H nearest in level", "H bilinear, no levels", "H nearest, no levels", "H untextured",
          "H, vertices in VRAM", "H untextured, VRAM vertices"]
PATTERNS4 = [3, 4, 16]  #round 10c's patterns by round 10's numbers: streaming reads, at a half, the mix over 256 KiB
WRITER_LINES = 16384    #round 10b's writer: 1 MiB, each line read in and written back
#(#279's constants, which part 113's gate frames were measured with: the scenes scale #279's push by them, so these
#aren't ge.hpp's, which the refit changes)
OLD_K = (0.34, 0.27, 1.16, 0.57)
OLD_WEIGHTS = (0.0, 0.2)

VERTEX = ("VertexByte", "VertexFloor", "Vertex3", "IndexedVertex")
NOT_IN_CORE = ("Prim", "Call", "Jump", "Cut", "PaletteLoad", "PaletteByte", "Patch", "PatchVertex")
TEXTURE = ("FetchByte", "Clut32FetchByte", "TexelByte", "TriangleTexelByte", "PaletteByte", "FetchFixed")

#The gate's judged frames (part 113's table): the scene's list place (None: the one with the most clocks) of its lists
#a frame, the list's time without the CPU (master before #279) and with #279's push, the PSP's frame (both captures'
#where there are two), the frame's other lists with #279, and its frame as the gate gave it, ms; the CPU's clock, MHz
#(a probe's cpu= must be it); and how the CPU's work goes with the window: "work" (a frame's work, the same in a longer
#window) or "spin" (a loop without syscalls waiting for the GE, its instructions growing with the window). A fixed
#place counts from the terms file's first S line, so it can land on the wrong list: a list started before the save
#(running, or done with its completion still to come) has no S line in the probe's run, so the count starts at the
#first list to start after the load (Killzone's file starts at the frame's small blit list, which place 0 took for its
#3D list). So no scene drawing more than one list a frame keeps a fixed place.
Scene = collections.namedtuple("Scene", "id name lists place t0 t1 psp others frame cpu kind")
SCENES = [
    Scene("smba-story", "Super Monkey Ball, story", 1, 0, 16.64, 18.76, (23.64,), 0.0, 18.76, 222, "work"),
    Scene("gos-menu", "Ghost of Sparta, main menu", 1, 0, 27.52, 28.96, (29.62,), 0.0, 28.96, 333, "work"),
    Scene("ahr-menu", "Activision Hits, 3D menu", 3, None, 18.95, 20.28, (30.30, 30.39), 5.17 + 6.88, 32.32, 222,
          "spin"),
    Scene("ahr-legal", "Activision Hits, copyright screen", 2, None, 7.68, 7.68, (9.10,), 0.90, 8.57, 222, "work"),
    Scene("coo-diff", "Chains of Olympus, difficulty menu", 1, 0, 26.21, 27.67, (27.60,), 0.0, 27.67, 333, "work"),
    Scene("kz-gp", "Killzone, the courtyard", 2, None, 21.15, 21.79, (26.03, 25.61), 1.67, 23.46, 222, "work"),
]
CAPTURES = {"ahr-menu": ("", " (idle capture)"), "kz-gp": (" (capture 1)", " (capture 2)")}
#Assumed until step 1's probe measures them, as (instructions, the cache counts' scale). Super Monkey Ball's
#instructions a game frame: 0.52 M, Phobos's own count when #279's model (part 113) was designed, never measured again
#(the refit design's review, finding F9), to 1.0 M; its lines the runner's cache counts or part 113's own for the scene
#(8,218 + 3,439 lines read in, 3,033 written back, 1,206 uncached accesses a frame), 14% more. Activision Hits' menu's a
#1/60 s: none (bound by the time passed), or 2.15-5.55 M by 0.01 M, the refit design's range for a loop waiting for the
#GE without syscalls (about 4.3 M cycles a frame as #279's design counted it), over which its CPU's time alone runs from
#0.96 to 2.26 times the time passed (at 2.25 M, where the two meet, the stalls cut its push the most)
PROVISIONAL = {"smba-story": [(n, scale) for scale in (1.0, 1.14) for n in (0.52e6, 0.75e6, 1.0e6)],
               "ahr-menu": [(None, 1.0)] + [(2.15e6 + 0.01e6 * i, 1.0) for i in range(341)]}


def pace():
    """GE::Cost's PSP CPU (ge.hpp): cycles an instruction, a line read in, written back, an uncached access; streaming's
    cycles a line"""
    text = open(GE_HPP, encoding="utf-8").read()
    get = lambda name: float(re.search(r"\b%s = ([0-9.]+)" % name, text).group(1))  # noqa: E731
    return (get("InstructionCycles"), get("FillCycles"), get("WritebackCycles"), get("UncachedCycles")), \
        float(re.search(r"StreamLines = 1 / ([0-9.]+)", text).group(1))


PACE, STREAM = pace()


def records(path):
    """a results file's records (contention10.py's), packed as tests/psp/measurements keeps them (.xz) or not"""
    for name in (path, path + ".xz"):
        if os.path.exists(name):
            data = lzma.open(name).read() if name.endswith(".xz") else open(name, "rb").read()
            break
    else:
        sys.exit("contention12.py: no %s (or .xz)" % path)
    w = struct.unpack("<%dI" % (len(data) // 4), data)
    at, out = 0, []
    while at + 4 <= len(w):
        case, size, clocks, rv = w[at:at + 4]
        repeats, values = rv & 0xffff, rv >> 16
        out.append((case, size, clocks >> 16, clocks & 0xffff,
                    [w[at + 4 + r * values:at + 4 + (r + 1) * values] for r in range(repeats)]))
        at += 4 + repeats * values
    return out


def middles(path):
    """{(case, size, bus): (list time, the thread's turns, their time)}, each its runs' middle"""
    return {(case, size, bus): (c10.middle(r[0] for r in runs), c10.middle(r[2] for r in runs if len(r) > 3),
                                c10.middle(r[3] for r in runs if len(r) > 3))
            for case, size, cpu, bus, runs in records(path)}


def round10(path):
    """time-contention as contention10.decode() has it: {(list, pattern, size, bus): (list time, turns, their time)}"""
    return {((0, case + 1) if case < 17 else ((case - 17) // 18 + 1, (case - 17) % 18)) + (size, bus): v
            for (case, size, bus), v in middles(path).items()}


def kind_of(name):
    """the RAM read a term's clocks are, by kind (texels: not VRAM's; transfers: not VRAM to VRAM), or None"""
    base = name.split("[")[0]
    if (base == "FetchByte" and name[10] in "01") or (base == "Clut32FetchByte" and name[16] in "01") or \
            (base in ("TexelByte", "TriangleTexelByte") and "[3]" not in name[len(base):len(base) + 6]):
        return "texels"
    if base == "TransferByte" and not name.startswith("TransferByte[1][1]"):
        return "transfers"
    return None


def clocks(sides):
    """a list's clocks as the shares need them, from its terms' clocks ({"F": {term: clocks}, "B": {...}})"""
    c = dict.fromkeys(("vertices", "prim", "command", "refetch", "texels", "streamed", "transfers", "core", "texture",
                       "palette"), 0.0)
    for side, terms in sides.items():
        for name, v in terms.items():
            base = name.split("[")[0]
            c["core"] += 0.0 if base in NOT_IN_CORE else v
            c["vertices"] += v if base in VERTEX else 0.0
            c["command"] += v if base == "Command" else 0.0
            kind = kind_of(name)
            if kind:
                c[kind] += v
                c["streamed"] += v if base in ("TexelByte", "TriangleTexelByte") else 0.0
            if side == "F":
                c["prim"] += v if base == "Prim" else 0.0
                c["refetch"] += v if base in ("Call", "Jump") else 0.0
            else:
                c["texture"] += v if base in TEXTURE else 0.0
                c["palette"] += v if base == "PaletteByte" else 0.0
    return c


def shares(c, ram=(1.0, 1.0, 1.0, 1.0), untextured=False):
    """x by kind, as the refit's core counts them: ram, the share from RAM of the vertices, their PRIMs' own clocks, the
    words and the refetches (1: all; 0: VRAM); a PRIM drawing no vertices is the list's own work, RAM's"""
    d = c["core"] - (c["texture"] - c["palette"] if untextured else 0.0) + c["prim"] + c["refetch"]
    return {"vertices": (c["vertices"] * ram[0] + c["prim"] * (ram[1] if c["vertices"] else 1.0)) / d,
            "words": (c["command"] * ram[2] + c["refetch"] * ram[3]) / d,
            "texels": 0.0 if untextured else c["texels"] / d, "transfers": c["transfers"] / d}


def old_shares(c):
    """the shares #279 counted: every vertex RAM's, no PRIM's or refetch's clocks, the words alone"""
    d = c["core"]
    return {"vertices": c["vertices"] / d, "words": c["command"] / d, "texels": c["texels"] / d,
            "transfers": c["transfers"] / d}


def round10_lists(terms, ours, coef):
    """{(list, size): clocks} for round 10's lists alone, from ge-terms.py's file and the runner's own results"""
    groups = c10.gefit.lists(terms).get("time-contention", [])
    out, at = {}, 0
    for case, size, cpu, bus, runs in records(os.path.join(ours, "time-contention.bin")):
        if not c10.gefit.listed("time-contention", case):
            continue
        group = groups[at + len(runs) - 1]
        at += len(runs)
        ge, pattern = (case - 17) // 18 + 1, (case - 17) % 18
        if pattern or (ge, size) in out:
            continue
        sides = {"F": {}, "B": {}}
        for run in group[0]:
            for k, v in run.items():
                side, name = k.split(".", 1)
                sides[side][name] = sides[side].get(name, 0.0) + coef.get(name, 0) * v
        out[(ge, size)] = clocks(sides)
    return out


Pair = collections.namedtuple("Pair", "round list name pattern size bus x t s")


def pairs(psp, lists):
    """every pair of the three rounds: t, the CPU's (lines read in, written back, uncached) a microsecond beside the
    list over streaming's alone"""
    r10 = round10(os.path.join(psp[0], "time-contention.bin"))
    out = []
    for bus in (111, 166):
        stream = c11.rate(r10.get((0, 3, 0, bus)))
        if not stream:
            continue
        for ge in range(1, 11):
            for size in sorted({k[2] for k in r10 if k[0] == ge and k[3] == bus}):
                base = r10.get((ge, 0, size, bus))
                if not base or not base[0] or (ge, size) not in lists:
                    continue
                x = shares(lists[(ge, size)])
                for p in range(1, 18):
                    beside = r10.get((ge, p, size, bus))
                    if not beside or not beside[0]:
                        continue
                    f, w, c = (0.0, 0.0, 0.0) if p in c11.VRAM else c11.traffic(p)
                    rb = c11.rate(beside) / stream
                    out.append(Pair("10", ge - 1, c10.LISTS[ge - 1], p, size, bus, x, (rb * f, rb * w, rb * c),
                                    beside[0] / base[0]))
    #round 10b: polling and writing before the list move no lines while it draws; the writer beside it once its lines
    #over the longer of the list beside it and the writer
    r10b = middles(os.path.join(psp[1], "time-contention3.bin"))
    for bus in (111, 166):
        stream = c11.rate(r10.get((0, 3, 0, bus)))
        for n, ge in enumerate(LISTS3):
            first = 6 + n * 7
            for size in sorted({k[1] for k in r10b if k[0] == first and k[2] == bus}):
                base = r10b.get((first, size, bus))
                if not base or not base[0] or (ge, size) not in lists:
                    continue
                x = shares(lists[(ge, size)])
                for p in range(1, 7):
                    beside = r10b.get((first + p, size, bus))
                    if not beside or not beside[0]:
                        continue
                    t = (0.0, 0.0, 0.0)
                    if p == 6:
                        r = WRITER_LINES / max(beside[0], beside[2] or beside[0]) / stream
                        t = (r, r, 0.0)
                    out.append(Pair("10b", n, c10.LISTS[ge - 1], p, size, bus, x, t, beside[0] / base[0]))
    r10c = middles(os.path.join(psp[2], "time-contention4.bin"))
    for bus in (111, 166):
        stream = c11.rate(r10c.get((0, 0, bus)))
        for li, name in enumerate(LISTS4):
            for size in (4096, 8192):
                base = r10c.get((3 + li * 4, size, bus))
                if not base or not base[0] or (9, size) not in lists:
                    continue
                x = shares(lists[(9, size)], ram=(0.0, 0.0, 1.0, 1.0) if li >= 5 else (1.0,) * 4,
                           untextured=li in (4, 6))
                for k, p in enumerate(PATTERNS4):
                    beside = r10c.get((3 + li * 4 + 1 + k, size, bus))
                    f, w, c = c11.traffic(p)
                    rb = c11.rate(beside) / stream
                    out.append(Pair("10c", li, name, p, size, bus, x, (rb * f, rb * w, rb * c), beside[0] / base[0]))
    return out


def letter(pr):
    return pr.name.split()[0]


def game(pr):
    return pr.round == "10" and pr.pattern in GAME


def products(pr, wb, uc):
    """x by kind times u, at the line weights"""
    u = pr.t[0] + wb * pr.t[1] + uc * pr.t[2]
    return tuple(pr.x[k] * u for k in KINDS)


def due(form, K, z, cap=None):
    """the window's push over the time passed, z the list's x by kind times u"""
    a, b = K[0] * z[0] + K[1] * z[1], K[2] * z[2] + K[3] * z[3]
    if form == "rule":
        return (a + b) / (1 + a + b)
    return a / (1 + a) + b if cap is None else min(a / (1 + a) + b, cap)


def model(form, K, z):
    return 1 / (1 - due(form, K, z, CAP))


def fit_rule(rows):
    K, error = c11.nonnegative([(dict(zip(KINDS, z)), s) for z, s in rows], KINDS)
    return tuple(K[k] for k in KINDS), error


def fit_hybrid(rows, start=None):
    """least squares on the relative error in s, none below 0: Levenberg-Marquardt, the bound kept by projection"""
    def error(K):
        return sum((model("hybrid", K, z) / s - 1) ** 2 for z, s in rows)
    K = list(start or (0.8, 0.7, 0.7, 0.5))
    e, lam = error(K), 1e-3
    for _ in range(400):
        H = [[0.0] * 4 for _ in range(4)]
        g = [0.0] * 4
        for z, s in rows:
            a, b = K[0] * z[0] + K[1] * z[1], K[2] * z[2] + K[3] * z[3]
            d = 1 / (1 + a) - b
            if d <= 1 - CAP:  #(capped: no slope)
                continue
            m = 1 / d
            ja, jb = m * m / ((1 + a) ** 2 * s), m * m / s
            j = (z[0] * ja, z[1] * ja, z[2] * jb, z[3] * jb)
            r = m / s - 1
            for i in range(4):
                g[i] += j[i] * r
                for k in range(i, 4):
                    H[i][k] += j[i] * j[k]
        A = [[(H[min(i, k)][max(i, k)]) * (1 + lam if i == k else 1) + (1e-12 if i == k else 0.0) for k in range(4)]
             for i in range(4)]
        step = c10.gefit.gauss(A, [-v for v in g])
        trial = [max(0.0, K[i] + step[i]) for i in range(4)]
        et = error(trial)
        if et < e:
            small = e - et <= 1e-14 * e
            K, e, lam = trial, et, max(lam / 4, 1e-12)
            if small:
                break
        else:
            lam *= 4
            if lam > 1e8:
                break
    return tuple(K), e


def fit(form, rows, start=None):
    return fit_rule(rows) if form == "rule" else fit_hybrid(rows, start)


def fitted(form, prs, weights=None):
    """(error, WritebackLine, UncachedLine, K): the form at the weights given, or at its own best on GRID"""
    best, start = None, None
    for wb, uc in ([weights] if weights else GRID):
        K, e = fit(form, [(products(pr, wb, uc), pr.s) for pr in prs], start)
        start = K
        if best is None or e < best[0]:
            best = (e, wb, uc, K)
    return best


def pct(values):
    return 100 * statistics.median(values) if values else float("nan")


def misses(form, K, wb, uc, prs):
    """[(pair, miss in s, miss in s - 1 or None)]"""
    out = []
    for pr in prs:
        m = model(form, K, products(pr, wb, uc))
        out.append((pr, m / pr.s - 1, (m - 1) / (pr.s - 1) - 1 if pr.s - 1 >= FLOOR else None))
    return out


def report_fit(form, fit_, prs, free):
    e, wb, uc, K = fit_
    rows = misses(form, K, wb, uc, prs)
    edge = free and (wb in (GRID[0][0], GRID[-1][0]) or uc in (GRID[0][1], GRID[-1][1]))
    print("== the %s: K vertices %.3f, words %.3f, texels %.3f, transfers %.3f; WritebackLine %.2f, UncachedLine %.2f "
          "(the squared relative misses in s %.4f)%s" % (form, *K, wb, uc, e, " (at the grid's edge: widen it)" if edge
                                                          else ""))
    for rnd in ("10", "10b", "10c"):
        r = [x for x in rows if x[0].round == rnd]
        p = [abs(x[2]) for x in r if x[2] is not None]
        print("  round %-3s %3d pairs: median |miss| in s %5.2f%%, within 25%% %5.1f%%; in s-1 %5.1f%% (%d with a push "
              "of 3%% or more)" % (rnd, len(r), pct([abs(x[1]) for x in r]),
                                  100 * sum(abs(x[1]) <= 0.25 for x in r) / len(r), pct(p), len(p)))
    print("  round 10 by list, median miss: s | s-1 | s-1 at game-like rates (streaming 1/2 and 1/4)")
    for li in range(10):
        r = [x for x in rows if x[0].round == "10" and x[0].list == li]
        g = [x[2] for x in r if game(x[0]) and x[2] is not None]
        if not any(r[0][0].x.values()):
            print("    %-26s %+6.1f%% | (reads no RAM: the model pushes it nothing)" % (c10.LISTS[li],
                                                                                   pct([x[1] for x in r])))
            continue
        print("    %-26s %+6.1f%% | %+6.1f%% | %s" % (
            c10.LISTS[li], pct([x[1] for x in r]), pct([x[2] for x in r if x[2] is not None]),
            "%+6.1f%% (%+.1f%% to %+.1f%%)" % (pct(g), 100 * min(g), 100 * max(g)) if g else "-"))
    print("  round 10 by rate, median miss in s-1 over the lists (streaming reads alone at 1, 1/2, 1/4, 1/8): " +
          ", ".join("%s %+.1f%%" % (c10.PATTERNS[p], pct([x[2] for x in rows if x[0].round == "10" and
                                                          x[0].pattern == p and x[2] is not None]))
                    for p in (3, 4, 5, 6)))
    for label, sel in (("RAM-vertex", lambda x: x[0].list < 5), ("VRAM-vertex", lambda x: x[0].list >= 5)):
        r = [x for x in rows if x[0].round == "10c" and sel(x)]
        print("  round 10c's %s lists: s %+.1f%% (%+.1f%% to %+.1f%%)" % (
            label, pct([x[1] for x in r]), 100 * min(x[1] for x in r), 100 * max(x[1] for x in r)))
    return rows


TESTS = [
    ("round 10c held out", lambda p: p.round != "10c", lambda p: p.round == "10c"),
    ("round 10's game-like rates held out", lambda p: not game(p), game),
    ("list G held out", lambda p: letter(p) != "G", lambda p: letter(p) == "G"),
    ("list A held out", lambda p: letter(p) != "A", lambda p: letter(p) == "A"),
    ("list E2 held out", lambda p: letter(p) != "E2", lambda p: letter(p) == "E2"),
    ("round 10 alone fitted, round 10b tested", lambda p: p.round == "10", lambda p: p.round == "10b"),
    ("round 10 alone fitted, round 10c tested", lambda p: p.round == "10", lambda p: p.round == "10c"),
    ("bus 111 fitted, bus 166 tested", lambda p: p.bus == 111, lambda p: p.bus == 166),
    ("bus 166 fitted, bus 111 tested", lambda p: p.bus == 166, lambda p: p.bus == 111),
]


def held_out(form, fit_, prs):
    """{test: (K, (median |miss in s|, median miss in s-1, median |miss in s-1|, pairs, with a push), by list)}"""
    _, wb, uc, K0 = fit_
    out = {}
    for label, train, test in TESTS:
        K, _ = fit(form, [(products(pr, wb, uc), pr.s) for pr in prs if train(pr)], K0)
        rows = misses(form, K, wb, uc, [pr for pr in prs if test(pr)])
        push = [x[2] for x in rows if x[2] is not None]
        lists = {}
        for x in rows:
            if x[2] is not None:
                lists.setdefault(letter(x[0]), []).append(x[2])
        out[label] = (K, (pct([abs(x[1]) for x in rows]), pct(push), pct([abs(v) for v in push]), len(rows),
                          len(push)), {k: pct(v) for k, v in lists.items()})
    return out


def cpu_rows(psp10, lists):
    """round 10's CPU turns beside each list against alone, PSP cycles a turn: (list, pattern, size, bus, x, traffic,
    alone, beside)"""
    r10 = round10(os.path.join(psp10, "time-contention.bin"))
    out = []
    for bus in (111, 166):
        cpu = 222 if bus == 111 else 333
        for ge in range(1, 11):
            for size in sorted({k[2] for k in r10 if k[0] == ge and k[3] == bus}):
                if (ge, size) not in lists:
                    continue
                x = shares(lists[(ge, size)])
                for p in range(1, 18):
                    a, b = r10.get((0, p, 0, bus)), r10.get((ge, p, size, bus))
                    if not a or not b or not b[1] or not b[2] or not c11.rate(a):
                        continue
                    out.append((ge, p, size, bus, x, c11.traffic(p), cpu / c11.rate(a), cpu / c11.rate(b)))
    return out


def checks(p):
    """the measuring loop's time checks a turn of round 10's pattern p (tools/psp-measure/ge.c's masks)"""
    return 1.0 if p == 9 else 1 / 4 if p == 11 else 1 / 8 if 3 <= p <= 8 else 1 / 64


def stall_fit(rows, with_checks):
    """the CPU's extra cycles a turn beside the list: x by kind times (lines read in, written back, uncached accesses)
    times cycles each, and with_checks a term per time check held at 0 or more (the patterns moving no lines in the fit
    too); least squares on the relative error, every subset of the time-check terms at 0 tried"""
    sides = ("F.", "W.", "U.") + (("Q.",) if with_checks else ())
    names = [side + k for side in sides for k in KINDS]
    use = [r for r in rows if r[1] not in c11.VRAM and (with_checks or r[1] not in (1, 2))]

    def features(r):
        f, w, c = r[5]
        per = {"F.": f, "W.": w, "U.": c, "Q.": checks(r[1])}
        return [per[side] * r[4][k] for side in sides for k in KINDS]
    A = [[0.0] * len(names) for _ in names]
    b = [0.0] * len(names)
    for r in use:
        x, wgt = features(r), 1 / r[6] ** 2
        for i in range(len(names)):
            b[i] += wgt * x[i] * (r[7] - r[6])
            for j in range(len(names)):
                A[i][j] += wgt * x[i] * x[j]
    best = None
    held = [n for n in names if n.startswith("Q.")]
    for fixed in itertools.chain.from_iterable(itertools.combinations(held, k) for k in range(len(held) + 1)):
        idx = [i for i, n in enumerate(names) if n not in fixed]
        Af = [[A[i][j] + (1e-9 * (A[i][i] + 1) if i == j else 0.0) for j in idx] for i in idx]
        sol = dict(zip([names[i] for i in idx], c10.gefit.gauss(Af, [b[i] for i in idx])))
        if any(sol.get(n, 0.0) < 0 for n in held):
            continue
        B = {n: sol.get(n, 0.0) for n in names}
        e = sum(((sum(B[n] * v for n, v in zip(names, features(r))) - (r[7] - r[6])) / r[6]) ** 2 for r in use)
        if best is None or e < best[0]:
            best = (e, B)
        if not held:
            break
    B = best[1]
    turn = [abs((r[6] + sum(B[n] * v for n, v in zip(names, features(r)))) / r[7] - 1) for r in use]
    return B, pct(turn), 100 * sum(t <= 0.25 for t in turn) / len(turn), len(turn)


def extra(B, x, f, w, c):
    """the CPU's extra cycles beside a busy GE for lines read in, written back and uncached accesses"""
    return sum((f * B["F." + k] + w * B["W." + k] + c * B["U." + k]) * x[k] for k in KINDS)


def t_alone(n, f, w, c):
    return PACE[0] * n + PACE[1] * f + PACE[2] * w + PACE[3] * c


def core_turns(core10, core10b):
    """the core's own instructions a turn of each of round 10's patterns alone, and round 10b's writer's, by bus"""
    r10 = round10(os.path.join(core10, "time-contention.bin"))
    turns = {(p, bus): 333.0 / c11.rate(r10[(0, p, 0, bus)]) for p in range(1, 18) for bus in (111, 166)
             if c11.rate(r10.get((0, p, 0, bus)))}
    for case, size, cpu, bus, runs in records(os.path.join(core10b, "time-contention3.bin")):
        if case == 5:
            turns[("writer", bus)] = 333.0 * c10.middle(r[3] for r in runs) / c10.middle(r[2] for r in runs)
    return turns


def simulate(form, K, wb, uc, B, prs, turns):
    """[(pair, the core's s, its due uncapped)] as the kernel would work each out: the pattern's turn from the core's
    own instructions and the lines it counts, T with the stalls beside the list, flat out (bound by T); VRAM's patterns
    and round 10b's polling and writing before the list none"""
    out = []
    for pr in prs:
        if (pr.round == "10b" and pr.pattern <= 5) or (pr.round != "10b" and pr.pattern in c11.VRAM):
            out.append((pr, 1.0, 0.0))
            continue
        if pr.round == "10b":
            n, (f, w, c) = turns.get(("writer", pr.bus)), (1.0, 1.0, 0.0)
        else:
            n, (f, w, c) = turns.get((pr.pattern, pr.bus)), c11.traffic(pr.pattern)
        if not n:
            continue
        T = t_alone(n, f, w, c) + extra(B, pr.x, f, w, c)
        u = (f + wb * w + uc * c) * STREAM / T
        z = tuple(pr.x[k] * u for k in KINDS)
        out.append((pr, model(form, K, z), due(form, K, z)))
    return out


def upper_middle(values):
    """a median as tests/psp/measured8.cpp takes it: sorted, the value at half the count (the upper middle one)"""
    return sorted(values)[len(values) // 2]


def measured8(found):
    """measured8.cpp's bounds and the round 10c group's (the design's section 6) on [(pair, the core's s, the PSP's)],
    each median of the misses' sizes measured8's (upper_middle()): [(what, holds)]"""
    miss = [(pr, c / p - 1) for pr, c, p in found]
    r10 = [abs(m) for pr, m in miss if pr.round == "10"]
    r10b = [(pr, m) for pr, m in miss if pr.round == "10b"]
    r10c = [(pr, m) for pr, m in miss if pr.round == "10c"]
    out = []
    if r10:
        med = 100 * upper_middle(r10)
        out += [("round 10's %d pairs: median %.2f%% (at most 7%%)" % (len(r10), med), med <= 7),
                ("within 25%% %.1f%% (90%% or more)" % (100 * sum(m <= 0.25 for m in r10) / len(r10)),
                 sum(m <= 0.25 for m in r10) >= 0.9 * len(r10))]
    if r10b:
        def bound(pr):
            return 0.08 if pr.pattern <= 5 else 0.01 if pr.list == 1 else (0.10 if pr.size == 1 else 0.30) \
                if pr.list == 0 else 1.0
        polls = [abs(m) for pr, m in r10b if pr.pattern <= 5]
        med = 100 * upper_middle([abs(m) for _, m in r10b])
        out += [("round 10b's %d pairs: median %.2f%% (at most 1.5%%)" % (len(r10b), med), med <= 1.5),
                ("polling, writing before and settling, %d pairs within 8%% (worst %.1f%%)" % (len(polls), 100 * max(
                    polls)), max(polls) <= 0.08)]
        for li, what in ((1, "C beside the writer within 1%"), (0, "A beside the writer within 10% (size 1), 30% (2)")):
            cases = [(pr, m) for pr, m in r10b if pr.pattern == 6 and pr.list == li]
            out.append(("%s: %s" % (what, ", ".join("%d/%d %+.1f%%" % (pr.size, pr.bus, 100 * m) for pr, m in cases)),
                        all(abs(m) <= bound(pr) for pr, m in cases)))
        others = [(pr, m) for pr, m in r10b if pr.pattern == 6 and pr.list >= 2]
        if others:
            out.append(("D, E2, H and I beside the writer within 100%% (worst %+.1f%%)" % (
                100 * max((m for _, m in others), key=abs)), all(abs(m) <= bound(pr) for pr, m in others)))
    if r10c:
        ram = [m for pr, m in r10c if pr.list < 5]
        vram = [m for pr, m in r10c if pr.list >= 5]
        med = 100 * upper_middle([abs(m) for m in ram])
        out += [("round 10c's RAM-vertex lists: median %.1f%% (within 7%%)" % med, med <= 7),
                ("every RAM-vertex pair within 20%% (worst %+.1f%%)" % (100 * max(ram, key=abs)),
                 max(abs(m) for m in ram) <= 0.20),
                ("the VRAM-vertex lists within 7%% (worst %+.1f%%)" % (100 * max(vram, key=abs)),
                 max(abs(m) for m in vram) <= 0.07)]
    return out


def fail(path, at, what):
    """the run ends on a bad input: the file (and its line) named"""
    sys.exit("contention12.py: %s%s: %s" % (path, "" if at is None else ":%d" % at, what))


def field(path, at, text):
    """a name=value field, (name, value)"""
    name, eq, value = text.partition("=")
    if not (name and eq):
        fail(path, at, "%s: not name=value" % text)
    return name, value


def number(path, at, name, text, signed=False):
    """a field's value: a finite number, 0 or more unless signed"""
    try:
        v = float(text)
    except ValueError:
        v = math.nan
    if not math.isfinite(v) or (v < 0 and not signed):
        fail(path, at, "%s=%s: not a number%s" % (name, text, "" if signed else " 0 or more"))
    return v


def fields_of(path, at, texts):
    """a line's name=value fields, {name: value}; a name given twice ends the run"""
    out = {}
    for text in texts:
        name, value = field(path, at, text)
        if name in out:
            fail(path, at, "%s given twice" % name)
        out[name] = value
    return out


def text_lines(path):
    """a text file's lines with their numbers from 1, each decoded as UTF-8; a byte that isn't ends the run"""
    with open(path, "rb") as f:
        for at, raw in enumerate(f, 1):
            try:
                yield at, raw.decode("utf-8")
            except UnicodeDecodeError as e:
                fail(path, at, "byte 0x%02x at column %d: not UTF-8" % (raw[e.start], e.start + 1))


def scene_list(path, lists, place, coef):
    """a scene's list at its place in the frame (None: the one with the most clocks), averaged over its frames, from
    ge-terms.py's file of the scene's run (a file without the scene's lists, or that list without clocks, ends it)"""
    counts = [collections.Counter() for _ in range(lists)]
    seen, n = [0] * lists, -1
    for at, line in text_lines(path):
        if line.startswith("S"):
            n += 1
            seen[n % lists] += 1
        elif line.startswith("R ") and n >= 0:
            for k, v in fields_of(path, at, line.split()[1:]).items():
                if k[:2] not in ("F.", "B.") or len(k) < 3:
                    fail(path, at, "%s=%s: a term is F.<name> or B.<name>" % (k, v))
                counts[n % lists][k] += number(path, at, k, v, signed=True)
    if n + 1 < lists:
        fail(path, None, "%d lists in it (an S line each), fewer than the scene's %d a frame" % (n + 1, lists))

    def sides(at):
        out = {"F": {}, "B": {}}
        for k, v in counts[at].items():
            side, name = k.split(".", 1)
            out[side][name] = out[side].get(name, 0.0) + coef.get(name, 0) * v / max(1, seen[at])
        return out
    if place is None:
        place = max(range(lists), key=lambda at: sum(sum(s.values()) for s in sides(at).values()))
    c = clocks(sides(place))
    if not c["core"] > 0:
        fail(path, None, "no clocks for the scene's list (place %d of its %d a frame)" % (place, lists))
    return c


def cache_counts(path):
    """a runner's output's cache counts a 1/60 s: (lines read in, data's and instructions', written back, uncached
    accesses, the share of the time the GE was busy)"""
    found = re.findall(r"caches: (\d+) data lines a frame read in from RAM, (\d+) written back, (\d+) uncached "
                       r"accesses, (\d+) instruction lines read in; (\d+) moved while the GE was busy, ([\d.]+)% of "
                       r"the time", "".join(line for _, line in text_lines(path)))
    if not found:
        sys.exit("contention12.py: %s has no caches: line" % path)
    if len(found) > 1:
        fail(path, None, "%d caches: lines, not one (the runner prints it once, at the end of a run)" % len(found))
    d, w, u, i, _, busy = (float(v) for v in found[0])
    return d + i, w, u, busy / 100


PROBE_FIELDS = ("passed", "instructions", "fills", "ifills", "writebacks", "uncached", "vertex_ram", "vertex_vram",
                "prim_ram", "prim_vram", "word_ram", "word_vram", "refetch_ram", "refetch_vram", "texel_ram",
                "transfer_ram", "all")


def probe(path, sid):
    """step 1's file of a scene: its P line's cpu (MHz) and frames, and its busy windows (README.md: "What step 1
    gives"); a blank line is skipped, anything else malformed ends the run"""
    head, windows = None, []
    for at, line in text_lines(path):
        if not line.strip():
            continue
        word, *fields = line.split()
        values = fields_of(path, at, fields)
        if word == "P":
            if head is not None:
                fail(path, at, "a second P line")
            if values.get("scene") != sid:
                fail(path, at, "scene=%s, not the file's %s" % (values.get("scene", ""), sid))
            for k in ("cpu", "frames"):
                if k not in values:
                    fail(path, at, "no %s=" % k)
            head = {k: number(path, at, k, values[k]) for k in ("cpu", "frames")}
            for k in ("cpu", "frames"):
                if not head[k] > 0:
                    fail(path, at, "%s=%s: above 0 needed" % (k, values[k]))
        elif word == "W":
            missing = [k for k in PROBE_FIELDS if k not in values]
            if missing:
                fail(path, at, "a window without %s" % ", ".join(missing))
            wn = {k: number(path, at, k, v) for k, v in values.items()}
            for k in ("passed", "all"):  #(all: above 0 in practice, not by master's code; a 0 ends the run: README.md)
                if not wn[k] > 0:
                    fail(path, at, "%s=%s: above 0 needed" % (k, values[k]))
            ram = sum(wn[k] for k in ("vertex_ram", "vertex_vram", "word_ram", "word_vram", "texel_ram",
                                      "transfer_ram"))
            #(allClocks holds every ramRead() and otherWork(); 1e-6: the sums' order, and a test file's printing)
            if ram > wn["all"] * (1 + 1e-6):
                fail(path, at, "all=%s: below vertex_*, word_*, texel_ram and transfer_ram together, %.9g" % (
                    values["all"], ram))
            windows.append({k: wn[k] for k in PROBE_FIELDS})
        else:
            fail(path, at, "%s: a line is P or W" % word)
    if head is None or not windows:
        fail(path, None, "no P line" if head is None else "no W lines")
    return head, windows


def window_shares(wn):
    d = wn["all"] + wn["prim_ram"] + wn["prim_vram"] + wn["refetch_ram"] + wn["refetch_vram"]
    return {"vertices": (wn["vertex_ram"] + wn["prim_ram"]) / d, "words": (wn["word_ram"] + wn["refetch_ram"]) / d,
            "texels": wn["texel_ram"] / d, "transfers": wn["transfer_ram"] / d} if d else dict.fromkeys(KINDS, 0.0)


def bracketable(inputs):
    """whether the scenes without a terms file can be bracketed (scenes_report()): the scenes with one give #279's
    parts a front, a back and words of more than 5% each, each in one of them at least"""
    have = set()
    for got in inputs.values():
        if got["clocks"]:
            x_old = old_shares(got["clocks"])
            fo, bo = parts(OLD_K, x_old)
            have |= {k for k, part in (("front", fo), ("back", bo), ("words", OLD_K[1] * x_old["words"]))
                     if fo + bo > 0 and part / (fo + bo) > 0.05}
    return have == {"front", "back", "words"}


def scene_inputs(folder, coef):
    """{scene id: its inputs} for all six judged scenes: the judged list's clocks (from <id>.terms), the CPU's (from
    <id>.probe, step 1's, or <id>.log, a runner's output: its caches: line), and from the probe the RAM shares and the
    windows. A scene with neither a probe nor a log, or one that can't be judged, ends the run (none goes unjudged)"""
    where = "--scenes " + folder
    missing = [sc.id for sc in SCENES if not any(os.path.isfile(os.path.join(folder, sc.id + ext))
                                                 for ext in (".probe", ".log"))]
    if missing:
        fail(where, None, "%sno .probe or .log for %s (each judged scene needs one)" % (
            "" if os.path.isdir(folder) else "no such folder, so ", ", ".join(missing)))
    out = {}
    for sc in SCENES:
        terms, pr, log = (os.path.join(folder, sc.id + ext) for ext in (".terms", ".probe", ".log"))
        got = {"clocks": scene_list(terms, sc.lists, sc.place, coef) if os.path.isfile(terms) else None}
        if os.path.isfile(pr):
            head, windows = probe(pr, sc.id)
            if head["cpu"] != sc.cpu:  #(regimes() cuts by the table's clock, so a typo there can't hide)
                fail(pr, None, "cpu=%g, not the scene table's clock, %g MHz" % (head["cpu"], sc.cpu))
            frames, hz = head["frames"], head["cpu"] * 1e6
            total = {k: sum(wn[k] for wn in windows) for k in PROBE_FIELDS}
            got["cache"] = ((total["fills"] + total["ifills"]) / frames, total["writebacks"] / frames,
                            total["uncached"] / frames, total["passed"] / (frames * hz / 60))
            share = lambda a, b: total[a] / (total[a] + total[b]) if total[a] + total[b] else 1.0  # noqa: E731
            got["ram"] = (share("vertex_ram", "vertex_vram"), share("prim_ram", "prim_vram"),
                          share("word_ram", "word_vram"), share("refetch_ram", "refetch_vram"))
            per = sc.t1 / (1000 / 60) if sc.kind == "work" else 1.0
            got["cpu"] = [(total["instructions"] / frames * per, 1.0)]
            got["windows"] = windows
            got["from"] = "probe" if got["clocks"] else "probe without terms (provisional)"
        else:
            got["cache"] = cache_counts(log)
            got["cpu"] = PROVISIONAL.get(sc.id)
            got["from"] = "log (provisional)"
        out[sc.id] = got
    alone = [sid for sid, got in out.items() if not got["clocks"]]
    if alone and not bracketable(out):
        fail(where, None, "%s can't be judged: no terms file of their own, and none here whose parts bracket them" %
             ", ".join(alone))
    return out


def lines(cc, wb, uc):
    return cc[0] + wb * cc[1] + uc * cc[2]


def push_rule(t0, c):
    """the window's push P = c T0 / (T0 + P), its line cost c"""
    return (-t0 + math.sqrt(t0 * t0 + 4 * c * t0)) / 2 if c > 0 else 0.0


def push_hybrid(t0, cf, cb):
    """the hybrid's: the front end's part by the rule, the back end's a fixed time a line"""
    p = cf + cb
    for _ in range(200):
        p = cf * (t0 + p) / (t0 + p + cf) + cb
    return p


def parts(K, x):
    return K[0] * x["vertices"] + K[1] * x["words"], K[2] * x["texels"] + K[3] * x["transfers"]


def list_time(form, K, wb, uc, sc, x, x_old, cc, cut=1.0, moved=0.0, rule_texels=0.0):
    """the judged list's time, ms: #279's push's line cost c = P (P + T0) / T0 scaled by K' and the lines (at the
    form's weights against #279's), times cut; moved: a share of the texels' clocks put in the front part at
    rule_texels a share (the streamed reads as wide ones, the hybrid's Hypothesis)"""
    p_old = sc.t1 - sc.t0
    c_old = p_old * (p_old + sc.t0) / sc.t0 if p_old > 0 else 0.0
    k_old = sum(k * x_old[n] for k, n in zip(OLD_K, KINDS))
    a, b = parts(K, x)
    a, b = a + rule_texels * x["texels"] * moved, b - K[2] * x["texels"] * moved
    ratio = lines(cc, wb, uc) / lines(cc, *OLD_WEIGHTS) * cut
    if form == "rule":
        return sc.t0 + push_rule(sc.t0, c_old * (a + b) / k_old * ratio)
    return sc.t0 + push_hybrid(sc.t0, c_old * a / k_old * ratio, c_old * b / k_old * ratio)


def frame_of(sc, t):
    """the frame, ms: the gate's own where #279 pushed the list nothing (no form moves it), else the list's time with
    the others'"""
    return sc.frame if sc.t1 == sc.t0 else t + sc.others


def regimes(form, K, wb, uc, B, sc, x, x_old, cc, cpu, moved=0.0, rule_texels=0.0):
    """the list's times over the CPU's inputs (instructions, the cache counts' scale), the push cut where the CPU's time
    with the stalls beside the GE passes the window; moved and rule_texels as list_time() takes them (the Hypothesis)"""
    out = []
    for n, scale in cpu or [(None, 1.0)]:
        c2 = (cc[0] * scale, cc[1] * scale, cc[2] * scale, cc[3])
        if n is None:
            out.append(list_time(form, K, wb, uc, sc, x, x_old, c2, 1.0, moved, rule_texels))
        elif sc.kind == "work":  #(a frame's work, a frame as long as #279's list: the window grows with the push)
            per = sc.t1 / (1000 / 60)
            f, w, c = c2[0] * per, c2[1] * per, c2[2] * per
            t_side = t_alone(n, f, w, c) + extra(B, x, f, w, c)
            cut = 1.0
            for _ in range(50):
                t = list_time(form, K, wb, uc, sc, x, x_old, c2, cut, moved, rule_texels)
                cut = min(1.0, t * c2[3] * 1e-3 * sc.cpu * 1e6 / t_side)
            out.append(t)
        else:  #(spinning a 1/60 s: the CPU's time grows with the window, so the cut doesn't)
            passed = c2[3] * sc.cpu * 1e6 / 60
            alone = t_alone(n, c2[0], c2[1], c2[2])
            base = max(passed, alone)
            out.append(list_time(form, K, wb, uc, sc, x, x_old, c2, base / max(base, alone + extra(B, x, *c2[:3])),
                                 moved, rule_texels))
    return out


def judged(sid, lo, hi, limit):
    """(the whole range within the scene's bound, the whole range past it): the menu against LIMIT, Super Monkey Ball
    against its exception's limit (within 15% the exception goes; short of it, it stays), the others against the bar"""
    if sid == "ahr-menu":
        return hi <= limit, lo > limit
    if sid == "smba-story":
        return lo > EXCEPTION and hi <= BAR, hi <= EXCEPTION or lo > BAR
    return max(abs(lo), abs(hi)) <= BAR, lo > BAR or hi < -BAR


def span(lo, hi):
    """a judged figure as printed: one, or a range where its ends are 0.05 points apart or more"""
    return "%+.1f%%" % lo if abs(hi - lo) < 0.05 else "%+.1f%% to %+.1f%%" % (lo, hi)


def scenes_report(forms, B, inputs, limit):
    """each form's judged frames (H5) and its scenes' windows at or past the cap (H4): {form: (holds, waits, rows
    [(scene id, what, low, high, how)], windows capped, windows, the parts' ratios)}; how: "firm" (step 1's inputs),
    "range" (over the CPU's inputs until step 1's: wholly past its bound, it fails) or "scenario" (a memory or mix step
    1 decides between: past its bound, H5 waits)"""
    found = {}
    for form, (_, wb, uc, K) in forms.items():
        rows, capped, nwin = [], 0, 0
        ratios = {"front": [], "back": [], "words": []}
        for sc in SCENES:
            got = inputs.get(sc.id)
            if not got or not got["clocks"]:
                continue
            c = got["clocks"]
            variants = [("", got.get("ram", (1.0,) * 4))]
            if got["from"] != "probe" and sc.id == "gos-menu":
                variants += [(" (vertices in VRAM)", (0.0, 0.0, 1.0, 1.0)),
                             (" (vertices and words in VRAM)", (0.0,) * 4)]
            x_old = old_shares(c)
            for tag, ram in variants:
                times = regimes(form, K, wb, uc, B, sc, shares(c, ram), x_old, got["cache"], got["cpu"])
                for q, psp in enumerate(sc.psp):
                    figs = [100 * (frame_of(sc, t) / psp - 1) for t in times]
                    rows.append((sc.id, "%s%s%s" % (sc.name, CAPTURES.get(sc.id, ("",))[q], tag), min(figs), max(figs),
                                 "firm" if got["from"] == "probe" else "scenario" if tag else "range"))
            fo, bo = parts(OLD_K, x_old)  #(the parts' ratios, as the terms have it, for the scenes without terms)
            a, b = parts(K, shares(c))
            if fo / (fo + bo) > 0.05:
                ratios["front"].append(a / fo)
            if bo / (fo + bo) > 0.05:
                ratios["back"].append(b / bo)
            if OLD_K[1] * x_old["words"] / (fo + bo) > 0.05:
                ratios["words"].append(K[1] * shares(c)["words"] / (OLD_K[1] * x_old["words"]))
        for got in inputs.values():  #(step 1's windows, each with its own clocks: the cap's H4)
            for wn in got.get("windows", []):
                xw = window_shares(wn)
                f, w, cc = wn["fills"] + wn["ifills"], wn["writebacks"], wn["uncached"]
                T = t_alone(wn["instructions"], f, w, cc) + extra(B, xw, f, w, cc)
                u = (f + wb * w + uc * cc) * STREAM / max(wn["passed"], T)
                nwin += 1
                capped += form == "hybrid" and due(form, K, tuple(xw[k] * u for k in KINDS)) >= CAP
        for sc in SCENES:  #(no terms file: from all of #279's push its back part's to all of it its front part's)
            got = inputs.get(sc.id)
            if not got or got["clocks"] or not ratios["back"] or not ratios["front"]:
                continue
            cc = got["cache"]
            p_old = sc.t1 - sc.t0
            c_old = p_old * (p_old + sc.t0) / sc.t0
            lr = lines(cc, wb, uc) / lines(cc, *OLD_WEIGHTS)
            for q, psp in enumerate(sc.psp):
                fig = lambda p: 100 * ((sc.t0 + p + sc.others) / psp - 1)  # noqa: E731
                back = [fig(push_hybrid(sc.t0, 0.0, c_old * r * lr) if form == "hybrid" else
                            push_rule(sc.t0, c_old * r * lr)) for r in (min(ratios["back"]), max(ratios["back"]))]
                front = fig(push_rule(sc.t0, c_old * max(ratios["front"]) * lr))
                words = fig(push_rule(sc.t0, c_old * max(ratios["words"]) * lr))
                tag = " (capture %d)" % (q + 1) if len(sc.psp) > 1 else ""
                rows.append((sc.id, "%s%s, RAM vertices (bracket)" % (sc.name, tag), back[0], front, "scenario"))
                rows.append((sc.id, "%s%s, vertices and words in VRAM" % (sc.name, tag), fig(0.0), back[1], "scenario"))
                rows.append((sc.id, "%s%s, vertices in VRAM, words in RAM" % (sc.name, tag), fig(0.0),
                             max(back[1], words), "scenario"))
        fails, waits = [], []
        for sid, what, lo, hi, how in rows:
            within, past = judged(sid, lo, hi, limit)
            if within:
                continue
            (fails if how == "firm" or (how == "range" and past) else waits).append(what)
        found[form] = (fails, waits, rows, capped, nwin, ratios)
    return found


def hypothesis(forms, B, inputs):
    """the hybrid's Super Monkey Ball and menu with their streamed RAM texel reads charged as the rule charges a wide
    read (its texel K, in the saturating part) instead of a fixed time a line, through regimes() as the table's rows
    (its cut, over the CPU's inputs): [(scene, the share moved, as the hybrid has it, so)], each (low, high)"""
    _, wb, uc, K = forms["hybrid"]
    rule_texels = forms["rule"][3][2]
    out = []
    for sc in SCENES:
        got = inputs.get(sc.id)
        if sc.id not in ("smba-story", "ahr-menu") or not got or not got["clocks"]:
            continue
        c = got["clocks"]
        x, x_old = shares(c, got.get("ram", (1.0,) * 4)), old_shares(c)
        moved = c["streamed"] / c["texels"] if c["texels"] else 0.0
        figs = [[100 * (frame_of(sc, t) / sc.psp[0] - 1) for t in regimes(
            "hybrid", K, wb, uc, B, sc, x, x_old, got["cache"], got["cpu"], m, rule_texels)] for m in (0.0, moved)]
        out.append((sc.name, 100 * moved, (min(figs[0]), max(figs[0])), (min(figs[1]), max(figs[1]))))
    return out


def bound_by_t(inputs, B):
    """each scene's probe windows bound by T, the CPU's time for its instructions and stalls (T past the time passed,
    as geContendBound() has it): {scene id: (windows bound, windows, T/passed's median, largest)}"""
    out = {}
    for sid, got in inputs.items():
        r = []
        for wn in got.get("windows", []):
            f, w, cc = wn["fills"] + wn["ifills"], wn["writebacks"], wn["uncached"]
            r.append((t_alone(wn["instructions"], f, w, cc) + extra(B, window_shares(wn), f, w, cc)) / wn["passed"])
        if r:
            out[sid] = (sum(v > 1 for v in r), len(r), statistics.median(r), max(r))
    return out


def digest(path):
    """a file's sha256, or a folder's: the sha256 of sha256sum's lines for its files in the names' byte order (as
    `cd FOLDER && LC_ALL=C sh -c 'sha256sum -- * | sha256sum'` prints it: the glob sorted in the C locale too)"""
    if os.path.isdir(path):
        names = sorted(n for n in os.listdir(path) if not n.startswith(".") and os.path.isfile(os.path.join(path, n)))
        return hashlib.sha256("".join("%s  %s\n" % (digest(os.path.join(path, n)), n) for n in names).encode()
                              ).hexdigest()
    if not os.path.isfile(path):
        fail(path, None, "no such file or folder")
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def shown(path):
    """a path as the output names it: in the repository, relative to it; else absolute, normalized (no '..')"""
    full = os.path.normpath(os.path.abspath(path))
    return os.path.relpath(full, REPO) if full.startswith(REPO + os.sep) else full


def header(args, options):
    """the output's first line: every input as given, with its sha256 (a folder's: digest()), and GE::Cost's pace as
    parsed from ge.hpp, with its sha256"""
    named = []
    for opt in options:
        value = getattr(args, opt)
        if value and opt == "kind":
            named.append("--kind %s" % ", ".join("%s=%s" % given for given in value))
        elif value:
            named.append("--%s %s" % (opt, ", ".join("%s %s" % (shown(p), digest(p)) for p in (
                value if isinstance(value, list) else [value]))))
    return ("inputs (sha256; a folder's of its files' sha256sum lines): %s; GE::Cost's pace from %s %s: "
            "InstructionCycles %g, FillCycles %g, WritebackCycles %g, UncachedCycles %g, StreamLines 1/%g" % (
                "; ".join(named), shown(GE_HPP), digest(GE_HPP), *PACE, STREAM))


def fit_mode(args):
    kinds = dict(args.kind or [])  #(the scenes' kinds as checked: every use below takes them)
    SCENES[:] = [sc._replace(kind=kinds.get(sc.id, sc.kind)) for sc in SCENES]
    coef = c10.coefficients()
    inputs = scene_inputs(args.scenes, coef) if args.scenes else {}
    print(header(args, ("terms", "ours", "turns", "scenes", "kind", "psp")))
    lists = round10_lists(args.terms, args.ours, coef)
    prs = pairs(args.psp, lists)
    print("%d pairs: round 10 %d, 10b %d, 10c %d; round 10's game-like rates %d. Each form at %s." % (
        len(prs), *(sum(p.round == r for p in prs) for r in ("10", "10b", "10c")), sum(game(p) for p in prs),
        "WritebackLine %.2f, UncachedLine %.2f (--weights)" % tuple(args.weights) if args.weights else
        "its own line weights, the best on WritebackLine 0-1 and UncachedLine 0-1.5 by 0.05 (L2)"))
    rows = cpu_rows(args.psp[0], lists)
    stalls = {}
    for label, with_checks in (("(a) lines alone", False), ("(c+) and a term per time check, 0 or more", True)):
        B, med, w25, n = stall_fit(rows, with_checks)
        stalls[with_checks] = B
        print("the CPU's stalls beside a busy GE, %s: its turn's median miss %.1f%%, within 25%% %.0f%% (%d pairs); "
              "cycles a line read in / written back / an uncached access%s, at a kind's share of 1: %s" % (
                  label, med, w25, n, " / a time check" if with_checks else "", "; ".join(
                      "%s %s" % (k, "/".join("%.0f" % B[s + k] for s in ("F.", "W.", "U.") + (("Q.",) if with_checks
                                                                                           else ()))) for k in KINDS)))
    B = stalls[args.stalls == "c+"]
    print("the simulation and the scenes use %s's per-line cycles" % ("(c+)" if args.stalls == "c+" else "(a)"))
    weights = tuple(args.weights) if args.weights else None
    forms = {form: fitted(form, prs, weights) for form in ("rule", "hybrid")}
    turns = core_turns(*args.turns)
    verdict = {}
    for form, fit_ in forms.items():
        print()
        report_fit(form, fit_, prs, weights is None)
    print("\n== held out (K fitted again on the rest at the form's weights; misses on the held-out pairs): median |s| "
          "| median s-1 (|s-1|), pairs (with a push)")
    held = {form: held_out(form, fit_, prs) for form, fit_ in forms.items()}
    for label, _, _ in TESTS:
        for form in ("rule", "hybrid"):
            K, m, by = held[form][label]
            print("  %-42s %-6s K %s | %5.2f%% | %+6.1f%% (%5.1f%%), %d (%d)%s" % (
                label if form == "rule" else "", form, "/".join("%.3f" % k for k in K), *m,
                "" if not label.startswith("round 10's game") else "\n" + " " * 52 + ", ".join(
                    "%s %+.1f%%" % (k, v) for k, v in sorted(by.items(), key=lambda kv: c10.LISTS.index(
                        [n for n in c10.LISTS if n.split()[0] == kv[0]][0])))))
    h1 = [(label, held["rule"][label][1][0], held["hybrid"][label][1][0]) for label, _, _ in TESTS]
    gl = "round 10's game-like rates held out"
    worst = {form: max(abs(held[form][gl][2][k]) for k in WORST_LISTS) for form in forms}
    ratio = held["hybrid"][gl][1][2] / held["rule"][gl][1][2]
    verdict["H1"] = all(h <= r + TOL_S for _, r, h in h1)
    verdict["H2"] = ratio <= GAME_RATIO and worst["hybrid"] <= worst["rule"]
    print("\n== the core simulated against the PSP (the core's instructions a turn from %s and %s; T with the stalls)" %
          tuple(shown(p) for p in args.turns))
    sims, checks3 = {}, {}
    for form, (_, wb, uc, K) in forms.items():
        sims[form] = simulate(form, K, wb, uc, B, prs, turns)
        checks3[form] = measured8([(pr, s, pr.s) for pr, s, _ in sims[form]])
        print("  the %s: %s" % (form, "; ".join("%s%s" % (w, "" if ok else " (FAILS)") for w, ok in checks3[form])))
    print("\n== the hybrid's cap at %.2f: its due (uncapped) over the window, the largest and the windows at or past "
          "it" % CAP)
    _, wb, uc, K = forms["hybrid"]
    capped = 0
    for label, dues in (("the PSP's pairs (u beside)", [(pr, due("hybrid", K, products(pr, wb, uc))) for pr in prs]),
                        ("the simulated core", [(pr, d) for pr, _, d in sims["hybrid"]])):
        for rnd in ("10", "10b", "10c"):
            d = [(pr, v) for pr, v in dues if pr.round == rnd]
            top = max(d, key=lambda pv: pv[1])
            n = sum(v >= CAP for _, v in d)
            capped += n
            print("  %-27s round %-3s %4d windows: largest %.3f (%s beside %s, size %d, bus %d); at or past the cap %d"
                  % (label, rnd, len(d), top[1], top[0].name, c10.PATTERNS[top[0].pattern] if rnd != "10b" else
                     PATTERNS3[top[0].pattern - 1], top[0].size, top[0].bus, n))
    print("  (at 0.70 the PSP's pairs would bind in %d)" % sum(due("hybrid", K, products(pr, wb, uc)) >= 0.70
                                                              for pr in prs))
    verdict["H4"] = capped == 0
    scenes, bound = {}, {}
    if args.scenes:
        provisional = [sid for sid, got in inputs.items() if got["from"] != "probe"]
        print("\n== the gate's judged frames (%s), against the PSP's; LIMIT %+.0f%% for Activision Hits' 3D menu" % (
            "PROVISIONAL until step 1's probe and terms files: " + ", ".join(
                "%s %s" % (sid, got["from"]) for sid, got in inputs.items()) if provisional else "step 1's",
            args.limit))
        scenes = scenes_report(forms, B, inputs, args.limit)
        for form in forms:
            holds, waits, rows_, cap_n, nwin, ratios = scenes[form]
            print("  the %s:" % form)
            for sid, what, lo, hi, how in rows_:
                print("    %-62s %-17s%s" % (what, span(lo, hi), "" if how == "firm" else " (%s)" % how))
            if ratios["back"]:
                print("    (the parts' K' ratios over #279's, the scenes with terms files: front %.2f-%.2f, back "
                      "%.2f-%.2f, words %.2f-%.2f)" % (min(ratios["front"]), max(ratios["front"]), min(ratios["back"]),
                                                       max(ratios["back"]), min(ratios["words"]), max(ratios["words"])))
            if nwin:
                print("    the scenes' windows (step 1's): %d, at or past the cap %d" % (nwin, cap_n))
        for name, share, as_is, wide in hypothesis(forms, B, inputs):
            print("  the hybrid's Hypothesis (its streamed RAM texel reads, %.1f%% of the clocks, as wide ones): %s "
                  "%s as it has them, %s so" % (share, name, span(*as_is), span(*wide)))
        bound = bound_by_t(inputs, B)
        if bound:
            print("  the probes' windows bound by T (the CPU's time for its instructions and stalls past the time "
                  "passed), T/passed's median and largest:")
            for sc in SCENES:
                if sc.id in bound:
                    print("    %-36s %4d of %4d  %.2f  %.2f  (its kind: %s%s)" % (
                        sc.name, *bound[sc.id], sc.kind, ", as --kind gives it" if sc.id in kinds else ""))
        verdict["H4"] = verdict["H4"] and scenes["hybrid"][3] == 0
    print("\n== the choice (H1-H5)")
    print("  H1 %s: %s" % ("holds" if verdict["H1"] else "FAILS", "; ".join(
        "%s: rule %.2f%%, hybrid %.2f%%" % h for h in h1)))
    print("  H2 %s: game-like |s-1| hybrid over rule %.2f (at most %.2f); worst list hybrid %.1f%%, rule %.1f%%" % (
        "holds" if verdict["H2"] else "FAILS", ratio, GAME_RATIO, worst["hybrid"], worst["rule"]))
    for form in forms:
        print("  H3 for the %s %s" % (form, "holds" if all(ok for _, ok in checks3[form]) else "FAILS"))
    print("  H4 %s: no window at or past the cap among the PSP's pairs, the simulated rounds%s" % (
        "holds" if verdict["H4"] else "FAILS", " and the scenes' windows" if any(
            s[4] for s in scenes.values()) else " (the scenes' windows: step 1's log)"))
    if not args.scenes:
        for form in forms:
            print("  H5 for the %s: not judged (no --scenes)" % form)
        print("  the choice: not judged (no --scenes)")
    else:
        #(a "work" scene with half or more of its windows bound by T, its kind not given: H5 waits for it)
        unsettled = [sc for sc in SCENES if sc.kind == "work" and sc.id not in kinds and sc.id in bound and
                     2 * bound[sc.id][0] >= bound[sc.id][1]]
        kind_waits = "; ".join("the kind of %s" % sc.name for sc in unsettled)
        h5 = {}
        for form in forms:
            fails, waits = scenes[form][:2]
            if not waits and provisional:  #(a scene's inputs not step 1's: H5 holds so far at most)
                waits = ["the provisional inputs of %s" % ", ".join(provisional)]
            h5[form] = (not fails, waits)
            print("  H5 for the %s %s%s%s" % (form, "FAILS on: %s" % "; ".join(fails) if fails else "holds" + (
                " so far" if waits or unsettled else ""), " (waits for step 1: %s)" % "; ".join(waits) if waits else "",
                " (waits: %s)" % kind_waits if unsettled else ""))
        for sc in unsettled:
            print("  WARNING: %s: %d of its %d windows bound by T, and its kind is \"work\", unchecked: H5 waits for "
                  "it (once it is checked, --kind %s=work or --kind %s=spin)" % (sc.name, *bound[sc.id][:2], sc.id,
                                                                                sc.id))

        def pending(form, done=False):
            """what the form's H5 waits for: step 1, the kinds unchecked, both, or nothing"""
            return " and ".join(w for w, on in (("step 1" + (" is in" if done else ""), h5[form][1]),
                                                (kind_waits + (" is checked" if done else ""), unsettled)) if on)
        ok3 = {form: all(ok for _, ok in checks3[form]) for form in forms}
        hybrid = verdict["H1"] and verdict["H2"] and ok3["hybrid"] and verdict["H4"] and h5["hybrid"][0]
        rule = ok3["rule"] and h5["rule"][0]
        if hybrid and not pending("hybrid"):
            choice = "the hybrid"
        elif hybrid:
            choice = "the hybrid on H1-H4 and H5 so far (H5 waits for %s)" % pending("hybrid")
        elif rule:
            choice = "the rule" + (" (H5 waits for %s)" % pending("rule") if pending("rule") else "")
        elif unsettled:
            choice = "neither so far (H5 waits for %s; if it is still neither once checked, stop and report)" % (
                kind_waits)
        else:
            choice = "neither: stop and report"
        print("  the choice: %s. The rule as the fallback if H6 fails after the code: %s." % (choice, (
            "yes: it met H3 and H5" if not pending("rule") else "only if H5 holds for it once %s" % pending(
                "rule", True)) if rule else "no (it fails %s here%s): if H6 fails, stop and report" % (" and ".join(
                    h for h, ok in (("H3", ok3["rule"]), ("H5", h5["rule"][0])) if not ok),
                    ", H5 waiting for %s" % kind_waits if unsettled and not h5["rule"][0] else "")))
    print("\n== GE::Cost for each form (the stalls: %s)" % ("(c+)" if args.stalls == "c+" else "(a)"))
    for form, (_, wb, uc, K) in forms.items():
        print("  the %s: Contention[4] = {%.3f, %.3f, %.3f, %.3f}; WritebackLine = %.2f, UncachedLine = %.2f%s; the "
              "stalls a line read in, written back, an uncached access: %s" % (
                  form, *K, wb, uc, "; the cap %.2f" % CAP if form == "hybrid" else "", "; ".join(
                      "%s %.0f/%.0f/%.0f" % (k, B["F." + k], B["W." + k], B["U." + k]) for k in KINDS)))
    return 0


def slowdowns(path, first, row):
    """{(case, size, bus): the list's time beside the pattern over alone} (measured8.cpp's)"""
    t = {k: v[0] for k, v in middles(path).items()}
    return {(case, size, bus): v / t[(case - (case - first) % row, size, bus)] for (case, size, bus), v in t.items()
            if case >= first and (case - first) % row and v and t.get((case - (case - first) % row, size, bus))}


COUNTERS = re.compile(r"ge contention: (\d+) windows capped; (\d+) spin skips refused, (\d+) instructions run in them, "
                      r"of (\d+)")
REFUSAL_COST = 0.5  #H6: the refused skips' turns at most this share of a polling game's instructions, %


def counter_lines(logs):
    """each runner output's "ge contention:" line, which the refit's runner prints once, at the end of a run with the
    PSP's timing (README.md): [(log, N windows capped, M spin skips refused, I instructions run in them, R run)]"""
    out = []
    for log in logs:
        found = COUNTERS.findall("".join(line for _, line in text_lines(log)))
        if len(found) != 1:
            fail(log, None, "%s \"ge contention:\" lines, not one (the runner prints it once, at the end of a run)" %
                 len(found))
        out.append((log, *(int(g) for g in found[0])))
    return out


def core_mode(args):
    """the core's own run of the three rounds against the PSP's files, and the build's counters"""
    counted = counter_lines(args.counters) if args.counters else []
    print(header(args, ("psp", "core", "counters")))
    found = []
    for at, (rnd, name, first, row) in enumerate((("10", "time-contention", 17, 18), ("10b", "time-contention3", 6, 7),
                                                  ("10c", "time-contention4", 3, 4))):
        ours = slowdowns(os.path.join(args.core[at], name + ".bin"), first, row)
        for (case, size, bus), s in slowdowns(os.path.join(args.psp[at], name + ".bin"), first, row).items():
            if (case, size, bus) not in ours:
                continue
            li, p = (case - first) // row, (case - first) % row
            label = c10.LISTS[li] if rnd == "10" else c10.LISTS[LISTS3[li] - 1] if rnd == "10b" else LISTS4[li]
            pr = Pair(rnd, li, label, PATTERNS4[p - 1] if rnd == "10c" else p, size, bus, None, None, s)
            found.append((pr, ours[(case, size, bus)], s))
    print("the core's run (%s) against the PSP's (%s): %d pairs" % (", ".join(shown(p) for p in args.core),
                                                                     ", ".join(shown(p) for p in args.psp), len(found)))
    for rnd in ("10", "10b", "10c"):
        print("  round %s by list, the core's over the PSP's, median miss in s | in s-1 (a push of 3%% or more):" % rnd)
        for _, name in sorted({(pr.list, pr.name) for pr, _, _ in found if pr.round == rnd}):
            m = [(c / p - 1, (c - 1) / (p - 1) - 1 if p - 1 >= FLOOR else None) for pr, c, p in found
                 if pr.round == rnd and pr.name == name]
            push = [b for _, b in m if b is not None]
            print("    %-28s %+6.1f%% | %s" % (name, pct([a for a, _ in m]), "%+6.1f%%" % pct(push) if push else "-"))
    print("  round 10 by rate, median miss in s-1 over the lists: " + ", ".join(
        "%s %+.1f%%" % (c10.PATTERNS[p], pct([(c - 1) / (s - 1) - 1 for pr, c, s in found
                                             if pr.round == "10" and pr.pattern == p and s - 1 >= FLOOR]))
        for p in (3, 4, 5, 6)))
    bounds = measured8(found)
    print("H3 in the code (measured8's bounds and the round 10c group's) %s" % (
        "holds" if all(ok for _, ok in bounds) else "FAILS"))
    for what, ok in bounds:
        print("  %s%s" % (what, "" if ok else " (FAILS)"))
    if counted:
        capped, refused, skips_all = 0, [], 0
        for log, n, skips, run, total in counted:
            capped += n
            skips_all += skips
            refused.append(100 * run / total if total else 0.0)
            print("  %s: %d windows capped; %d spin skips refused, their turns %.3f%% of the instructions run" % (
                shown(log), n, skips, refused[-1]))
        print("H4 in the code %s: %d windows at or past the cap (a window there fails H4)" % (
            "holds" if capped == 0 else "FAILS", capped))
        print("H6's proxy %s: the refused skips' turns up to %.2f%% of a run's emulated instructions (the limit "
              "%.1f%%; %s)" % ("holds" if max(refused) <= REFUSAL_COST else "FAILS", max(refused), REFUSAL_COST,
                       "skips were refused, so step 4g's host measurement decides H6" if skips_all else
                       "no skip refused"))
    return 0


def kind_given(text):
    """--kind's ID=work or ID=spin: (scene id, kind)"""
    sid, _, kind = text.partition("=")
    if sid not in [sc.id for sc in SCENES] or kind not in ("work", "spin"):
        raise argparse.ArgumentTypeError("%s: ID=work or ID=spin, ID one of %s" % (text, ", ".join(
            sc.id for sc in SCENES)))
    return sid, kind


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawTextHelpFormatter,
                                 epilog="README.md (\"The GE timing's fit\") has the commands and what step 1 gives.")
    ap.add_argument("--psp", nargs=3, metavar=("R10", "R10B", "R10C"),
                    default=[os.path.join(MEASUREMENTS, "ge-round" + r) for r in ("10", "10b", "10c")],
                    help="the PSP's files of rounds 10, 10b and 10c (default: tests/psp/measurements')")
    ap.add_argument("--terms", help="ge-terms.py's file of a runner's run of round 10 (the lists' terms)")
    ap.add_argument("--ours", help="that run's results/ge")
    ap.add_argument("--turns", nargs=2, metavar=("CORE_R10", "CORE_R10B"),
                    help="the core's own run of rounds 10 and 10b (results/ge): its instructions a turn alone")
    ap.add_argument("--scenes", help="a folder of each judged scene's <id>.terms and <id>.probe (step 1's) or <id>.log")
    ap.add_argument("--kind", action="append", type=kind_given, metavar="ID=work|spin",
                    help="a probed scene's kind as checked, once a scene (work: a frame's work; spin: a loop waiting "
                    "for the GE): H5 waits for an unchecked \"work\" scene with its windows bound by T; a scene judged "
                    "on its log keeps the table's kind")
    ap.add_argument("--limit", type=float, default=10.0,
                    help="LIMIT for Activision Hits' 3D menu, %% (the owner's: 10)")
    ap.add_argument("--weights", nargs=2, type=float, metavar=("WB", "UC"),
                    help="both forms at these line weights instead of each at its own (the prototypes' checks)")
    ap.add_argument("--stalls", choices=("c+", "a"), default="c+",
                    help="the stalls' per-line cycles: with the time-check term (c+, the default) or without (a)")
    ap.add_argument("--core", nargs=3, metavar=("CORE_R10", "CORE_R10B", "CORE_R10C"),
                    help="the core's own run of the three rounds (results/ge) against the PSP's, with measured8's "
                    "bounds")
    ap.add_argument("--counters", nargs="+", metavar="LOG", help="with --core: runner outputs with the build's "
                    "\"ge contention:\" line (H4 in the code; the refused skips for H6)")
    args = ap.parse_args(argv)
    if args.kind and (not args.scenes or args.core or len(dict(args.kind)) < len(args.kind)):
        ap.error("--kind goes with --scenes, once a scene")
    for sid, _ in args.kind or []:
        if not os.path.isfile(os.path.join(args.scenes, sid + ".probe")):
            ap.error("--kind %s: no %s.probe in --scenes %s, and --kind is for a probed scene only (one judged on its "
                     "log keeps the table's kind, the one its provisional CPU inputs are counted in)" % (
                         sid, sid, args.scenes))
    if args.core:
        return core_mode(args)
    if not (args.terms and args.ours and args.turns):
        ap.error("--terms, --ours and --turns are needed (or --core)")
    return fit_mode(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
