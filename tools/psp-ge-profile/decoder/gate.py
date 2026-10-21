#!/usr/bin/env python3
"""The GE timing's gate (docs/psp-core.md, part 106; the owner's plan): each of the owner's captures of a game on his
PSP-1000 (tests/psp/measurements/ge-captures, parts 103 and 110) beside Phobos's GE profile of the same scene made with
the GE timing "PSP". A list's GE time is from when the GE could begin it to its FINISH, as geprof.py measures it. Lists
are paired by their place in the frame: a frame is the lists queued between two flips (a sceDisplaySetFrameBuf to
another buffer than the call before, or every call where a game keeps one buffer, as Killzone does: geprof.py's flips),
a list's place its order among them; in the frames holding the scene's usual count of lists (the commonest), each
place's median GE time over the window of whole seconds, the PSP's against Phobos's, and the median of the frames'
totals the same way (Killzone's scene list and full-screen list are its places 1 and 2). The owner's rule (2026-10-18,
20:35): each judged scene is judged by its whole frame, which passes within 15% of the PSP's either way (none more than
15% too slow: a model too slow makes a game run worse than a PSP); each list is reported, not judged. A frame the owner
has accepted as an exception (EXCEPTED: his words, the date, which way it may miss and how far) is shown and doesn't
fail the gate within its limit; any other judged frame outside the bar does. Killzone is judged at kz-gp's moment, the
courtyard standing still
(Friday's -1 and Sunday's -1, the owner confirmed); captures of moments Phobos has no state of are shown, not judged.

Phobos's profiles: tools/psp-runner's --ge-timing psp --ge-profile NAME.bin over 900 frames from each scene's state,
the profile from frame 301 (--ge-profile-from 301), ahr-legal's 360 frames from frame 1 (part 106 has the commands).

usage: gate.py PROFILES_DIR [CAPTURES_DIR]   (PROFILES_DIR holds coo-diff.bin, gos-menu.bin, smba-story.bin,
kz-gp.bin, ahr-menu.bin, ahr-legal.bin, a2-gp.bin; prints the table, and exits 1 if a judged frame fails)"""
import bisect
import lzma
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geprof  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURES = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tests", "psp", "measurements", "ge-captures"))
BAR = 0.15

#(what, capture, PSP window (s), Phobos scene, Phobos window (s), judged). The -r9 captures are the owner's of Sunday's
#round 9 (Killzone in one session, 60, 78, 159, 322 and 443 s in; Activision Hits' 3D menu idle at the ahr-menu state's
#moment); Killzone's courtyard, standing still, is kz-gp's moment (Friday's -1 and Sunday's -1), the rest of the level
#more demanding gameplay Phobos has no state of.
ROWS = [
    ("Chains of Olympus, difficulty menu", "UCUS98653-5", (0, 10), "coo-diff", (0, 10), True),
    ("Ghost of Sparta, main menu", "UCUS98737-1", (0, 10), "gos-menu", (0, 10), True),
    ("Ghost of Sparta, a later scene (another moment)", "UCUS98737-2", (0, 10), "gos-menu", (0, 10), False),
    ("Ghost of Sparta, a later scene (another moment)", "UCUS98737-3", (0, 10), "gos-menu", (0, 10), False),
    ("Super Monkey Ball Adventure, Story Mode", "ULES00364-1", (0, 7), "smba-story", (0, 10), True),
    ("Killzone, the courtyard", "UCUS98646-1", (0, 10), "kz-gp", (0, 10), True),
    ("Killzone, the courtyard", "UCUS98646-r9-1", (0, 10), "kz-gp", (0, 10), True),
    ("Killzone, later in the level (another moment)", "UCUS98646-2", (0, 10), "kz-gp", (0, 10), False),
    ("Killzone, 78 s in (another moment)", "UCUS98646-r9-2", (0, 10), "kz-gp", (0, 10), False),
    ("Killzone, 159 s in (another moment)", "UCUS98646-r9-3", (0, 10), "kz-gp", (0, 10), False),
    ("Killzone, 322 s in (another moment)", "UCUS98646-r9-4", (0, 10), "kz-gp", (0, 10), False),
    ("Killzone, 443 s in (another moment)", "UCUS98646-r9-5", (0, 10), "kz-gp", (0, 10), False),
    ("Activision Hits Remixed, 3D main menu", "ULUS10186-menu", (0, 10), "ahr-menu", (0, 10), True),
    ("Activision Hits Remixed, 3D main menu, idle", "ULUS10186-r9-1", (0, 10), "ahr-menu", (0, 10), True),
    ("Activision Hits Remixed, copyright screen", "ULUS10186-boot2", (3, 7), "ahr-legal", (0, 2), True),
    ("Activision Hits Remixed, a first boot capture cut short", "ULUS10186-boot1-cut", (0, 99), "ahr-legal", (0, 2),
     False),
    ("Army of Two, gameplay (another moment)", "ULUS10472-1", (0, 10), "a2-gp", (0, 10), False),
    ("Army of Two, gameplay (another moment)", "ULUS10472-2", (0, 10), "a2-gp", (0, 10), False),
]
#The owner's accepted exceptions: a judged scene's frame that may miss the bar one way without failing the gate, with
#the reason, the date and his words, and a limit past which it fails all the same, so it can't get worse unseen. Any
#other miss of that frame (the other way) fails it.
#(None now: Super Monkey Ball's, accepted on 2026-10-18 at -29.6% and limited at -21% by part 113, went once the
#refit's hybrid form put its frame within the bar.)
EXCEPTED = {}
#Capture headers whose clocks aren't the scene's: read as the capture began, before the game set its own.
CLOCKS_NOTE = {
    "ULUS10186-boot2": "the capture's header has 333/166, read before the game set its clocks; the copyright screen "
                       "ran at 222/111 (docs/psp-core.md, part 104)",
}


