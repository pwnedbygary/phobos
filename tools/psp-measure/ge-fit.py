#!/usr/bin/env python3
"""The GE timing model's fit (GE::Cost, docs/psp-core.md parts 76 and 106) to the owner's timed lists, in Python alone
(no numpy). Inputs, for each round (6, 8 and 9): the terms of each run of the GE as a runner counted them (ge-terms.py's
PHOBOS_GE_TERMS file, the program's round run with the GE timing "PSP"), that run's own results folder (its records
say how many runs each case had) and the PSP's results folder (the median of each case's runs is the time to fit).

Each timed list (queued at the timed buffer with the timing callbacks; lists queued with it at once join it) is the GE's
runs from its queueing to the next list's, and the lists before a test's results file (its "O" line) are that test's,
in its records' order; the time-* tests' (whose first value a run is the list's time). A list's time in bus clocks (microseconds times the bus clock's MHz) is fitted as List for each
list started plus, for each run, the longer of its front end's terms and its back end's (the core's pipeline gives the
same for a list of one kind of work; it lets the two ends overlap only as far as its queue). The fit is a least squares
on the relative error; each run's longer end is taken from the last fit's coefficients until no run changes ends, as
part 76 did; a term that would go below 0 is left out (0) and the rest fitted again. Lists whose CPU moves their stall
address (time-lists' and time-lists2's cases 2 and 3) are left out; time-anchors (round 8's cases again, on round 9's
day) and the cases OUTSIDE names are checks, not fitted: their misses are shown apart.

usage: ge-fit.py PSP_RESULTS TERMS OURS [TERMS OURS ...] [--fix TERM=VALUE,...] [--show N]
prints each term's fitted value (GE::Cost's names), the median miss by test, and the furthest lists"""
import os
import statistics
import struct
import sys

UNFINISHED = 0xffffffff
SPACING = 16384 * 4  #ge.c's ListSpacing: case 1's lists, queued at once
CHECKS = {"time-anchors"}
#Cases whose texels the model can't follow, shown as checks: 3D squares in perspective (round 8's time-textures 13-15
#and round 6's time-fill 39: v changes along each pixel row; time-fill 38, the same at w 1, takes 1.8 times as long)
#and turned a quarter (time-textures 16-18: the rows run down the texture's columns), which the PSP reads at a fraction
#of the wide rows' rate, and mipmapped squares (time-textures 10-12: level 1 of a 256x256 texture, 4.5 times a 1:1
#strip as wide) (docs/psp-core.md, part 106).
OUTSIDE = {("time-textures", c) for c in range(10, 19)} | {("time-fill", 39)}


