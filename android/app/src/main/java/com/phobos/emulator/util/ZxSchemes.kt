package com.phobos.emulator.util

/** A way the gamepad plays a ZX Spectrum game: as a joystick interface, or as keys of the keyboard. */
data class ZxScheme(val id: Int, val short: String, val label: String)

// In menu order. The ids are what's stored and what native code maps (zxSchemeKeys); 0-4 came first.
val ZX_SCHEMES = listOf(
    ZxScheme(0, "KEMP", "Kempston joystick"),
    ZxScheme(5, "SINC 1", "Sinclair 1 (keys 6–0)"),
    ZxScheme(6, "SINC 2", "Sinclair 2 (keys 1–5)"),
    ZxScheme(7, "CURSOR", "Cursor (keys 5–8, fire 0)"),
    ZxScheme(8, "QAOP", "QAOP + Space"),
    ZxScheme(1, "Q/P", "Q/P + Space (Manic Miner)"),
    ZxScheme(2, "Z/X", "Z/X + Space"),
    ZxScheme(3, "ELITE", "Elite"),
    ZxScheme(4, "CUSTOM", "Custom (long-press a key to bind it)"),
)

fun zxScheme(id: Int): ZxScheme = ZX_SCHEMES.firstOrNull { it.id == id } ?: ZX_SCHEMES.first()
