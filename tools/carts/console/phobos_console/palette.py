# SPDX-FileCopyrightText: 2026 Phobos contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Palettes for the Phobos system dioramas.

Colours are linear RGB: the preview renders go through AgX, so the authored
values are linear.  Each system module selects a palette through the SYSTEMS
table in ``systems/__init__.py``.
"""

# ---------------------------------------------------------------------------
# palette (linear RGB; the renders go through AgX)
# ---------------------------------------------------------------------------
SHELL = (0.50, 0.49, 0.455, 1.0)      # warm light grey SNES plastic
DARK = (0.022, 0.022, 0.026, 1.0)     # charcoal CRT cabinet and dark plastic
ACCENT = (0.16, 0.10, 0.44, 1.0)      # SNES purple (switches, A/B)
METAL = (0.44, 0.44, 0.45, 1.0)       # satin metal (pins, knob caps, badge)
ACCENT_LIGHT = (0.34, 0.31, 0.47, 1.0)  # SNES lavender X/Y buttons
GLASS = (0.02, 0.10, 0.05, 1.0)       # green LED lens
SCREEN = (0.006, 0.008, 0.014, 1.0)   # CRT face; the app stamps art over it

# The NES palette is its own authored one (the shells are not tintable): warm
# light-grey upper shell, a darker warm-grey lower chassis, black trim, Nintendo
# red for the wordmark, POWER and the A/B buttons, and a red LED lens.
NES_SHELL = (0.560, 0.552, 0.522, 1.0)
NES_DARK = (0.014, 0.014, 0.017, 1.0)
NES_ACCENT = (0.430, 0.022, 0.018, 1.0)
NES_ACCENT_LIGHT = (0.240, 0.232, 0.216, 1.0)
NES_METAL = (0.42, 0.42, 0.43, 1.0)
NES_GLASS = (0.36, 0.012, 0.012, 1.0)
NES_SCREEN = (0.006, 0.008, 0.014, 1.0)

SNES_PALETTE = {
    "shell": (SHELL, 0.42, 0.0, None, 0.0),
    "dark": (DARK, 0.44, 0.0, None, 0.0),
    "accent": (ACCENT, 0.34, 0.0, None, 0.0),
    "accent_light": (ACCENT_LIGHT, 0.38, 0.0, None, 0.0),
    "metal": (METAL, 0.28, 1.0, None, 0.0),
    "glass": (GLASS, 0.06, 0.0, (0.02, 0.35, 0.08, 1.0), 2.0),
    "screen": (SCREEN, 0.06, 0.0, (0.015, 0.03, 0.07, 1.0), 1.0),
}

NES_PALETTE = {
    "shell": (NES_SHELL, 0.44, 0.0, None, 0.0),
    "dark": (NES_DARK, 0.42, 0.0, None, 0.0),
    "accent": (NES_ACCENT, 0.36, 0.0, None, 0.0),
    "accent_light": (NES_ACCENT_LIGHT, 0.48, 0.0, None, 0.0),
    "metal": (NES_METAL, 0.30, 1.0, None, 0.0),
    "glass": (NES_GLASS, 0.06, 0.0, (0.55, 0.015, 0.012, 1.0), 2.2),
    "screen": (NES_SCREEN, 0.06, 0.0, (0.015, 0.03, 0.07, 1.0), 1.0),
}
