#!/usr/bin/env python3
"""gate.py's verdict (docs/psp-core.md, parts 106 and 113): each judged scene by its whole frame, the owner's rule; a
list is reported, never failing the gate; an accepted exception passes the way the owner accepted, no further than its
limit, and fails the other way or past it (none is accepted now: the tests make one up, on Ghost of Sparta's frame;
Super Monkey Ball's, gone since the refit, fails past the bar like any other).
usage: python3 test_gate.py"""
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate  # noqa: E402

MADE_UP = {"UCUS98737-1": ("too fast", -0.21, "a made-up exception for the tests, 2026-10-20")}


class Verdict(unittest.TestCase):
    def test_a_frame_within_the_bar_passes_either_way(self):
        for miss in (0.0, 0.149, -0.15, 0.15):
            self.assertEqual(gate.judged_frame("UCUS98653-5", miss), ("passes", False))

    def test_a_frame_outside_it_fails(self):
        self.assertEqual(gate.judged_frame("UCUS98653-5", 0.151), ("too slow", True))
        self.assertEqual(gate.judged_frame("UCUS98737-1", -0.2), ("too fast", True))

    @mock.patch.dict(gate.EXCEPTED, MADE_UP)
    def test_an_exception_passes_the_way_it_was_accepted(self):
        mark, fails = gate.judged_frame("UCUS98737-1", -0.206)
        self.assertFalse(fails)
        self.assertIn("accepted exception", mark)

    @mock.patch.dict(gate.EXCEPTED, MADE_UP)
    def test_an_exception_fails_past_its_limit(self):
        mark, fails = gate.judged_frame("UCUS98737-1", -0.296)
        self.assertTrue(fails)
        self.assertIn("limit", mark)

    @mock.patch.dict(gate.EXCEPTED, MADE_UP)
    def test_an_exception_fails_the_other_way(self):
        self.assertEqual(gate.judged_frame("UCUS98737-1", 0.2), ("too slow", True))

    def test_super_monkey_balls_frame_has_no_exception(self):
        self.assertEqual(gate.judged_frame("ULES00364-1", -0.206), ("too fast", True))

    def test_every_exception_names_a_judged_capture_a_way_and_a_limit(self):
        judged = {capture for _, capture, _, _, _, judge in gate.ROWS if judge}
        for capture, (way, limit, reason) in gate.EXCEPTED.items():
            self.assertIn(capture, judged)
            self.assertIn(way, ("too fast", "too slow"))
            self.assertTrue(abs(limit) > gate.BAR and (limit < 0) == (way == "too fast"))
            self.assertIn("2026-", reason)


if __name__ == "__main__":
    unittest.main()
