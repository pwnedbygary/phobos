#!/usr/bin/env python3
"""gate.py's verdict (docs/psp-core.md, part 106): each judged scene by its whole frame, the owner's rule; a list is
reported, never failing the gate; the owner's accepted exception passes the way he accepted and fails the other way.
usage: python3 test_gate.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate  # noqa: E402


class Verdict(unittest.TestCase):
    def test_a_frame_within_the_bar_passes_either_way(self):
        for miss in (0.0, 0.149, -0.15, 0.15):
            self.assertEqual(gate.judged_frame("UCUS98653-5", miss), ("passes", False))

    def test_a_frame_outside_it_fails(self):
        self.assertEqual(gate.judged_frame("UCUS98653-5", 0.151), ("too slow", True))
        self.assertEqual(gate.judged_frame("UCUS98737-1", -0.2), ("too fast", True))

    def test_the_owners_exception_passes_the_way_he_accepted(self):
        mark, fails = gate.judged_frame("ULES00364-1", -0.296)
        self.assertFalse(fails)
        self.assertIn("accepted exception", mark)

    def test_the_exception_fails_the_other_way(self):
        self.assertEqual(gate.judged_frame("ULES00364-1", 0.2), ("too slow", True))

    def test_every_exception_names_a_judged_capture_and_a_way(self):
        judged = {capture for _, capture, _, _, _, judge in gate.ROWS if judge}
        for capture, (way, reason) in gate.EXCEPTED.items():
            self.assertIn(capture, judged)
            self.assertIn(way, ("too fast", "too slow"))
            self.assertIn("2026-", reason)


if __name__ == "__main__":
    unittest.main()
