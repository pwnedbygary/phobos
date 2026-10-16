#!/usr/bin/env python3
# Tests for geprof.py on synthetic captures, written with geprof.write_capture as the plugin would write them: games
# that render once a vblank, three times a vblank, at 30 frames a second, with stall-driven lists, back-to-back lists,
# lists without callbacks, captures without the monitor's vblank records, across the 32-bit timer's wrap, cut short,
# and with lists still running from before the capture.
#
# usage: python3 -m unittest test_geprof   (from this folder), or python3 test_geprof.py

import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geprof as g

P = g.VBLANK_US
LIST = 0x08900000
FRAMEBUFFERS = (0x04000000, 0x04088000, 0x04110000)
# Written by the plugin itself, run on the Mac with its firmware faked (make hosttest in ../plugin): three seconds of
# a game whose every frame is a list built while the GE runs it, a SIGNAL, a sync, a flip and a vblank wait.
PLUGIN_CAPTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plugin", "hosttest.bin")


class Game:
    """Records as the plugin makes them, at times in microseconds from the capture's start (start is the timer's
    value then)."""

    def __init__(self, start=50_000_000, monitor=True):
        self.start = start
        self.monitor = monitor
        self.records = []
        self.next_id = 0x08A00000

    def at(self, t):
        return (self.start + round(t)) & 0xFFFFFFFF

    def call(self, kind, entry, exit, a=0, b=0, c=0, result=0, ge=g.GE_IDLE, thread=1):
        self.records.append((kind, thread, 0, self.at(entry), self.at(exit) or 1, ge, a, b, c, result))

    def enqueue(self, t, list_address, stall=0, cbid=0, ge=g.GE_IDLE):
        self.next_id += 0x40
        self.call(g.ENQUEUE, t, t + 10, list_address, stall, cbid, self.next_id, ge)
        return self.next_id

    def finish(self, t, cbid, after, handler=20):
        self.records.append((g.FINISH, 0, 0, self.at(t), self.at(t + handler), g.GE_DRAWING, cbid, 0, after, 0))

    def vblank(self, t, count):
        if self.monitor:
            self.records.append((g.VBLANK, 0, count, self.at(t), self.at(t), g.GE_IDLE, count, 0x13, 0, 0))

    def flip(self, t, address, sync=1):
        self.call(g.SET_FRAMEBUF, t, t + 5, address, 512, 3 | sync << 8)

    def write(self, folder, length, name="capture.bin", **header):
        path = os.path.join(folder, name)
        fields = dict(discId="UCUS98653", title="God of War: Chains of Olympus", pluginVersion="0.1.0",
                      startTime=self.at(0), endTime=self.at(length), pluginStartTime=self.at(-20_000_000), cpuMhz=222,
                      busMhz=111, cpuMhzEnd=222, busMhzEnd=111, capacity=65536, hooks=0xFFFFFFFF,
                      flags=g.MONITOR | g.GE_SAMPLED if self.monitor else g.GE_SAMPLED, captureNumber=1)
        fields.update(header)
        g.write_capture(path, self.records, [(0, "interrupt"), (0x04A1B2C3, "render")], **fields)
        return path


def once_a_vblank(game, frames, ge_us=5000, offset=0.0):
    """A game that queues one list a vblank, waits for it, flips and waits for the next vblank."""
    for k in range(frames):
        v = offset + k * P
        game.vblank(v, 1000 + k)
        t = v + 1000
        game.enqueue(t, LIST + (k % 2) * 0x10000)
        done = t + 10 + ge_us
        game.call(g.DRAW_SYNC, t + 2000, done + 30, 0, ge=g.GE_DRAWING)
        game.finish(done, 0, LIST + (k % 2) * 0x10000 + 0x100)
        game.flip(done + 100, FRAMEBUFFERS[k % 2])
        game.call(g.WAIT_VBLANK_START, done + 200, v + P + 15)