def load(path):
    """geprof.py's report of a capture or profile (.bin, or .bin.xz), and the capture"""
    data = open(path, "rb").read()
    if path.endswith(".xz"):
        data = lzma.decompress(data)
    capture = geprof.parse_capture(data)
    return geprof.report_json(capture), capture


def frames(path, window):
    """the lists queued in the window, frame by frame (each frame's GE times, ms, in their order): only the frames
    holding the commonest count of lists; with the clocks"""
    if not os.path.exists(path):
        return None, None
    j, capture = load(path)
    calls = [(r.entry, r.a) for r in capture.records if r.kind == geprof.SET_FRAMEBUF and r.result >= 0]
    one = len(calls) > 1 and len({a for _, a in calls}) == 1
    flips, last = [], None
    for at, address in calls:
        if address != last or one:
            flips.append(at)
        last = address
    lo, hi = window[0] * 1e6, window[1] * 1e6
    by = {}
    for g in sorted((g for g in j["lists"] if g["queued"] is not None and lo <= g["queued"] < hi),
                    key=lambda g: g["queued"]):
        by.setdefault(bisect.bisect_right(flips, g["queued"]), []).append(g["ge_us"])
    whole = [f for f in by.values() if all(t for t in f)]
    if not whole:
        return [], None
    count = statistics.mode(len(f) for f in whole)
    clocks = "%d/%d" % (j["header"]["cpuMhz"], j["header"]["busMhz"])
    return [[t / 1000 for t in f] for f in whole if len(f) == count], clocks


def verdict(miss):
    return "passes" if abs(miss) <= BAR else ("too slow" if miss > 0 else "too fast")


def judged_frame(capture, miss):
    """a judged frame's mark and whether it fails the gate: within the bar, or outside it the way the owner accepted
    (EXCEPTED) and no further than its limit, it doesn't"""
    mark = verdict(miss)
    if mark == "passes":
        return mark, False
    if capture in EXCEPTED and EXCEPTED[capture][0] == mark:
        limit = EXCEPTED[capture][1]
        if abs(miss) <= abs(limit):
            return "%s (the owner's accepted exception)" % mark, False
        return "%s (past the accepted exception's limit, %+.0f%%)" % (mark, 100 * limit), True
    return mark, True


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    profiles = argv[0]
    captures = argv[1] if len(argv) > 1 else CAPTURES
    print("| Scene | Capture | List | PSP | Phobos, PSP timing | Against the PSP | 15% bar |")
    print("| --- | --- | --- | --- | --- | --- | --- |")
    lists, framed, failed, excepted, notes = [], [], False, [], []
    for what, capture, pwin, scene, owin, judge in ROWS:
        psp, pclock = frames(os.path.join(captures, capture + ".bin.xz"), pwin)
        ours, oclock = frames(os.path.join(profiles, scene + ".bin"), owin)
        if not psp:
            continue
        rows = []
        count = len(psp[0])
        same = bool(ours) and len(ours[0]) == count
        for place in range(count):
            a = statistics.median(f[place] for f in psp)
            b = statistics.median(f[place] for f in ours) if same else None
            rows.append(("%d of %d" % (place + 1, count), a, b))
        a = statistics.median(sum(f) for f in psp)
        b = statistics.median(sum(f) for f in ours) if ours else None
        rows.append(("frame", a, b))
        if capture in CLOCKS_NOTE:
            notes.append(CLOCKS_NOTE[capture])
            pclock += "*" * len(notes)
        for n, (label, a, b) in enumerate(rows):
            miss = None if b is None else b / a - 1
            mark = "-"
            if miss is not None:
                mark = verdict(miss)
                if not judge:
                    mark = "not judged (%s)" % mark
                elif label != "frame":
                    lists.append(miss)
                    mark = "reported (%s)" % mark
                else:
                    framed.append(miss)
                    mark, fails = judged_frame(capture, miss)
                    failed |= fails
                    if capture in EXCEPTED and not fails and abs(miss) > BAR:
                        excepted.append("%s (%s, %+.1f%%): %s" % (what, capture, 100 * miss, EXCEPTED[capture][2]))
            print("| %s | %s | %s | %.2f ms%s | %s | %s | %s |" % (
                what if n == 0 else "", capture if n == 0 else "", label, a, " (%s)" % pclock if n == 0 else "",
                "-" if b is None else "%.2f ms%s" % (b, " (%s)" % oclock if n == 0 else ""),
                "-" if miss is None else "%+.1f%%" % (100 * miss), mark))
    for n, note in enumerate(notes):
        print("%s %s" % ("*" * (n + 1), note))
    for name, misses in (("lists (reported)", lists), ("frames (judged)", framed)):
        if misses:
            print("%s: %d; within 15%%: %d; most too slow %+.0f%%; median miss %.1f%%" % (
                name, len(misses), sum(abs(m) <= BAR for m in misses), 100 * max(misses),
                100 * statistics.median(abs(m) for m in misses)))
    for line in excepted:
        print("excepted: " + line)
    print("gate: %s (each judged scene by its whole frame)" % ("fails" if failed else "passes"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
