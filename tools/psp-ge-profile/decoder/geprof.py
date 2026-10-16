#!/usr/bin/env python3
# Reads a capture from psp-ge-profile's plugin (<disc id>-<n>.bin: the layout is plugin/profile.h's, described in
# ../README.md) and prints how the game drove the GE and the display: per vertical blank, the display lists queued,
# each list's GE time, the GE's busy share, the CPU's time blocked in GE syncs and vblank waits, and the frame
# buffer flips; then a summary per second (renders per displayed frame, GE milliseconds per frame, flips a second).
#
# A list's GE time runs from when the GE could begin it (when it was queued, or when the previous list's FINISH
# callback returned, whichever is later: the GE driver starts the next list only then) to its FINISH callback. A
# list the game builds while the GE runs it (queued with a stall address, then given more with
# sceGeListUpdateStallAddr) spends part of that waiting for the CPU; its "tail" is the GE time after its last stall
# update, when it was all there, and the stall updates that found the GE waiting are counted.
#
# Phobos's runner writes the same captures from the emulator (tools/psp-runner --ge-profile), with one kind of record
# more: each piece of GE time its model charged (pixels, primitives, cycles). The tables are the same for both; a
# capture with charges also gets the model's own summary, and its lists the charges each was given.
#
# usage: geprof.py CAPTURE.bin [--lists] [--events] [--no-vblanks] [--counts] [--from S] [--to S] [--json FILE]
#
# Standard library only. write_capture() writes the same format, for the tests (test_geprof.py) and for any
# program that wants its own captures read the same way.

import argparse
import json
import struct
import sys
from bisect import bisect_right
from dataclasses import dataclass, field

MAGIC = b"PHGEPROF"
FORMAT = 1
HEADER_SIZE = 256
RECORD_SIZE = 32
THREAD_SIZE = 36

# The vblank period: the PSP's LCD shows 60000/1001 frames a second.
VBLANK_US = 1001 / 60000 * 1e6

KINDS = {
    1: "sceGeListEnQueue", 2: "sceGeListEnQueueHead", 3: "sceGeListDeQueue", 4: "sceGeListUpdateStallAddr",
    5: "sceGeListSync", 6: "sceGeDrawSync", 7: "sceGeBreak", 8: "sceGeContinue", 9: "sceGeSetCallback",
    10: "sceGeUnsetCallback", 11: "SIGNAL", 12: "FINISH", 13: "sceDisplaySetFrameBuf",
    14: "sceDisplayWaitVblankStart", 15: "sceDisplayWaitVblankStartCB", 16: "sceDisplayWaitVblank",
    17: "sceDisplayWaitVblankCB", 18: "sceDisplayWaitVblankStartMulti", 19: "sceDisplayWaitVblankStartMultiCB",
    20: "vblank", 64: "GE model's charge",
}
ENQUEUE, ENQUEUE_HEAD, DEQUEUE, UPDATE_STALL, LIST_SYNC, DRAW_SYNC, BREAK, CONTINUE = 1, 2, 3, 4, 5, 6, 7, 8
SET_CALLBACK, UNSET_CALLBACK, SIGNAL, FINISH, SET_FRAMEBUF = 9, 10, 11, 12, 13
WAIT_VBLANK_START, WAIT_VBLANK_START_CB, WAIT_VBLANK, WAIT_VBLANK_CB = 14, 15, 16, 17
WAIT_VBLANK_START_MULTI, WAIT_VBLANK_START_MULTI_CB = 18, 19
WAITS = {14, 15, 16, 17, 18, 19}
VBLANK = 20
INTERRUPT_KINDS = {SIGNAL, FINISH, VBLANK}
# Phobos's runner only: a piece of GE time its model charged (a: pixels, b: primitives, c: cycles at the CPU's
# 333 MHz; exit: when the GE was done with all it had; result: the list it ran, -1 none).
MODEL = 64
MODEL_CYCLES_PER_US = 333

GE_IDLE, GE_NOT_SAMPLED, GE_DRAWING, GE_STALLED = 0, 1, 2, 3
GE_STATES = {0: "idle", 1: "?", 2: "drawing", 3: "stalled"}

# Header flags.
FULL, MONITOR, TIMED, GE_SAMPLED = 1, 2, 4, 8

