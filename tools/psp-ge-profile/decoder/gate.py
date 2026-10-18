#!/usr/bin/env python3
"""The GE timing's gate (docs/psp-core.md, part 106; the owner's plan): each of the owner's captures of a game on his
PSP-1000 (tests/psp/measurements/ge-captures, part 103) beside Phobos's GE profile of the same scene made with the GE
timing "PSP". For each, a list's GE time (from when the GE could begin it to its FINISH, as geprof.py measures it): the
median over each one's window of whole seconds, and Phobos's miss against the PSP's: part 106's first comparison's
measure, windows and pairs. Killzone queues a scene list and a full-screen one in turn: its scene lists are judged
against each of its captures (two moments of the level), and its full-screen lists too. The two kinds are told apart by
their turns (the first comparison took lists over 5 ms, which the first model's full-screen lists were too). A scene
passes within 15% of the PSP's; none may be more than 15% too slow (a model too slow makes a game run worse than a
PSP). Captures of moments Phobos has no state of are shown, not judged.

Phobos's profiles: tools/psp-runner's --ge-timing psp --ge-profile NAME.bin over 900 frames from each scene's state,
the profile from frame 301 (--ge-profile-from 301), ahr-legal's 360 frames from frame 1 (part 106 has the commands).

usage: gate.py PROFILES_DIR [CAPTURES_DIR]   (PROFILES_DIR holds coo-diff.bin, gos-menu.bin, smba-story.bin,
kz-gp.bin, ahr-menu.bin, ahr-legal.bin, a2-gp.bin; prints the table, and exits 1 if a judged scene fails)"""
import lzma
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geprof  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURES = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tests", "psp", "measurements", "ge-captures"))
BAR = 0.15

#(what, capture, PSP window (s), Phobos scene, Phobos window (s), kind, judged)
ROWS = [
    ("Chains of Olympus, difficulty menu", "UCUS98653-5", (0, 10), "coo-diff", (0, 10), None, True),
    ("Ghost of Sparta, main menu", "UCUS98737-1", (0, 10), "gos-menu", (0, 10), None, True),
    ("Ghost of Sparta, a later scene (no state)", "UCUS98737-2", (0, 10), "gos-menu", (0, 10), None, False),
    ("Ghost of Sparta, a later scene (no state)", "UCUS98737-3", (0, 10), "gos-menu", (0, 10), None, False),
    ("Super Monkey Ball Adventure, Story Mode", "ULES00364-1", (0, 7), "smba-story", (0, 10), None, True),
    ("Killzone, gameplay: scene list", "UCUS98646-1", (0, 10), "kz-gp", (0, 10), "scene", True),
    ("Killzone, gameplay: scene list", "UCUS98646-2", (0, 10), "kz-gp", (0, 10), "scene", True),
    ("Killzone, gameplay: full-screen list", "UCUS98646-1", (0, 10), "kz-gp", (0, 10), "full", True),
    ("Killzone, gameplay: full-screen list", "UCUS98646-2", (0, 10), "kz-gp", (0, 10), "full", True),
    ("Activision Hits Remixed, 3D main menu", "ULUS10186-menu", (0, 10), "ahr-menu", (0, 10), None, True),
    ("Activision Hits Remixed, copyright screen", "ULUS10186-boot2", (3, 7), "ahr-legal", (0, 2), None, True),
    ("Activision Hits Remixed, a first boot capture cut short", "ULUS10186-boot1-cut", (0, 99), "ahr-legal", (0, 2),
     None, False),
    ("Army of Two, gameplay (another moment)", "ULUS10472-1", (0, 10), "a2-gp", (0, 10), None, False),
    ("Army of Two, gameplay (another moment)", "ULUS10472-2", (0, 10), "a2-gp", (0, 10), None, False),
]


def report(path):
    """geprof.py's report of a capture or profile (.bin, or .bin.xz)"""
    data = open(path, "rb").read()
    if path.endswith(".xz"):
        data = lzma.decompress(data)
    capture = geprof.parse_capture(data)
    return geprof.report_json(capture), capture.header


def list_time(path, window, kind):
    """the median GE time (ms) of the lists queued in the window; for Killzone, of its scene or full-screen lists"""
    if not os.path.exists(path):
        return None, None
    j, header = report(path)
    lo, hi = window[0] * 1e6, window[1] * 1e6
    lists = sorted((g for g in j["lists"] if g["queued"] is not None and lo <= g["queued"] < hi and g["ge_us"]),
                   key=lambda g: g["queued"])
    times = [g["ge_us"] for g in lists]
    if kind and len(times) > 1:  #(the half with the longer median, taken in turns, is the scene's)
        a, b = times[0::2], times[1::2]
        scene, full = (a, b) if statistics.median(a) > statistics.median(b) else (b, a)
        times = scene if kind == "scene" else full
    clocks = "%d/%d" % (j["header"]["cpuMhz"], j["header"]["busMhz"])
    return (statistics.median(times) / 1000 if times else None), clocks


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    profiles = argv[0]
    captures = argv[1] if len(argv) > 1 else CAPTURES
    print("| Scene | Capture | PSP | Phobos, PSP timing | Against the PSP | 15% bar |")
    print("| --- | --- | --- | --- | --- | --- |")
    judged, failed = [], False
    for what, capture, pwin, scene, owin, kind, judge in ROWS:
        psp, pclock = list_time(os.path.join(captures, capture + ".bin.xz"), pwin, kind)
        ours, oclock = list_time(os.path.join(profiles, scene + ".bin"), owin, kind)
        if psp is None:
            continue
        miss = None if ours is None else ours / psp - 1
        verdict = "-"
        if miss is not None:
            verdict = "passes" if abs(miss) <= BAR else ("too slow" if miss > 0 else "too fast")
            if not judge:
                verdict += " (not judged)"
            else:
                judged.append(miss)
                failed |= abs(miss) > BAR
        print("| %s | %s | %.2f ms (%s) | %s | %s | %s |" % (what, capture, psp, pclock,
              "-" if ours is None else "%.2f ms (%s)" % (ours, oclock),
              "-" if miss is None else "%+.0f%%" % (100 * miss), verdict))
    if judged:
        print("\njudged: %d; within 15%%: %d; most too slow %+.0f%%; median miss %.1f%%" % (
            len(judged), sum(abs(m) <= BAR for m in judged), 100 * max(judged),
            100 * statistics.median(abs(m) for m in judged)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
