package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class ZxSchemesTest {
    @Test
    fun listsEverySchemeNativeCodeMapsOnce() {
        assertEquals((0..8).toList(), ZX_SCHEMES.map { it.id }.sorted())
    }

    @Test
    fun anUnknownSchemeIsKempston() {
        assertEquals("Kempston joystick", zxScheme(42).label)
        assertEquals("Q/P + Space (Manic Miner)", zxScheme(1).label)
    }
}
