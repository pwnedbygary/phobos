package com.phobos.emulator.ui.touch

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutCodecTest {
    @Test fun roundTripKeepsEveryField() {
        val overrides = mapOf(
            "dpad" to ElementOverride(fx = 0.125f, fy = 0.75f, scale = 1.2f, hidden = false),
            "l3" to ElementOverride(hidden = true),
            "face" to ElementOverride(fx = 0.9f, fy = 0.8f),
        )
        val decoded = TouchLayoutCodec.decode(TouchLayoutCodec.encode(overrides))
        assertEquals(overrides, decoded)
    }

    @Test fun emptyInputDecodesToNoOverrides() {
        assertTrue(TouchLayoutCodec.decode(null).isEmpty())
        assertTrue(TouchLayoutCodec.decode("").isEmpty())
    }

    @Test fun malformedEntriesAreSkipped() {
        val decoded = TouchLayoutCodec.decode("garbage;|0.1|0.2|1|0;face|0.5|0.5|1.000|")
        assertEquals(setOf("face"), decoded.keys)
    }

    @Test fun valuesAreClamped() {
        val decoded = TouchLayoutCodec.decode("dpad|1.7|-3|9|1")
        val o = decoded.getValue("dpad")
        assertEquals(1f, o.fx!!, 0f)
        assertEquals(0f, o.fy!!, 0f)
        assertEquals(TouchLayoutCodec.MAX_ELEMENT_SCALE, o.scale, 0f)
        assertEquals(true, o.hidden)
    }

    @Test fun halfSpecifiedPositionFallsBackToDefault() {
        val o = TouchLayoutCodec.decode("dpad|0.5||1.000|").getValue("dpad")
        assertNull(o.fx)
        assertNull(o.fy)
    }

    @Test fun layoutKeysSeparateOrientations() {
        assertEquals("n64_land", touchLayoutKey(TouchFamily.N64, landscape = true))
        assertEquals("n64_port", touchLayoutKey(TouchFamily.N64, landscape = false))
    }
}
