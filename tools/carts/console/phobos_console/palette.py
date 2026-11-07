# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Colours shared by every system's stage.

Each system authors its own palette in its own module (``systems/<name>.py``),
so tuning one console's plastic never touches another console's, and two authors
adding two systems never edit the same file.  Only the values the shared CRT
stage has to draw identically everywhere live here.

Colours are linear RGB: the preview renders go through AgX, so the authored
values are linear.
"""

# ---------------------------------------------------------------------------
# the shared CRT screen target
# ---------------------------------------------------------------------------
SCREEN = (0.006, 0.008, 0.014, 1.0)     # the CRT face the app stamps art over
SCREEN_GLOW = (0.015, 0.03, 0.07, 1.0)  # its faint cool glow

__all__ = ["SCREEN", "SCREEN_GLOW"]
