package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class PspVideoTest {
    @Test
    fun drawingThreadsAreAutoOrACount() {
        assertEquals(listOf(0, 1, 2, 4, 6, 8), PspDrawingThreads.choices)
        assertEquals(PspDrawingThreads.AUTO, PspDrawingThreads.choices.first())
        assertEquals("Auto (all cores but one)", PspDrawingThreads.label(0))
        assertEquals("1 (the emulation thread)", PspDrawingThreads.label(1))
        assertEquals("6", PspDrawingThreads.label(6))
    }

    @Test
    fun theCoreIsToldAChoiceOrAuto() {
        for (threads in PspDrawingThreads.choices) assertEquals(threads, PspDrawingThreads.forCore(threads))
        assertEquals(0, PspDrawingThreads.forCore(3))
        assertEquals(0, PspDrawingThreads.forCore(-1))
        assertEquals(0, PspDrawingThreads.forCore(64))
    }

    @Test
    fun thePictureIsDrawnAtTheNearestWholeMultiple() {
        assertEquals(4, pictureMultiple(480f, 272f, 1906f, 1080f))  // the RP6 held sideways: 3.97 times
        assertEquals(2, pictureMultiple(480f, 272f, 1080f, 612f))   // upright: 2.25 times
        assertEquals(3, pictureMultiple(480f, 272f, 1440f, 816f))   // Integer Scaled: 3 times exactly
        assertEquals(4, pictureMultiple(480f, 272f, 1920f, 1080f))  // Stretched: 4 across, 3.97 down
        assertEquals(1, pictureMultiple(480f, 272f, 400f, 227f))    // smaller than the picture
        assertEquals(4, pictureMultiple(480f, 272f, 3840f, 2160f))  // 4 at most
        assertEquals(1, pictureMultiple(0f, 0f, 1906f, 1080f))      // no picture yet
    }
}