def records(path):
    data = open(path, "rb").read()
    w = struct.unpack("<%dI" % (len(data) // 4), data)
    at, out = 0, []
    while at + 4 <= len(w):
        case, size, clocks, rv = w[at:at + 4]
        repeats, values = rv & 0xffff, rv >> 16
        out.append((case, size, clocks >> 16, clocks & 0xffff,
                    [w[at + 4 + r * values:at + 4 + (r + 1) * values] for r in range(repeats)]))
        at += 4 + repeats * values
    return out


def median_time(runs):
    v = [r[0] for r in runs if r[0] != UNFINISHED]
    return statistics.median(v) if v else None


def lists(path):
    """the timed lists of each test: {test: [[runs, lists], ...]}, each run {term: count}"""
    queued = {}
    for line in open(path):
        if line.startswith("Q "):
            p = line.split()
            if int(p[3]) >= 0:
                queued[(int(p[1], 16), int(p[3]))] = queued.get((int(p[1], 16), int(p[3])), 0) + 1
    (timed, callbacks), _ = max(queued.items(), key=lambda kv: kv[1])
    tests, groups, current, test = {}, [], None, None
    for line in open(path):
        tag = line[0]
        if tag == "Q":
            p = line.split()
            address, cb = int(p[1], 16), int(p[3])
            if cb == callbacks and address == timed:
                current = [[], 1]
                groups.append(current)
            elif cb == callbacks and current is not None and 0 < address - timed < 8 * SPACING \
                    and (address - timed) % SPACING == 0:
                current[1] += 1
            else:
                current = None
        elif tag == "R" and current is not None:
            current[0].append({k: float(v) for k, v in (kv.split("=") for kv in line.split()[1:])})
        elif tag == "O" and "/results/ge/" in line and line.strip().endswith(".part"):
            if test and groups:
                tests[test] = groups
            test, groups, current = os.path.basename(line.strip()).split(".")[0], [], None
    if test and groups:
        tests[test] = groups
    return tests


def load(psp, pairs):
    recs = []
    for terms, ours in pairs:
        for test, groups in lists(terms).items():
            path = os.path.join(ours, test + ".bin")
            if not test.startswith("time-") or not os.path.exists(path):  #(cpu-contention's first value is the CPU's)
                continue
            mine = records(path)
            if sum(len(r[4]) for r in mine) != len(groups):
                print("%s: %d lists counted, %d runs recorded: left out" % (test, len(groups),
                                                                             sum(len(r[4]) for r in mine)))
                continue
            #(the PSP reads its CPU's clock back as 221 at 333/222/166 and 265 at 266/133, where Phobos says 222, 266)
            theirs = {(c, s, cpu, bus): runs for c, s, cpu, bus, runs in records(os.path.join(psp, test + ".bin"))}
            at = 0
            for c, s, cpu, bus, runs in mine:
                group = groups[at + len(runs) - 1]  #(the last run: the cache as a run finds it)
                at += len(runs)
                if test in ("time-lists", "time-lists2") and c in (2, 3):
                    continue
                read = {(222, 166): 221, (266, 133): 265}.get((cpu, bus), cpu)
                psp_runs = theirs.get((c, s, read, bus)) or theirs.get((c, s, cpu, bus))
                t = median_time(psp_runs) if psp_runs else None
                if t is None or t <= 0 or not group[0]:
                    continue
                sides = [({k[2:]: v for k, v in run.items() if k.startswith("F.")},
                          {k[2:]: v for k, v in run.items() if k.startswith("B.")}) for run in group[0]]
                recs.append({"key": (test, c, s, cpu, bus), "y": t * bus, "lists": group[1], "runs": sides,
                             "check": test in CHECKS or (test, c) in OUTSIDE})
    return recs


def gauss(A, b):
    n = len(b)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        if abs(M[c][c]) < 1e-300:
            continue
        for r in range(n):
            if r != c and M[r][c]:
                k = M[r][c] / M[c][c]
                for j in range(c, n + 1):
                    M[r][j] -= k * M[c][j]
    return [M[i][n] / M[i][i] if abs(M[i][i]) > 1e-300 else 0.0 for i in range(n)]


def solve(rows, fixed):
    """least squares on relative error over rows (counts, y), coefficients >= 0"""
    free = sorted({k for counts, _ in rows for k in counts} - set(fixed))
    while True:
        idx = {k: i for i, k in enumerate(free)}
        A = [[0.0] * len(free) for _ in free]
        b = [0.0] * len(free)
        for counts, y in rows:
            w = 1 / y ** 2
            target = y - sum(fixed[k] * v for k, v in counts.items() if k in fixed)
            items = [(idx[k], v) for k, v in counts.items() if k in idx]
            for i, vi in items:
                b[i] += w * vi * target
                for j, vj in items:
                    A[i][j] += w * vi * vj
        for i in range(len(free)):
            A[i][i] += 1e-9 * (A[i][i] + 1)
        coef = dict(zip(free, gauss(A, b)))
        negative = [(v, k) for k, v in coef.items() if v < 0]
        if not negative:
            coef.update(fixed)
            return coef
        free.remove(min(negative)[1])


def charge(F, coef):
    return sum(coef.get(k, 0) * v for k, v in F.items())


def predict(rec, coef):
    return coef.get("List", 0) * rec["lists"] + sum(max(charge(F, coef), charge(B, coef)) for F, B in rec["runs"])


def fit(recs, fixed):
    coef, sides = {}, None  #(the first fit takes both ends' terms of every run, which gives each term a start)
    for _ in range(50):
        rows = []
        for r in recs:
            counts = {"List": r["lists"]}
            for n, (F, B) in enumerate(r["runs"]):
                for side in ((F, B) if sides is None else (F,) if sides[len(rows)][n] else (B,)):
                    for k, v in side.items():
                        counts[k] = counts.get(k, 0) + v
            rows.append((counts, r["y"]))
        coef = solve(rows, fixed)
        new = [[charge(F, coef) > charge(B, coef) for F, B in r["runs"]] for r in recs]
        if new == sides:
            break
        sides = new
    return coef


def main(argv):
    fixed, show, args = {}, 0, []
    i = 0
    while i < len(argv):
        if argv[i] == "--fix":
            for kv in argv[i + 1].split(","):
                k, v = kv.split("=")
                fixed[k] = float(v)
            i += 2
        elif argv[i] == "--show":
            show, i = int(argv[i + 1]), i + 2
        else:
            args.append(argv[i])
            i += 1
    if len(args) < 3 or len(args) % 2 == 0:
        print(__doc__)
        return 2
    recs = load(args[0], list(zip(args[1::2], args[2::2])))
    checks = [r for r in recs if r["check"]]
    recs = [r for r in recs if not r["check"]]
    coef = fit(recs, fixed)
    seen = {"List"} | {k for r in recs for F, B in r["runs"] for k in list(F) + list(B)}
    for k in sorted(seen | set(coef)):
        note = "  (fixed)" if k in fixed else "" if k in coef else "  (left out: it would go below 0)"
        print("  %-26s %10.4f%s" % (k, coef.get(k, 0), note))
    print("  (no list here counts any other of GE::Cost's terms)")
    errors = [(predict(r, coef) / r["y"] - 1, r) for r in recs]
    misses = sorted(abs(e) for e, _ in errors)
    print("lists %d: median miss %.1f%%, within 10%% %.0f%%, within 25%% %.0f%%" % (
        len(misses), 100 * statistics.median(misses), 100 * sum(m <= .1 for m in misses) / len(misses),
        100 * sum(m <= .25 for m in misses) / len(misses)))
    by = {}
    for e, r in errors:
        by.setdefault(r["key"][0], []).append(e)
    for test in sorted(by):
        v = sorted(abs(e) for e in by[test])
        print("  %-16s %4d lists: median miss %5.1f%%, within 10%% %3.0f%%" % (test, len(v), 100 * statistics.median(v),
                                                                            100 * sum(m <= .1 for m in v) / len(v)))
    for e, r in sorted(errors, key=lambda x: -abs(x[0]))[:show]:
        print("    %+6.0f%% %s" % (100 * e, r["key"]))
    if checks:
        print("checks (not fitted):")
        for r in checks:
            print("  %-16s case %2d size %4d at %d/%d: %+6.1f%%" % (r["key"][0], r["key"][1], r["key"][2], r["key"][3],
                                                                  r["key"][4], 100 * (predict(r, coef) / r["y"] - 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