HEADER_FIELDS = [
    ("magic", "8s"), ("format", "I"), ("headerSize", "I"), ("recordSize", "I"), ("recordCount", "I"), ("flags", "I"),
    ("dropped", "I"), ("pluginVersion", "16s"), ("discId", "16s"), ("title", "64s"), ("startTime", "I"),
    ("endTime", "I"), ("startVcount", "I"), ("endVcount", "I"), ("cpuMhz", "I"), ("busMhz", "I"), ("cpuMhzEnd", "I"),
    ("busMhzEnd", "I"), ("firmware", "I"), ("arkVersion", "I"), ("arkMinor", "I"), ("model", "I"), ("hooks", "I"),
    ("captureNumber", "I"), ("pluginStartTime", "I"), ("capacity", "I"), ("threadTableOffset", "I"),
    ("threadCount", "I"), ("partition", "I"), ("bufferBytes", "I"), ("reserved", "48s"),
]
HEADER_STRUCT = struct.Struct("<" + "".join(f for _, f in HEADER_FIELDS))
RECORD_STRUCT = struct.Struct("<BBHIIIIIII")
assert HEADER_STRUCT.size == HEADER_SIZE and RECORD_STRUCT.size == RECORD_SIZE


class CaptureError(Exception):
    pass


def physical(address):
    return address & 0x1FFFFFFF


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


@dataclass
class Record:
    index: int
    kind: int
    thread: int
    vcount: int
    entry: int          # microseconds from the capture's start
    exit: int           # None: still in the call when the capture ended
    ge: int
    a: int
    b: int
    c: int
    result: int         # signed

    @property
    def ge_state(self):
        return self.ge & 3

    @property
    def ge_address(self):
        return self.ge & ~3 if self.ge_state in (GE_DRAWING, GE_STALLED) else None

    @property
    def name(self):
        return KINDS.get(self.kind, "kind %d" % self.kind)

    @property
    def waits_for_ge(self):
        """A sync that blocks until a list (sceGeListSync mode 0) or the queue (sceGeDrawSync mode 0) is done."""
        return (self.kind == LIST_SYNC and self.b == 0) or (self.kind == DRAW_SYNC and self.a == 0)


@dataclass
class Capture:
    header: dict
    records: list
    threads: list                    # (uid, name); a record's thread is an index here
    truncated: bool = False
    warnings: list = field(default_factory=list)

    @property
    def length_us(self):
        return (self.header["endTime"] - self.header["startTime"]) & 0xFFFFFFFF

    def thread_name(self, index):
        if index == 0:
            return "interrupt"
        if index < len(self.threads):
            return self.threads[index][1] or "0x%08x" % self.threads[index][0]
        return "other threads"


def _text(raw):
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def read_capture(path):
    with open(path, "rb") as file:
        data = file.read()
    return parse_capture(data)


def parse_capture(data):
    if len(data) < HEADER_SIZE:
        raise CaptureError("too short for a header (%d bytes)" % len(data))
    header = dict(zip((name for name, _ in HEADER_FIELDS), HEADER_STRUCT.unpack_from(data, 0)))
    if header["magic"] != MAGIC:
        raise CaptureError("not a psp-ge-profile capture (magic %r)" % header["magic"])
    if header["format"] != FORMAT:
        raise CaptureError("format %d; this decoder reads format %d" % (header["format"], FORMAT))
    if header["headerSize"] != HEADER_SIZE or header["recordSize"] != RECORD_SIZE:
        raise CaptureError("header size %d / record size %d" % (header["headerSize"], header["recordSize"]))
    for name in ("magic", "pluginVersion", "discId", "title"):
        header[name] = _text(header[name])
    del header["reserved"]
    capture = Capture(header, [], [])
    count = header["recordCount"]
    available = (len(data) - HEADER_SIZE) // RECORD_SIZE
    if available < count:
        capture.truncated = True
        capture.warnings.append("the file ends after %d of its %d records" % (available, count))
        count = available
    start = header["startTime"]

    def relative(t):
        return ((t - start + 0x80000000) & 0xFFFFFFFF) - 0x80000000

    for n in range(count):
        kind, thread, vcount, entry, exit, ge, a, b, c, result = RECORD_STRUCT.unpack_from(data, HEADER_SIZE + n * RECORD_SIZE)
        capture.records.append(Record(n, kind, thread, vcount, relative(entry), relative(exit) if exit else None, ge, a, b,
                                      c, signed(result)))
    offset = header["threadTableOffset"]
    for n in range(header["threadCount"]):
        at = offset + n * THREAD_SIZE
        if at + THREAD_SIZE > len(data):
            capture.warnings.append("the thread table is cut short")
            break
        uid, name = struct.unpack_from("<I32s", data, at)
        capture.threads.append((uid, _text(name)))
    capture.records.sort(key=lambda r: (r.entry, r.index))
    return capture


