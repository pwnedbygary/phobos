package com.phobos.emulator.ui.theme

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class XmbDeepTest {
    private val times = (0..100).map { it / 100f }
    private val across = (0..40).map { it / 40f }

    @Test fun theSheetsStayInTheirBandAndLoopSeamlessly() {
        val band = WaveColors.DEEP_BAND
        for (sheet in SHEETS) for (index in 0 until sheet.strands) for (u in across) {
            for (time in times) assertTrue("strand $index at $u, $time", sheet.strandAt(index, u, time) in band)
            assertEquals(sheet.strandAt(index, u, 0f), sheet.strandAt(index, u, 1f), 1e-4f)
        }
    }

    @Test fun eachSheetFansOutAndGathersAsItTurns() {
        for (sheet in SHEETS) {
            val spans = times.flatMap { time -> across.map { u -> sheet.strandAt(sheet.strands - 1, u, time) - sheet.strandAt(0, u, time) } }
            assertTrue("lies flat", spans.maxOf { kotlin.math.abs(it) } > sheet.spread * 0.95f)
            assertTrue("turns edge on", spans.minOf { kotlin.math.abs(it) } < sheet.spread * 0.05f)
            val fades = times.flatMap { time -> across.map { u -> sheet.fade(u, time) } }
            assertTrue(fades.all { it in Sheet.EDGE_ON..1f })
        }
    }

    @Test fun sparklesRiseThroughTheBandAndLoopSeamlessly() {
        val band = WaveColors.DEEP_BAND
        fun at(index: Int, time: Float) = FloatArray(3).also { Sparkles.at(index, time, it) }
        for (index in 0 until Sparkles.COUNT) {
            for (time in times) {
                val (x, y, brightness) = at(index, time)
                assertTrue("sparkle $index at $time: $x, $y", x in -0.02f..1.02f && y in band && brightness in 0f..1f)
            }
            val start = at(index, 0f)
            val end = at(index, 1f)
            assertEquals(start[0], end[0], 1e-4f)
            assertEquals(start[1], end[1], 1e-3f)
            assertEquals(start[2], end[2], 1e-3f)
        }
    }
}
