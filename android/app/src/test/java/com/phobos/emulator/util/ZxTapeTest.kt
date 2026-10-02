package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ZxTapeTest {
    @Test fun theCoreStateReadsAsATape() {
        assertEquals(ZxTape(true, true, 42_000, 296_000), ZxTape.of(intArrayOf(1, 1, 42_000, 296_000)))
        assertEquals(ZxTape(), ZxTape.of(intArrayOf(0, 0, 0, 0)))
        assertEquals(ZxTape(), ZxTape.of(IntArray(0)))
    }

    @Test fun onlyATapeStoppedPartWayIsPaused() {
        assertTrue(ZxTape(true, false, 1_000, 2_000).paused)
        assertFalse(ZxTape(true, false, 0, 2_000).paused)
        assertFalse(ZxTape(true, false, 2_000, 2_000).paused)
        assertFalse(ZxTape(true, true, 1_000, 2_000).paused)
    }

    @Test fun theStatusReadsLikeATapeCounter() {
        assertEquals("Playing, 0:42 of 4:56", ZxTape(true, true, 42_500, 296_000).status)
        assertEquals("Stopped at 1:05 of 4:56", ZxTape(true, false, 65_000, 296_000).status)
        assertEquals("At the start, 4:56 long", ZxTape(true, false, 0, 296_000).status)
        assertEquals("At the end", ZxTape(true, false, 296_000, 296_000).status)
        assertEquals("No tape", ZxTape().status)
    }

    @Test fun progressStaysWithinTheTape() {
        assertEquals(0.5f, ZxTape(true, true, 1_000, 2_000).progress, 0f)
        assertEquals(1f, ZxTape(true, false, 3_000, 2_000).progress, 0f)
        assertEquals(0f, ZxTape().progress, 0f)
    }
}