def write_capture(path, records, threads=(), **header):
    """Writes a capture: records are tuples (kind, thread, vcount, entry, exit, ge, a, b, c, result) with absolute
    32-bit times; header fields not given are 0 (strings empty), but for the magic, sizes and counts."""
    values = {name: 0 for name, _ in HEADER_FIELDS}
    values.update(magic=MAGIC, format=FORMAT, headerSize=HEADER_SIZE, recordSize=RECORD_SIZE, pluginVersion="",
                  discId="", title="", reserved=b"")
    values.update(header)
    values["recordCount"] = len(records)
    values["threadCount"] = len(threads)
    values["threadTableOffset"] = HEADER_SIZE + len(records) * RECORD_SIZE
    for name in ("pluginVersion", "discId", "title"):
        if isinstance(values[name], str):
            values[name] = values[name].encode()
    out = bytearray(HEADER_STRUCT.pack(*(values[name] for name, _ in HEADER_FIELDS)))
    for record in records:
        kind, thread, vcount, entry, exit, ge, a, b, c, result = record
        out += RECORD_STRUCT.pack(kind, thread, vcount & 0xFFFF, entry & 0xFFFFFFFF, exit & 0xFFFFFFFF,
                                  ge & 0xFFFFFFFF, a & 0xFFFFFFFF, b & 0xFFFFFFFF, c & 0xFFFFFFFF, result & 0xFFFFFFFF)
    for uid, name in threads:
        out += struct.pack("<I32s", uid, name.encode()[:31])
    with open(path, "wb") as file:
        file.write(out)


