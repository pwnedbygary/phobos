package com.phobos.emulator.input

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CapturePolicyTest {
    private val up = 1 shl 0
    private val a = 1 shl 4
    private val leftStickUp = 1 shl 17

    // Axis ids: 0 X, 1 Y, 14 RZ, 15 HAT_X, 16 HAT_Y, 17 LTRIGGER.
    private fun axes(vararg values: Pair<Int, Float>): (Int) -> Float = { axis -> values.toMap()[axis] ?: 0f }

    @Test
    fun aDpadTargetIgnoresTheSticksAndTakesTheHat() {
        assertNull(CapturePolicy.axisBinding(up, axes(0 to 0.9f, 14 to -1f)))
        assertEquals("a:16:0", CapturePolicy.axisBinding(up, axes(0 to 0.9f, 16 to -1f)))
        assertEquals("a:15:1", CapturePolicy.axisBinding(up, axes(15 to 1f)))
    }

    @Test
    fun otherTargetsTakeAnyAxisPastTheThreshold() {
        assertEquals("a:1:0", CapturePolicy.axisBinding(leftStickUp, axes(1 to -0.8f)))
        assertEquals("a:17:1", CapturePolicy.axisBinding(a, axes(17 to 1f)))
        assertNull(CapturePolicy.axisBinding(a, axes(0 to 0.4f, 1 to -0.3f)))
    }

    @Test
    fun onlyAxesPastTheThresholdCountAsHeld() {
        assertEquals(emptySet<Int>(), CapturePolicy.deflectedAxes(axes(0 to 0.2f, 1 to -0.35f)))
        assertEquals(setOf(0, 16), CapturePolicy.deflectedAxes(axes(0 to 0.9f, 16 to -1f)))
        assertEquals(setOf(15), CapturePolicy.stillHeld(setOf(15, 16), axes(15 to 1f, 16 to 0.2f)))
    }

    @Test
    fun aStickTargetIgnoresDpadKeys() {
        assertNull(CapturePolicy.keyBinding(leftStickUp, 19))
        assertEquals("k:96", CapturePolicy.keyBinding(leftStickUp, 96))
        assertEquals("k:19", CapturePolicy.keyBinding(up, 19))
        assertEquals("k:97", CapturePolicy.keyBinding(a, 97))
    }

    @Test
    fun triggersAndHatsArePairedWithTheirKeys() {
        // 104 BUTTON_L2, 105 BUTTON_R2, 19-22 the D-pad.
        assertEquals(setOf(104), CapturePolicy.pairedKeys("a:17:1"))
        assertEquals(setOf(105), CapturePolicy.pairedKeys("a:22:1"))
        assertEquals(setOf(21, 22), CapturePolicy.pairedKeys("a:15:0"))
        assertEquals(setOf(19, 20), CapturePolicy.pairedKeys("a:16:1"))
        assertEquals(emptySet<Int>(), CapturePolicy.pairedKeys("a:0:1"))
        assertEquals(emptySet<Int>(), CapturePolicy.pairedKeys("k:22"))
    }

    @Test
    fun aHatPressesTheDpadKeys() {
        // 19 up, 20 down, 21 left, 22 right.
        assertEquals(listOf(19, 22), CapturePolicy.hatKeys(axes(15 to 1f, 16 to -1f)))
        assertEquals(listOf(20, 21), CapturePolicy.hatKeys(axes(15 to -1f, 16 to 1f)))
        assertEquals(emptyList<Int>(), CapturePolicy.hatKeys(axes(15 to 0.3f, 0 to 1f)))
    }
}
