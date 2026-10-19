#!/usr/bin/env python3
"""Round 10's CPU beside the GE (time-contention, time-contention2; docs/psp-core.md, parts 112 and 106), in Python
alone: each list's time beside each CPU pattern against the list alone (its slowdown), each pattern's turns alone and
beside the list (the CPU's slowdown), and the GE's contention terms fitted to them. A pattern's RAM traffic is its
lines of RAM a microsecond alone (its turns and their time: streaming, writing and the chain a line a turn, copying
2048 lines a turn, the mixes their misses as a working set over the data cache's 16 KiB has them, VRAM and the cache's
own hits none), u its share of streaming's flat out. The list's work that reads RAM (rho, by kind: the front end's
vertices and words, the back end's texels, transfers) is the model's own count of that list (ge-terms.py's file of the
same round, run in a runner): each list beside a pattern takes 1 + sum(K_kind x rho_kind x u) as long, K_kind
(GE::Cost::Contention) fitted by least squares on the relative error, none below 0.

usage: contention10.py PSP_RESULTS TERMS OURS   (OURS: the runner's own results of the round, for the counts' order)
prints the slowdowns by list and pattern, each pattern's traffic and the CPU's slowdown, and the fitted K"""
import importlib.util
import os
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("gefit", os.path.join(HERE, "ge-fit.py"))
gefit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gefit)

PATTERNS = ["idle", "arithmetic", "reads within 8 KiB", "streaming reads", "streaming 1/2", "streaming 1/4",
            "streaming 1/8", "streaming writes", "streaming writes 1/4", "copies", "a chain of pointers", "uncached",
            "VRAM reads", "VRAM writes", "mix 16 KiB", "mix 64 KiB", "mix 256 KiB", "mix 1 MiB"]
LISTS = ["A RAM 8888 wide", "B VRAM 8888 wide", "C no texture", "D vertices", "E1 NOPs", "E2 CALLed lists",
         "F copies RAM to VRAM", "G new textures", "H small trilinear strips", "I skinned lit strips"]
STREAMING2 = [3, 4, 5, 6]  #time-contention2's patterns 1-4: the streaming reads


def lines_per_turn(pattern):
    """RAM lines a turn of the pattern moves (0: none from RAM)"""
    if pattern in (3, 4, 5, 6, 7, 8, 10):  #streaming reads and writes, the chain: a line each
        return 1.0
    if pattern == 9:  #64 KiB read and 64 KiB written
        return 2048.0
    if pattern == 11:  #16 uncached words: a line's bytes over the bus, a word at a time
        return 1.0
    if pattern in (14, 15, 16, 17):  #two reads and two writes at random words of the set: the misses past 16 KiB
        size = {14: 16, 15: 64, 16: 256, 17: 1024}[pattern]
        return 4.0 * max(0.0, 1 - 16.0 / size)
    return 0.0  #idle, arithmetic, cached reads, VRAM