def vblank_times(capture):
    """The vblanks' starts (microseconds from the capture's start) and where they came from: the monitor's vblank
    records, with any it missed filled in from the vblank counts; else the returns from vblank waits, fitted to the
    LCD's period; else the period alone from the capture's start."""
    end = capture.length_us
    seen = [r for r in capture.records if r.kind == VBLANK]
    if len(seen) >= 2:
        times = [seen[0].entry]
        for previous, record in zip(seen, seen[1:]):
            step = (record.a - previous.a) & 0xFFFFFFFF
            if 1 < step < 1000:
                gap = (record.entry - previous.entry) / step
                times += [round(previous.entry + gap * n) for n in range(1, step)]
            times.append(record.entry)
        return _extend(times, end), "the monitor's vblank records (%d)" % len(seen)
    returns = sorted(r.exit for r in capture.records if r.kind in WAITS and r.exit is not None and r.result >= 0)
    if returns:
        # A wait returns a little after its vblank, never before: the phase is the earliest of the returns' phases.
        phases = sorted(t % VBLANK_US for t in returns)
        spread = phases[-1] - phases[0]
        phase = phases[0]
        if spread > VBLANK_US / 2:  # the phases wrap around the period's end
            shifted = sorted((p + VBLANK_US / 2) % VBLANK_US for p in phases)
            phase = (shifted[0] - VBLANK_US / 2) % VBLANK_US
        first = phase - VBLANK_US * int(phase // VBLANK_US + 1)
        times = []
        t = first
        while t <= end:
            if t >= -VBLANK_US:
                times.append(round(t))
            t += VBLANK_US
        return times, "fitted to %d vblank waits' returns" % len(returns)
    times = [round(n * VBLANK_US) for n in range(int(end // VBLANK_US) + 2)]
    return times, "assumed (no vblank records and no vblank waits): every %.1f us from the start" % VBLANK_US


def _extend(times, end):
    while times[-1] + VBLANK_US <= end + VBLANK_US:
        times.append(round(times[-1] + VBLANK_US))
        if times[-1] > end:
            break
    while times[0] > 0:
        times.insert(0, round(times[0] - VBLANK_US))
    return times


@dataclass
class GeList:
    id: int
    head: bool
    address: int
    stall: int                      # the first stall address (0: none)
    cbid: int
    enqueued: int                   # when the enqueue call returned
    thread: int
    ge_at_enqueue: int
    updates: list = field(default_factory=list)   # (time, stall, GE state when called)
    start: int = None
    end: int = None
    freed: int = None               # when the GE could start the next one (the FINISH callback's return)
    ended_by: str = None            # "FINISH", "sync", "dequeue", None (not seen)
    called: int = None              # when the enqueue call was made
    model_cycles: int = None        # Phobos's model: the GE time charged for it (cycles at 333 MHz), its pixels and
    model_pixels: int = None        # primitives (model_charges())
    model_primitives: int = None

    @property
    def last_stall(self):
        stalls = [self.stall] + [s for _, s, _ in self.updates]
        return max(physical(s) for s in stalls)

    @property
    def complete(self):
        """When the whole list was there for the GE: when queued without a stall, else the last stall update."""
        if self.stall == 0:
            return self.enqueued
        return self.updates[-1][0] if self.updates else self.enqueued

    @property
    def ge_us(self):
        return self.end - self.start if self.start is not None and self.end is not None else None

    @property
    def tail_us(self):
        if self.ge_us is None:
            return None
        return max(0, self.end - max(self.start, self.complete))

    @property
    def starved(self):
        return sum(1 for _, _, state in self.updates if state == GE_STALLED)

    def contains(self, address):
        lo = physical(self.address)
        hi = self.last_stall if self.stall else lo + 0x400000
        return lo < physical(address) <= max(hi, lo + 8)


def display_lists(capture):
    """Each list's life: queued, started, ended, from the records in time order. FINISH callbacks are matched to the
    queued list whose commands they end (the FINISH's list address), else to the head of the queue; a list without
    a callback ends at the sync that waited for it."""
    points = []
    for r in capture.records:
        if r.kind in (ENQUEUE, ENQUEUE_HEAD, DEQUEUE, UPDATE_STALL, BREAK, CONTINUE) and r.exit is not None:
            points.append((r.exit, 1, r))
        elif r.kind == FINISH:
            points.append((r.entry, 0, r))
        elif r.waits_for_ge and r.exit is not None and r.result >= 0:
            points.append((r.exit, 2, r))
    points.sort(key=lambda p: (p[0], p[1], p[2].index))
    lists, queue, by_id = [], [], {}
    ge_free = 0
    # Lists queued before the capture may still be running: until a FINISH matches a list seen here (or the GE is
    # seen idle), one that matches none is taken to be theirs.
    first_ge = next((r for r in capture.records if r.kind <= CONTINUE or r.kind in (SIGNAL, FINISH)), None)
    unknown_ahead = first_ge is not None and first_ge.ge_state in (GE_DRAWING, GE_STALLED)
    idle_seen = next((r.entry for r in capture.records if r.kind not in INTERRUPT_KINDS and r.ge_state == GE_IDLE
                      and r.kind <= SET_FRAMEBUF), None)
    orphans = 0

    def finish(gelist, end, freed, how):
        nonlocal ge_free
        if gelist.start is None:
            gelist.start = max(gelist.enqueued, ge_free) if not unknown_ahead else gelist.enqueued
        gelist.end, gelist.freed, gelist.ended_by = max(end, gelist.start), max(freed, gelist.start), how
        ge_free = gelist.freed
        queue.remove(gelist)
        if queue and queue[0].start is None and not queue[0].head:
            queue[0].start = max(queue[0].enqueued, ge_free)

    for time, _, r in points:
        if unknown_ahead and idle_seen is not None and idle_seen <= time:
            unknown_ahead = False
        if r.kind in (ENQUEUE, ENQUEUE_HEAD):
            if r.result < 0:
                continue
            gelist = GeList(r.result, r.kind == ENQUEUE_HEAD, r.a, r.b, signed(r.c), r.exit, r.thread, r.ge_state,
                            called=r.entry)
            lists.append(gelist)
            by_id[gelist.id] = gelist
            if gelist.head:
                queue.insert(0, gelist)
            else:
                if not queue and not unknown_ahead:
                    gelist.start = max(gelist.enqueued, ge_free) if r.ge_state != GE_IDLE else gelist.enqueued
                queue.append(gelist)
        elif r.kind == UPDATE_STALL:
            gelist = by_id.get(r.a)
            if gelist is not None and r.result >= 0:
                gelist.updates.append((r.exit, r.b, r.ge_state))
        elif r.kind == DEQUEUE:
            gelist = by_id.get(r.a)
            if gelist is not None and gelist in queue and r.result >= 0:
                finish(gelist, r.exit, r.exit, "dequeue")
        elif r.kind == CONTINUE:
            if queue and queue[0].head and queue[0].start is None and r.result >= 0:
                queue[0].start = r.exit
        elif r.kind == FINISH:
            freed = r.exit if r.exit is not None else r.entry
            match = next((g for g in queue if g.contains(r.c)), None)
            if match is None and (unknown_ahead or not queue):
                orphans += 1
                ge_free = freed
                if queue and queue[0].start is None and not queue[0].head:
                    queue[0].start = max(queue[0].enqueued, ge_free)
                continue
            if match is None:
                match = queue[0]
            unknown_ahead = False
            for earlier in queue[:queue.index(match)]:
                finish(earlier, r.entry, r.entry, "sync")
            finish(match, r.entry, freed, "FINISH")
        elif r.kind == LIST_SYNC:
            gelist = by_id.get(r.a)
            if gelist is not None and gelist in queue:
                for earlier in queue[:queue.index(gelist) + 1]:
                    finish(earlier, r.exit, r.exit, "sync")
        elif r.kind == DRAW_SYNC:
            for gelist in [g for g in queue if g.enqueued <= r.entry]:
                finish(gelist, r.exit, r.exit, "sync")
            unknown_ahead = False
    if orphans:
        capture.warnings.append("%d FINISH callbacks ended lists queued before the capture" % orphans)
    for gelist in queue:
        if gelist.start is None and not gelist.head:
            gelist.start = max(gelist.enqueued, ge_free)
    if any(r.kind in (BREAK, ENQUEUE_HEAD) for r in capture.records):
        capture.warnings.append("the game used sceGeBreak or sceGeListEnQueueHead: those lists' times are approximate")
    return lists


@dataclass
class ModelSummary:
    charges: int = 0
    pixels: int = 0
    primitives: int = 0
    cycles: int = 0
    pixel_bound: int = 0            # charges whose pixels set their time (four a bus clock), not their primitives

    @property
    def ms(self):
        return self.cycles / MODEL_CYCLES_PER_US / 1000


def model_charges(capture, lists, start_us=None, end_us=None):
    """Phobos's runner only: the GE time its model charged (the MODEL records), given to the lists they were for (by
    the list's ID, from its enqueue call, in which the GE runs it as far as it can, to its end), and summed over the
    window. Its time is max(pixels / 4, primitives x 166 / 35) bus clocks (ares/psp/kernel/ge.cpp's geTime()). None
    if the capture has no charges."""
    charges = [r for r in capture.records if r.kind == MODEL]
    if not charges:
        return None
    by_id = {}
    for g in lists:
        by_id.setdefault(g.id, []).append(g)
    end = capture.length_us
    summary = ModelSummary()
    for r in charges:
        if (start_us is None or r.entry >= start_us) and (end_us is None or r.entry < end_us):
            summary.charges += 1
            summary.pixels += r.a
            summary.primitives += r.b
            summary.cycles += r.c
            summary.pixel_bound += r.a * 35 >= r.b * 664
        for g in by_id.get(r.result, ()):
            if g.called <= r.entry <= (g.end if g.end is not None else end):
                g.model_cycles = (g.model_cycles or 0) + r.c
                g.model_pixels = (g.model_pixels or 0) + r.a
                g.model_primitives = (g.model_primitives or 0) + r.b
                break
    return summary


def counts(capture, start_us=None, end_us=None):
    """The calls and interrupts of each kind in the window, as the plugin's dry run counts them."""
    found = {}
    for r in capture.records:
        if (start_us is None or r.entry >= start_us) and (end_us is None or r.entry < end_us):
            found[r.kind] = found.get(r.kind, 0) + 1
    return found


def _union(intervals):
    merged = []
    for a, b in sorted(intervals):
        if b <= a:
            continue
        if merged and a <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    return merged


def _spread(rows, starts, intervals, attribute):
    """Adds each interval's microseconds to the rows (vblanks) it overlaps."""
    for a, b in intervals:
        n = max(0, bisect_right(starts, a) - 1)
        while n < len(rows) and rows[n].start < b:
            row = rows[n]
            covered = min(b, row.start + row.length) - max(a, row.start)
            if covered > 0:
                setattr(row, attribute, getattr(row, attribute) + covered)
            n += 1


@dataclass
class VblankRow:
    index: int
    start: int
    length: int
    lists: int = 0
    finishes: int = 0
    signals: int = 0
    updates: int = 0
    starved: int = 0
    ge_us: int = 0
    tail_us: int = 0
    sync_us: int = 0
    wait_us: int = 0
    setframebufs: int = 0
    flips: int = 0
    shown: int = None               # the frame buffer shown from this vblank
    new_frame: bool = False

    @property
    def ge_share(self):
        return self.ge_us / self.length if self.length else 0


def per_vblank(capture, lists=None, vblanks=None):
    """A row per vblank the capture covers: what was queued, called and flipped in it, and the GE's and the CPU's
    time in it. The frame buffer a vblank shows is the last one set before it starts. A flip is a
    sceDisplaySetFrameBuf to another buffer than the call before set (setting the same one again isn't a new
    frame), and a vblank shows a new frame if the one before it had a flip: a game that renders twice a vblank into
    two buffers ends each vblank on the same buffer, but every vblank shows a new image."""
    if lists is None:
        lists = display_lists(capture)
    if vblanks is None:
        vblanks, _ = vblank_times(capture)
    end = capture.length_us
    rows = [VblankRow(n, lo, hi - lo) for n, (lo, hi) in enumerate(zip(vblanks, vblanks[1:]))]
    starts = [row.start for row in rows]
    last_address = None
    for r in capture.records:
        n = bisect_right(starts, r.entry) - 1
        if n < 0:
            continue
        row = rows[n]
        if r.kind in (ENQUEUE, ENQUEUE_HEAD) and r.result >= 0:
            row.lists += 1
        elif r.kind == FINISH:
            row.finishes += 1
        elif r.kind == SIGNAL:
            row.signals += 1
        elif r.kind == UPDATE_STALL:
            row.updates += 1
            row.starved += r.ge_state == GE_STALLED
        elif r.kind == SET_FRAMEBUF and r.result >= 0:
            row.setframebufs += 1
            row.flips += r.a != last_address
            last_address = r.a
    _spread(rows, starts, _union((g.start, g.end if g.end is not None else end) for g in lists if g.start is not None),
            "ge_us")
    _spread(rows, starts, _union((max(g.start, g.complete), g.end) for g in lists if g.ge_us is not None), "tail_us")
    _spread(rows, starts, [(r.entry, r.exit if r.exit is not None else end) for r in capture.records if r.waits_for_ge],
            "sync_us")
    _spread(rows, starts, [(r.entry, r.exit if r.exit is not None else end) for r in capture.records if r.kind in WAITS],
            "wait_us")
    flips = [(r.entry, r.a) for r in capture.records if r.kind == SET_FRAMEBUF and r.result >= 0]
    if len(flips) > 1 and len({address for _, address in flips}) == 1:
        note = ("every sceDisplaySetFrameBuf sets the same buffer (%08x, %d times): renders and displayed frames, counted "
                "by flips, are none; its frames are counted by those calls" % (flips[0][1], len(flips)))
        if note not in capture.warnings:
            capture.warnings.append(note)
    shown, f = None, 0
    for n, row in enumerate(rows):
        while f < len(flips) and flips[f][0] < row.start:
            shown = flips[f][1]
            f += 1
        row.shown = shown
        row.new_frame = n > 0 and rows[n - 1].flips > 0
    return [row for row in rows if row.start + row.length > 0 and row.start < end]


@dataclass
class SecondRow:
    second: int
    vblanks: int = 0
    displayed: int = 0
    flips: int = 0
    setframebufs: int = 0
    lists: int = 0
    finishes: int = 0
    ge_us: int = 0
    tail_us: int = 0
    sync_us: int = 0
    wait_us: int = 0
    length_us: int = 0

    @property
    def renders_per_frame(self):
        return self.flips / self.displayed if self.displayed else None

    @property
    def ge_ms_per_frame(self):
        return self.ge_us / 1000 / self.displayed if self.displayed else None


def per_second(rows):
    seconds = {}
    for row in rows:
        s = seconds.setdefault(max(0, row.start) // 1000000, SecondRow(max(0, row.start) // 1000000))
        s.vblanks += 1
        s.displayed += row.new_frame
        s.flips += row.flips
        s.setframebufs += row.setframebufs
        s.lists += row.lists
        s.finishes += row.finishes
        s.ge_us += row.ge_us
        s.tail_us += row.tail_us
        s.sync_us += row.sync_us
        s.wait_us += row.wait_us
        s.length_us += row.length
    return [seconds[k] for k in sorted(seconds)]


def _ms(us):
    return "%.2f" % (us / 1000) if us is not None else "-"


def describe(capture, out):
    h = capture.header
    out.write("%s  %s\n" % (h["discId"] or "?", h["title"]))
    model = "?" if h["model"] == 0xFFFFFFFF else h["model"]
    out.write("plugin %s, format %d; firmware 0x%08x, model value %s, ARK 0x%x.%x\n"
              % (h["pluginVersion"], h["format"], h["firmware"], model, h["arkVersion"], h["arkMinor"]))
    out.write("clocks: CPU %d MHz, bus %d MHz at the start; %d / %d at the end\n"
              % (h["cpuMhz"], h["busMhz"], h["cpuMhzEnd"], h["busMhzEnd"]))
    after = ((h["startTime"] - h["pluginStartTime"]) & 0xFFFFFFFF) / 1e6
    out.write("capture %d: %.2f s from %.1f s after the game started (%s); %d records of %d room, %d dropped%s\n"
              % (h["captureNumber"], capture.length_us / 1e6, after, "timed" if h["flags"] & TIMED else "buttons",
                 len(capture.records), h["capacity"], h["dropped"], "; THE BUFFER FILLED" if h["flags"] & FULL else ""))
    missing = [KINDS[k] for k in range(1, 20) if not h["hooks"] & (1 << k) and k not in INTERRUPT_KINDS]
    out.write("hooks: %s; monitor %s\n" % ("all in place" if not missing else "missing " + ", ".join(missing),
                                           "on" if h["flags"] & MONITOR else "off (no SIGNAL, FINISH or vblank records)"))
    names = ", ".join("%d %s" % (n, capture.thread_name(n)) for n in range(1, len(capture.threads)))
    out.write("threads: %s\n" % (names or "none"))


def print_report(capture, out=None, show_lists=False, show_events=False, show_vblanks=True, start_s=None, end_s=None,
                 show_counts=False):
    out = out or sys.stdout
    vblanks, source = vblank_times(capture)
    lists = display_lists(capture)
    rows = per_vblank(capture, lists, vblanks)
    if start_s is not None:
        rows = [r for r in rows if r.start >= start_s * 1e6]
    if end_s is not None:
        rows = [r for r in rows if r.start < end_s * 1e6]
    window = (start_s * 1e6 if start_s is not None else None, end_s * 1e6 if end_s is not None else None)
    model = model_charges(capture, lists, *window)
    describe(capture, out)
    out.write("vblanks: %s\n" % source)
    by = {}
    for g in lists:
        by[g.ended_by] = by.get(g.ended_by, 0) + 1
    out.write("lists: %d (ended by FINISH %d, by a sync %d, dequeued %d, not seen ending %d)\n"
              % (len(lists), by.get("FINISH", 0), by.get("sync", 0), by.get("dequeue", 0), by.get(None, 0)))
    for warning in capture.warnings:
        out.write("note: %s\n" % warning)
    if model is not None:
        displayed = sum(r.new_frame for r in rows)
        out.write("Phobos's GE model: %d charges, %.2f M pixels, %d primitives, %.2f ms of the GE's time (%s ms a "
                  "displayed frame); %.0f%% of the charges set by their pixels (four a bus clock), the rest by their "
                  "primitives\n"
                  % (model.charges, model.pixels / 1e6, model.primitives, model.ms,
                     "%.2f" % (model.ms / displayed) if displayed else "-",
                     100 * model.pixel_bound / model.charges if model.charges else 0))
    if show_counts:
        found = counts(capture, *window)
        length = (min(end_s * 1e6, capture.length_us) if end_s is not None else capture.length_us) \
            - (start_s * 1e6 if start_s is not None else 0)
        out.write("\ncalls and interrupts in %.2f s (as the dry run counts them)\n" % (length / 1e6))
        for kind in sorted(found):
            out.write("%-34s %7d %9.1f a second\n" % (KINDS.get(kind, "kind %d" % kind), found[kind],
                                                    found[kind] / (length / 1e6) if length > 0 else 0))

    out.write("\nper second (renders: flips, frame buffer changes; displayed: vblanks after a flip, showing a new frame)\n")
    out.write("%6s %7s %9s %7s %8s %6s %8s %8s %5s %8s %9s\n" % ("second", "vblanks", "displayed", "renders", "per frame",
                                                                "lists", "GE ms/f", "tail/f", "GE%", "sync ms", "vwait ms"))
    for s in per_second(rows):
        per = s.renders_per_frame
        out.write("%6d %7d %9d %7d %8s %6d %8s %8s %5.1f %8s %9s\n"
                  % (s.second, s.vblanks, s.displayed, s.flips, "%.2f" % per if per is not None else "-", s.lists,
                     "%.2f" % s.ge_ms_per_frame if s.ge_ms_per_frame is not None else "-",
                     _ms(s.tail_us / s.displayed) if s.displayed else "-", 100 * s.ge_us / s.length_us if s.length_us else 0,
                     _ms(s.sync_us), _ms(s.wait_us)))

    if show_vblanks:
        out.write("\nper vblank (GE: in a list; tail: after a list was all there; sync, vwait: CPU blocked)\n")
        out.write("%6s %9s %5s %4s %4s %9s %7s %5s %7s %7s %7s %5s %10s\n"
                  % ("vblank", "ms", "lists", "FIN", "SIG", "stall(GE)", "GE ms", "GE%", "tail ms", "sync ms", "vwait", "flips",
                     "shown"))
        for row in rows:
            out.write("%6d %9.2f %5d %4d %4d %9s %7s %5.1f %7s %7s %7s %5d %10s%s\n"
                      % (row.index, row.start / 1000, row.lists, row.finishes, row.signals,
                         "%d(%d)" % (row.updates, row.starved), _ms(row.ge_us), 100 * row.ge_share, _ms(row.tail_us),
                         _ms(row.sync_us), _ms(row.wait_us), row.flips,
                         "%08x" % row.shown if row.shown is not None else "-", " new" if row.new_frame else ""))

    if show_lists:
        out.write("\ndisplay lists (ms from the capture's start%s)\n"
                  % ("; model: the GE time Phobos's model charged, its pixels and primitives" if model else ""))
        out.write("%10s %8s %9s %9s %9s %7s %7s %7s %5s %8s %-8s%s\n"
                  % ("id", "address", "queued", "start", "end", "GE ms", "tail", "stalls", "cb", "thread", "ended",
                     " %8s %9s %6s" % ("model ms", "pixels", "prims") if model else ""))
        for g in lists:
            charged = ""
            if model:
                charged = " %8s %9s %6s" % (_ms(g.model_cycles / MODEL_CYCLES_PER_US) if g.model_cycles is not None else "-",
                                            g.model_pixels if g.model_pixels is not None else "-",
                                            g.model_primitives if g.model_primitives is not None else "-")
            out.write("%10x %08x %9s %9s %9s %7s %7s %7s %5d %8s %-8s%s%s\n"
                      % (g.id, g.address, _ms(g.enqueued), _ms(g.start), _ms(g.end), _ms(g.ge_us), _ms(g.tail_us),
                         "%d(%d)" % (len(g.updates), g.starved), g.cbid, capture.thread_name(g.thread)[:8],
                         g.ended_by or "-", charged, " head" if g.head else ""))

    if show_events:
        out.write("\nrecords (us from the capture's start)\n")
        for r in capture.records:
            out.write("%10d %10s %-34s %-12s ge %-7s %08x a %08x b %08x c %08x -> %d\n"
                      % (r.entry, r.exit - r.entry if r.exit is not None else "open", r.name, capture.thread_name(r.thread)[:12],
                         GE_STATES[r.ge_state], r.ge & ~3, r.a, r.b, r.c, r.result))
    return rows, lists


def report_json(capture):
    vblanks, source = vblank_times(capture)
    lists = display_lists(capture)
    rows = per_vblank(capture, lists, vblanks)
    model = model_charges(capture, lists)
    return {
        "header": capture.header,
        "vblank_source": source,
        "warnings": capture.warnings,
        "counts": {KINDS.get(kind, "kind %d" % kind): n for kind, n in sorted(counts(capture).items())},
        "model": None if model is None else dict(charges=model.charges, pixels=model.pixels,
                                                 primitives=model.primitives, cycles=model.cycles, ms=model.ms,
                                                 pixel_bound=model.pixel_bound),
        "lists": [dict(id=g.id, address=g.address, cbid=g.cbid, queued=g.enqueued, start=g.start, end=g.end,
                       ge_us=g.ge_us, tail_us=g.tail_us, stall_updates=len(g.updates), starved=g.starved,
                       ended_by=g.ended_by, thread=capture.thread_name(g.thread), model_cycles=g.model_cycles,
                       model_pixels=g.model_pixels, model_primitives=g.model_primitives) for g in lists],
        "vblanks": [dict(index=r.index, start=r.start, length=r.length, lists=r.lists, finishes=r.finishes,
                         signals=r.signals, stall_updates=r.updates, starved=r.starved, ge_us=r.ge_us, tail_us=r.tail_us,
                         sync_us=r.sync_us, wait_us=r.wait_us, setframebufs=r.setframebufs, flips=r.flips,
                         shown=r.shown, new_frame=r.new_frame) for r in rows],
        "seconds": [dict(second=s.second, vblanks=s.vblanks, displayed=s.displayed, renders=s.flips,
                         renders_per_frame=s.renders_per_frame, ge_ms_per_frame=s.ge_ms_per_frame, lists=s.lists,
                         ge_us=s.ge_us, tail_us=s.tail_us, sync_us=s.sync_us, wait_us=s.wait_us) for s in per_second(rows)],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description="Decodes a psp-ge-profile capture.")
    parser.add_argument("capture")
    parser.add_argument("--lists", action="store_true", help="each display list's times")
    parser.add_argument("--events", action="store_true", help="every record")
    parser.add_argument("--no-vblanks", action="store_true", help="leave out the per-vblank table")
    parser.add_argument("--counts", action="store_true", help="the calls and interrupts of each kind, as a dry run counts")
    parser.add_argument("--from", dest="start", type=float, help="seconds from the capture's start")
    parser.add_argument("--to", dest="end", type=float, help="seconds from the capture's start")
    parser.add_argument("--json", help="write the tables as JSON to this file")
    args = parser.parse_args(argv)
    try:
        capture = read_capture(args.capture)
    except (OSError, CaptureError) as error:
        sys.stderr.write("geprof: %s: %s\n" % (args.capture, error))
        return 1
    print_report(capture, show_lists=args.lists, show_events=args.events, show_vblanks=not args.no_vblanks,
                 start_s=args.start, end_s=args.end, show_counts=args.counts)
    if args.json:
        with open(args.json, "w") as file:
            json.dump(report_json(capture), file, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