class Decoder(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)

    def decode(self, game, length, **header):
        capture = g.read_capture(game.write(self.folder.name, length, **header))
        lists = g.display_lists(capture)
        rows = g.per_vblank(capture, lists)
        return capture, lists, rows

    def test_round_trip(self):
        game = Game()
        game.enqueue(1000, LIST, 0, 0)
        game.finish(4000, 0, LIST + 0x100)
        capture = g.read_capture(game.write(self.folder.name, 10_000, dropped=3, flags=g.FULL | g.MONITOR))
        self.assertEqual(capture.header["discId"], "UCUS98653")
        self.assertEqual(capture.header["title"], "God of War: Chains of Olympus")
        self.assertEqual(capture.header["dropped"], 3)
        self.assertEqual(capture.length_us, 10_000)
        self.assertEqual([r.kind for r in capture.records], [g.ENQUEUE, g.FINISH])
        self.assertEqual(capture.records[0].entry, 1000)
        self.assertEqual(capture.records[0].exit, 1010)
        self.assertEqual(capture.records[1].c, LIST + 0x100)
        self.assertEqual(capture.thread_name(1), "render")
        self.assertEqual(capture.thread_name(0), "interrupt")
        self.assertEqual(capture.thread_name(9), "other threads")

    def test_once_a_vblank(self):
        game = Game()
        once_a_vblank(game, 120)
        capture, lists, rows = self.decode(game, 120 * P)
        self.assertEqual(len(lists), 120)
        self.assertTrue(all(gl.ended_by == "FINISH" for gl in lists))
        self.assertAlmostEqual(lists[5].ge_us, 5000, delta=1)
        self.assertAlmostEqual(lists[5].tail_us, 5000, delta=1)
        full = rows[2:-2]
        self.assertTrue(all(row.lists == 1 and row.finishes == 1 and row.flips == 1 for row in full))
        self.assertTrue(all(abs(row.ge_us - 5000) <= 1 for row in full))
        self.assertTrue(all(abs(row.ge_share - 5000 / P) < 0.001 for row in full))
        self.assertTrue(all(abs(row.sync_us - 3040) <= 1 for row in full))
        self.assertTrue(all(row.new_frame for row in full))
        seconds = g.per_second(rows)
        self.assertEqual(seconds[0].displayed, 59)  # the first vblank has nothing before it
        self.assertEqual(seconds[1].displayed, 60)
        self.assertAlmostEqual(seconds[1].renders_per_frame, 1.0)
        self.assertAlmostEqual(seconds[1].ge_ms_per_frame, 5.0, delta=0.01)
        self.assertIn("monitor", g.vblank_times(capture)[1])

    def test_three_renders_a_vblank(self):
        # What Phobos does in Chains of Olympus' title scene: the scene drawn and flipped three times a vblank.
        game = Game()
        flips = 0
        for k in range(60):
            v = k * P
            game.vblank(v, 2000 + k)
            for n in range(3):
                t = v + 500 + n * 5000
                address = LIST + n * 0x20000
                game.enqueue(t, address)
                game.finish(t + 1510, 0, address + 0x80)
                game.call(g.DRAW_SYNC, t + 20, t + 1540, 0, ge=g.GE_DRAWING)
                game.flip(t + 1600, FRAMEBUFFERS[flips % 2])
                flips += 1
        capture, lists, rows = self.decode(game, 60 * P)
        seconds = g.per_second(rows)
        self.assertEqual(seconds[0].flips, 180)
        self.assertEqual(seconds[0].displayed, 59)
        self.assertAlmostEqual(seconds[0].renders_per_frame, 180 / 59)
        self.assertTrue(all(row.lists == 3 and row.flips == 3 for row in rows[:-1]))
        self.assertTrue(all(abs(row.ge_us - 3 * 1500) <= 3 for row in rows[:-1]))

    def test_thirty_frames_a_second(self):
        game = Game()
        for k in range(0, 120, 2):
            v = k * P
            game.vblank(v, 3000 + k)
            game.vblank(v + P, 3001 + k)
            t = v + 800
            game.enqueue(t, LIST + (k // 2 % 2) * 0x10000)
            game.finish(t + 20010, 0, LIST + (k // 2 % 2) * 0x10000 + 0x40)
            game.flip(t + 20100, FRAMEBUFFERS[k // 2 % 2])
            game.call(g.WAIT_VBLANK_START_MULTI, t + 20200, v + 2 * P + 10, 2)
        capture, lists, rows = self.decode(game, 120 * P)
        seconds = g.per_second(rows)
        self.assertEqual(seconds[1].displayed, 30)
        self.assertEqual(seconds[1].flips, 30)
        self.assertAlmostEqual(seconds[1].renders_per_frame, 1.0)
        self.assertAlmostEqual(seconds[1].ge_ms_per_frame, 20.0, delta=0.05)
        self.assertEqual(sum(row.new_frame for row in rows[10:20]), 5)

    def test_stall_driven_list(self):
        game = Game()
        game.vblank(0, 10)
        game.vblank(P, 11)
        list_id = game.enqueue(1000, LIST, stall=LIST)
        game.call(g.UPDATE_STALL, 2000, 2005, list_id, LIST + 0x400, ge=g.GE_STALLED)
        game.call(g.UPDATE_STALL, 3000, 3005, list_id, LIST + 0x800, ge=g.GE_DRAWING)
        game.call(g.UPDATE_STALL, 4000, 4005, list_id, LIST + 0xC00, ge=g.GE_STALLED)
        game.finish(7005, 0, LIST + 0xC00)
        capture, lists, rows = self.decode(game, 2 * P)
        (gl,) = lists
        self.assertEqual(gl.start, 1010)
        self.assertEqual(gl.end, 7005)
        self.assertEqual(gl.complete, 4005)
        self.assertEqual(gl.tail_us, 3000)
        self.assertEqual(gl.starved, 2)
        self.assertEqual(rows[0].updates, 3)
        self.assertEqual(rows[0].starved, 2)
        self.assertEqual(rows[0].tail_us, 3000)

    def test_lists_back_to_back(self):
        first = LIST
        second = LIST + 0x40000
        game = Game()
        game.vblank(0, 1)
        game.enqueue(1000, first)
        game.enqueue(1100, second, ge=g.GE_DRAWING)
        game.finish(3000, 0, first + 0x80, handler=20)
        game.finish(5000, 0, second + 0x80)
        capture, lists, rows = self.decode(game, P)
        a, b = lists
        self.assertEqual((a.start, a.end, a.freed), (1010, 3000, 3020))
        self.assertEqual(b.start, 3020)
        self.assertEqual(b.ge_us, 1980)
        self.assertEqual(rows[0].ge_us, 1990 + 1980)

    def test_finish_outside_every_list(self):
        # A list that jumped to commands elsewhere ends there: its FINISH is taken as the queue's head's.
        game = Game()
        game.vblank(0, 1)
        game.enqueue(1000, LIST, stall=LIST + 0x100)
        game.enqueue(1100, LIST + 0x40000, stall=LIST + 0x40100, ge=g.GE_DRAWING)
        game.finish(3000, 0, 0x09400080)
        game.finish(5000, 0, LIST + 0x40100)
        capture, lists, rows = self.decode(game, P)
        a, b = lists
        self.assertEqual((a.ended_by, a.end), ("FINISH", 3000))
        self.assertEqual((b.ended_by, b.start, b.end), ("FINISH", 3020, 5000))
        self.assertFalse(capture.warnings)

    def test_list_without_callback(self):
        game = Game()
        game.vblank(0, 1)
        list_id = game.enqueue(1000, LIST, cbid=-1)
        game.call(g.LIST_SYNC, 1100, 6100, list_id, 0, ge=g.GE_DRAWING)
        game.call(g.LIST_SYNC, 6200, 6201, list_id, 1)  # a peek doesn't end anything
        capture, lists, rows = self.decode(game, P)
        (gl,) = lists
        self.assertEqual(gl.cbid, -1)
        self.assertEqual(gl.ended_by, "sync")
        self.assertEqual(gl.end, 6100)
        self.assertEqual(rows[0].sync_us, 5000)

    def test_vblanks_from_waits(self):
        game = Game(monitor=False)
        once_a_vblank(game, 120, offset=4321.0)
        capture, lists, rows = self.decode(game, 122 * P)
        times, source = g.vblank_times(capture)
        self.assertIn("vblank waits", source)
        # the waits return 15 us after each vblank: the fit lands there
        self.assertLessEqual({round((t - 4321) % P) for t in times}, {14, 15, 16})
        seconds = g.per_second(rows)
        self.assertAlmostEqual(seconds[1].renders_per_frame, 1.0)

    def test_vblanks_assumed(self):
        game = Game(monitor=False)
        game.enqueue(1000, LIST)
        game.finish(3000, 0, LIST + 0x80)
        capture = g.read_capture(game.write(self.folder.name, 1_000_000))
        times, source = g.vblank_times(capture)
        self.assertIn("assumed", source)
        self.assertEqual(times[0], 0)
        self.assertGreaterEqual(times[-1], 1_000_000)

    def test_timer_wrap(self):
        game = Game(start=0xFFFFFFFF - 500_000)
        once_a_vblank(game, 60)
        capture, lists, rows = self.decode(game, 60 * P)
        self.assertEqual(capture.length_us, round(60 * P))
        self.assertTrue(all(0 <= r.entry <= capture.length_us + 20 for r in capture.records))
        self.assertEqual(len(lists), 60)
        self.assertTrue(all(abs(gl.ge_us - 5000) <= 1 for gl in lists))

    def test_truncated_file(self):
        game = Game()
        once_a_vblank(game, 10)
        path = game.write(self.folder.name, 10 * P)
        with open(path, "rb") as file:
            data = file.read()
        cut = data[:g.HEADER_SIZE + 7 * g.RECORD_SIZE + 5]
        capture = g.parse_capture(cut)
        self.assertTrue(capture.truncated)
        self.assertEqual(len(capture.records), 7)
        self.assertTrue(any("ends after 7" in w for w in capture.warnings))
        self.assertTrue(any("thread table" in w for w in capture.warnings))

    def test_not_a_capture(self):
        with self.assertRaises(g.CaptureError):
            g.parse_capture(b"x" * 300)
        with self.assertRaises(g.CaptureError):
            g.parse_capture(b"PHGEPROF")

    def test_lists_from_before_the_capture(self):
        game = Game()
        game.vblank(0, 1)
        game.call(g.LIST_SYNC, 100, 101, 0x1234, 1, result=2, ge=g.GE_DRAWING)  # the GE busy at the start
        game.finish(2000, 0, 0x08F00080)  # a list queued before the capture
        game.enqueue(1500, LIST, ge=g.GE_DRAWING)
        game.finish(5000, 0, LIST + 0x80)
        capture, lists, rows = self.decode(game, P)
        (gl,) = lists
        self.assertEqual(gl.start, 2020)
        self.assertEqual(gl.end, 5000)
        self.assertTrue(any("before the capture" in w for w in capture.warnings))

    def test_open_calls_end_with_the_capture(self):
        game = Game()
        game.vblank(0, 1)
        game.records.append((g.WAIT_VBLANK_START, 1, 0, game.at(10_000), 0, g.GE_IDLE, 0, 0, 0, 0))  # still waiting
        capture, lists, rows = self.decode(game, P)
        self.assertIsNone(capture.records[-1].exit)
        self.assertEqual(rows[0].wait_us, round(P) - 10_000)

    @unittest.skipUnless(os.path.exists(PLUGIN_CAPTURE), "make hosttest in ../plugin writes it")
    def test_plugin_capture(self):
        # The plugin's structs (profile.h) and this decoder agree, across the timer's wrap.
        capture = g.read_capture(PLUGIN_CAPTURE)
        self.assertEqual((capture.header["discId"], capture.header["title"]), ("UCUS98653", "God of War: Chains of Olympus"))
        self.assertEqual((capture.header["cpuMhz"], capture.header["busMhz"]), (222, 111))
        self.assertFalse(capture.truncated or capture.warnings)
        self.assertEqual(capture.thread_name(1), "render")
        self.assertTrue(all(-20 <= r.entry < capture.length_us for r in capture.records))
        frames = len(capture.records) // 10
        self.assertEqual(len(capture.records), frames * 10)
        lists = g.display_lists(capture)
        self.assertEqual(len(lists), frames)
        self.assertTrue(all(gl.ended_by == "FINISH" for gl in lists))
        self.assertTrue(all(len(gl.updates) == 3 and gl.starved == 2 and 0 < gl.tail_us < gl.ge_us for gl in lists))
        self.assertIn("monitor", g.vblank_times(capture)[1])
        rows = g.per_vblank(capture, lists)
        full = [row for row in rows if row.lists]
        self.assertEqual(len(full), frames)
        self.assertTrue(all(row.finishes == 1 and row.signals == 1 and row.flips == 1 and row.updates == 3
                            for row in full))
        seconds = g.per_second(rows)
        self.assertAlmostEqual(seconds[1].renders_per_frame, 1.0)

    def test_report_and_json(self):
        game = Game()
        once_a_vblank(game, 70)
        path = game.write(self.folder.name, 70 * P, flags=g.MONITOR | g.FULL, dropped=12)
        out = io.StringIO()
        capture = g.read_capture(path)
        g.print_report(capture, out, show_lists=True, show_events=True)
        text = out.getvalue()
        self.assertIn("UCUS98653", text)
        self.assertIn("THE BUFFER FILLED", text)
        self.assertIn("per second", text)
        self.assertIn("per vblank", text)
        self.assertIn("display lists", text)
        report = g.report_json(capture)
        json.dumps(report)
        self.assertEqual(len(report["lists"]), 70)
        self.assertEqual(report["seconds"][1]["renders_per_frame"], 1.0)
        target = os.path.join(self.folder.name, "out.json")
        sink = io.StringIO()
        saved, sys.stdout = sys.stdout, sink
        try:
            self.assertEqual(g.main([path, "--json", target, "--no-vblanks", "--from", "0.5"]), 0)
        finally:
            sys.stdout = saved
        with open(target) as file:
            self.assertEqual(json.load(file)["header"]["discId"], "UCUS98653")
        self.assertIn("per second", sink.getvalue())
        self.assertNotIn("per vblank", sink.getvalue())

    def test_one_frame_buffer(self):
        # Killzone's way in Phobos: its lists draw elsewhere, and every sceDisplaySetFrameBuf names one buffer.
        game = Game()
        for k in range(20):
            game.vblank(k * P, 500 + k)
            game.flip(k * P + 1000, FRAMEBUFFERS[0])
        capture, lists, rows = self.decode(game, 20 * P)
        self.assertEqual(sum(row.flips for row in rows), 1)
        self.assertTrue(any("sets the same buffer (04000000, 20 times)" in w for w in capture.warnings))

    def test_model_charges(self):
        # Phobos's runner's captures: CoO's loop (sync the last list, flip, queue the next, no callbacks, no vblank
        # waits), each list charged by the model in two pieces, one set by its pixels, one by its primitives; and the
        # counts a dry run would give.
        game = Game()
        list_id = None
        for k in range(30):
            t = k * 6000
            if k % 3 == 0:
                game.vblank(t, 100 + k // 3)
            if list_id is not None:
                game.call(g.LIST_SYNC, t, t + 500, list_id, 0, ge=g.GE_DRAWING)
            game.flip(t + 600, FRAMEBUFFERS[k % 2], sync=0)
            list_id = game.enqueue(t + 700, LIST + (k % 2) * 0x10000, cbid=-1)
            game.records.append((g.MODEL, 1, 0, game.at(t + 705), game.at(t + 4705), g.GE_DRAWING, 400_000, 100,
                                 1_332_000, list_id))
            game.records.append((g.MODEL, 1, 0, game.at(t + 706), game.at(t + 5706), g.GE_DRAWING, 1000, 30_000,
                                 333_000, list_id))
        capture, lists, rows = self.decode(game, 30 * 6000)
        model = g.model_charges(capture, lists)
        self.assertEqual((model.charges, model.pixels, model.primitives, model.pixel_bound), (60, 30 * 401_000, 30 * 30_100, 30))
        self.assertAlmostEqual(model.ms, 30 * 5.0)
        self.assertTrue(all(gl.model_cycles == 1_665_000 and gl.model_pixels == 401_000 for gl in lists))
        self.assertEqual(g.counts(capture)[g.ENQUEUE], 30)
        self.assertEqual(g.counts(capture, 0, 60_000)[g.LIST_SYNC], 9)
        out = io.StringIO()
        g.print_report(capture, out, show_lists=True, show_counts=True)
        text = out.getvalue()
        self.assertIn("Phobos's GE model: 60 charges, 12.03 M pixels, 903000 primitives, 150.00 ms", text)
        self.assertIn("50% of the charges set by their pixels", text)
        self.assertIn("model ms", text)
        self.assertIn("sceGeListEnQueue                        30", text)
        report = g.report_json(capture)
        self.assertEqual(report["model"]["charges"], 60)
        self.assertEqual(report["counts"]["GE model's charge"], 60)
        self.assertEqual(report["lists"][0]["model_cycles"], 1_665_000)
        # a capture from the PSP has no charges, and no model line
        plain = Game()
        once_a_vblank(plain, 10)
        capture, lists, rows = self.decode(plain, 10 * P)
        self.assertIsNone(g.model_charges(capture, lists))
        out = io.StringIO()
        g.print_report(capture, out, show_lists=True)
        self.assertNotIn("GE model", out.getvalue())
        self.assertNotIn("model ms", out.getvalue())


if __name__ == "__main__":
    unittest.main()