def records(path):
    data = open(path, "rb").read()
    w = struct.unpack("<%dI" % (len(data) // 4), data)
    at, out = 0, []
    while at + 4 <= len(w):
        case, size, clocks, rv = w[at:at + 4]
        repeats, values = rv & 0xffff, rv >> 16
        runs = [w[at + 4 + r * values:at + 4 + (r + 1) * values] for r in range(repeats)]
        out.append((case, size, clocks >> 16, clocks & 0xffff, runs))
        at += 4 + repeats * values
    return out


def middle(values):
    v = sorted(x for x in values if x != 0xffffffff)
    return v[len(v) // 2] if v else None


def decode(path, test):
    """{(list, pattern, size, bus): (list time us, thread turns, thread time us)}; list 0: the pattern alone"""
    first, per = gefit.CONTENTION[test]
    out = {}
    for case, size, cpu, bus, runs in records(path):
        if case < first:
            ge, pattern = 0, case + 1 if test == "time-contention" else STREAMING2[case]
        else:
            ge, pattern = (case - first) // per + 1, (case - first) % per
            if test == "time-contention2" and pattern:
                pattern = STREAMING2[pattern - 1]
        t = middle(r[0] for r in runs)
        turns = middle(r[2] for r in runs if len(r) > 3)
        spent = middle(r[3] for r in runs if len(r) > 3)
        out[(ge, pattern, size, bus)] = (t, turns, spent)
    return out


def shares(terms, ours, coef):
    """each list's (A..I) RAM-reading clocks by kind over all its clocks, from the model's count of the list alone"""
    groups = gefit.lists(terms).get("time-contention", [])
    recs = [r for r in records(os.path.join(ours, "time-contention.bin")) if gefit.listed("time-contention", r[0])]
    out, at = {}, 0
    for case, size, cpu, bus, runs in recs:
        group = groups[at + len(runs) - 1]
        at += len(runs)
        ge, pattern = (case - 17) // 18 + 1, (case - 17) % 18
        if pattern or (ge, size) in out:
            continue
        kinds = {"vertices": 0.0, "words": 0.0, "texels": 0.0, "transfers": 0.0}
        total = 0.0
        for run in group[0]:
            for k, v in run.items():
                side, name = k.split(".", 1)
                c = coef.get(name, 0) * v
                total += c
                base = name.split("[")[0]
                if base in ("VertexByte", "VertexFloor", "Vertex3", "IndexedVertex"):
                    kinds["vertices"] += c
                elif base in ("Command", "Call", "Jump"):
                    kinds["words"] += c
                elif (base == "FetchByte" and name[10] in "01") or (base in ("TexelByte", "TriangleTexelByte") and
                                                                    "[3]" not in name[len(base):len(base) + 6]):
                    kinds["texels"] += c
                elif base == "TransferByte" and name.startswith("TransferByte[0]"):
                    kinds["transfers"] += c
        out[(ge, size)] = {k: v / total for k, v in kinds.items()} if total else kinds
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    psp, terms, ours = argv
    grid = decode(os.path.join(psp, "time-contention.bin"), "time-contention")
    coef = {}
    fit_out = os.environ.get("GE_COEF")  #(a ge-fit.py output's coefficients, else GE::Cost's as the source has them)
    source = open(os.path.join(HERE, "..", "..", "ares", "psp", "ge", "ge.hpp"), encoding="utf-8").read()
    import re
    import itertools
    block = source[source.index("struct Cost {"):source.index("struct Pipe {")]
    for m in re.finditer(r"static constexpr (?:f64|s64|u64) ([^;]+);", block):
        for part in re.split(r",\s*(?=[A-Z])", m.group(1)):
            if "=" not in part:
                continue
            name, val = [x.strip() for x in part.split("=", 1)]
            if name.endswith("]"):
                nums = [float(x) for x in re.findall(r"-?[0-9.]+", val)]
                dims = [int(x) for x in re.findall(r"\[(\d+)\]", name)]
                for i, idx in enumerate(itertools.product(*[range(d) for d in dims])):
                    coef[name.split("[")[0] + "".join("[%d]" % j for j in idx)] = nums[i]
            else:
                try:
                    coef[name] = float(val)
                except ValueError:
                    pass
    if fit_out:
        for line in open(fit_out):
            m = re.match(r"\s+([A-Za-z0-9\[\]]+)\s+(-?[0-9.]+)", line)
            if m and not line.startswith("  time-"):
                coef[m.group(1)] = float(m.group(2))
    rho = shares(terms, ours, coef)
    for bus in sorted({k[3] for k in grid}):
        alone = {p: grid.get((0, p, 0, bus)) for p in range(1, 18)}
        stream = alone[3]
        if not stream or not stream[1] or not stream[2]:
            continue
        stream_rate = stream[1] / stream[2] * lines_per_turn(3)
        print("bus %d MHz: each pattern alone (turns a us), its RAM traffic as a share of streaming's (u)" % bus)
        u = {}
        for p in range(1, 18):
            a = alone[p]
            rate = a[1] / a[2] if a and a[1] and a[2] else 0
            u[p] = rate * lines_per_turn(p) / stream_rate if stream_rate else 0
            print("  %-22s %8.3f turns/us  u %.3f" % (PATTERNS[p], rate, u[p]))
        print("  each list's slowdown beside each pattern (its time over its time alone), size 1:")
        print("  %-26s" % "" + "".join("%9s" % PATTERNS[p][:8] for p in range(1, 18)))
        rows = []
        for ge in range(1, 11):
            sizes = sorted({k[2] for k in grid if k[0] == ge and k[3] == bus})
            for size in sizes[:1] + sizes[1:]:
                base = grid.get((ge, 0, size, bus))
                if not base or not base[0]:
                    continue
                line = []
                for p in range(1, 18):
                    g = grid.get((ge, p, size, bus))
                    s = g[0] / base[0] if g and g[0] else float("nan")
                    line.append(s)
                    r = rho.get((ge, size))
                    if r and s == s:
                        rows.append(({k: r[k] * u[p] for k in r}, s))
                if size == sizes[0]:
                    print("  %-26s" % LISTS[ge - 1][:26] + "".join("%9.3f" % s for s in line))
        print("  the CPU's slowdown beside each list (its turns a us alone over beside), size 1:")
        for ge in range(1, 11):
            sizes = sorted({k[2] for k in grid if k[0] == ge and k[3] == bus})
            if not sizes:
                continue
            line = []
            for p in range(1, 18):
                g, a = grid.get((ge, p, sizes[0], bus)), alone[p]
                if g and g[1] and g[2] and a and a[1] and a[2]:
                    line.append((a[1] / a[2]) / (g[1] / g[2]))
                else:
                    line.append(float("nan"))
            print("  %-26s" % LISTS[ge - 1][:26] + "".join("%9.3f" % s for s in line))
        #K by kind: slowdown - 1 = sum K_kind rho_kind u, least squares on the relative error, none below 0
        kinds = ["vertices", "words", "texels", "transfers"]
        free = list(kinds)
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
            K = dict(zip(free, gefit.gauss(A, b)))
            negative = [k for k in free if K[k] < 0]
            if not negative:
                break
            free.remove(min(negative, key=lambda k: K[k]))
        K = {k: (K.get(k, 0.0) if k in free else 0.0) for k in kinds}
        miss = [abs((1 + sum(K[k] * x[k] for k in kinds)) / s - 1) for x, s in rows]
        print("  K by kind (GE::Cost::Contention): " + ", ".join("%s %.3f" % (k, K[k]) for k in kinds) +
              "; %d list-pattern pairs, median miss %.1f%%" % (len(rows), 100 * statistics.median(miss) if miss else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
