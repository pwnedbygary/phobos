package com.phobos.emulator.ui.theme

import com.phobos.emulator.ui.theme.SakuraCycle.Phase
import com.phobos.emulator.ui.theme.SakuraCycle.Sky
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class SakuraCycleTest {
    /**
     * A landscape phone's art grid (1920 by 1080 pixels at 4 dp art pixels and 2.5x density): the horizon, the
     * sun's and the moon's radii, and how far their glows reach beyond them.
     */
    private val horizon = 79
    private val sunRadius = 10.8f
    private val moonRadius = 12.96f
    private val sunGlow = 3f
    private val moonGlow = 5f

    private val moments = 0L..SakuraCycle.LENGTH step 50L

    /** The rows a body shows, its glow and all. */
    private fun sunRows(time: Long) = SakuraCycle.sunRun(time)?.let { SakuraCycle.shownRows(SakuraCycle.middleRow(it, sunRadius, sunGlow, horizon), sunRadius + sunGlow, horizon) } ?: IntRange.EMPTY
    private fun moonRows(time: Long) = SakuraCycle.moonRun(time)?.let { SakuraCycle.shownRows(SakuraCycle.middleRow(it, moonRadius, moonGlow, horizon), moonRadius + moonGlow, horizon) } ?: IntRange.EMPTY

    @Test fun thePhasesRunInOrderAndRepeat() {
        assertEquals(Phase.entries, Phase.entries.map { SakuraCycle.phase(SakuraCycle.start(it)) })
        assertEquals(Phase.entries.sumOf { it.length }, SakuraCycle.LENGTH)
        assertTrue("a slow cycle: ${SakuraCycle.LENGTH} ms", SakuraCycle.LENGTH >= 6 * 60_000L)
        for (time in listOf(0L, 61_234L, 333_333L)) {
            assertEquals(SakuraCycle.phase(time), SakuraCycle.phase(time + 7 * SakuraCycle.LENGTH))
            assertEquals(SakuraCycle.daylight(time), SakuraCycle.daylight(time - 3 * SakuraCycle.LENGTH))
        }
    }

    @Test fun theSunAndTheMoonAreNeverUpTogether() {
        for (time in moments) {
            assertTrue("both up at $time", sunRows(time).isEmpty() || moonRows(time).isEmpty())
        }
        for (phase in listOf(Phase.DUSK, Phase.DAWN)) {
            val from = SakuraCycle.start(phase) + 50
            for (time in from until from + phase.length - 100 step 50) {
                assertTrue("something up in the $phase at $time", sunRows(time).isEmpty() && moonRows(time).isEmpty())
            }
        }
    }

    @Test fun eachRisesOutOfTheHorizonAndSinksBackBelowIt() {
        for ((name, rows, radius) in listOf(Triple("sun", ::sunRows, sunRadius + sunGlow), Triple("moon", ::moonRows, moonRadius + moonGlow))) {
            val whole = SakuraCycle.shownRows(0, radius, Int.MAX_VALUE).count()
            var before = IntRange.EMPTY
            var rose = false
            for (time in moments) {
                val now = rows(time)
                // Clipped by the horizon until it's clear of it: what shows always stands on the horizon.
                assertTrue("the $name floats at $time: $now", now.isEmpty() || now.last == horizon - 1 || now.count() == whole)
                assertTrue("the $name jumps at $time: $before to $now", abs(now.count() - before.count()) <= 1)
                if (before.isEmpty() && !now.isEmpty()) {
                    assertEquals("the $name pops up at $time", listOf(horizon - 1), now.toList())
                    rose = true
                }
                if (!before.isEmpty() && now.isEmpty()) assertEquals("the $name vanishes at $time", listOf(horizon - 1), before.toList())
                before = now
            }
            assertTrue("the $name never rose", rose)
        }
    }

    @Test fun theDrawnSunAndMoonAppearAndGoOnlyAtTheHorizon() {
        val palette = PixelPalette(0xFF101020.toInt(), 0xFFF0F0F0.toInt(), 0xFFC060F0.toInt(), 0xFF60C0A0.toInt(), 0xFFF0B040.toInt(), true)
        val edges = listOf(SakuraCycle.LENGTH, SakuraCycle.start(Phase.DUSK), SakuraCycle.start(Phase.MOONRISE), SakuraCycle.start(Phase.DAWN))
        for ((cols, rows) in listOf(192 to 108, 108 to 192, 320 to 200, 240 to 135, 480 to 270)) {
            val art = SakuraArt(cols, rows, palette, SakuraMode.CYCLE)
            // As a run starts or ends, nothing of the body or its glow shows on either side of the moment.
            for (edge in edges) {
                assertTrue("${cols}x$rows at ${edge - 1}", art.landscape(edge - 1).contentEquals(art.landscape(edge)))
                assertTrue("${cols}x$rows at $edge", art.landscape(edge).contentEquals(art.landscape(edge + 1)))
            }
        }
    }

    @Test fun eachClimbsAHorizonToHorizonArc() {
        assertEquals(-1f, SakuraCycle.height(0f), 1e-6f)
        assertEquals(-1f, SakuraCycle.height(1f), 1e-6f)
        assertEquals(1f + SakuraCycle.ARC, SakuraCycle.height(0.5f), 1e-5f)
        val rising = Phase.SUNRISE.length.toFloat() / (Phase.SUNRISE.length + Phase.DAY.length + Phase.SUNSET.length)
        assertEquals("a diameter in the rising", 1f, SakuraCycle.height(rising), 1e-5f)
        for (step in 0..1000) {
            val run = step / 1000f
            assertEquals(SakuraCycle.height(run), SakuraCycle.height(1f - run), 1e-4f)
            if (step > 0) assertTrue("a kink at $run", abs(SakuraCycle.height(run) - SakuraCycle.height(run - 0.001f)) < 0.04f)
        }
        // It stays in the sky above the horizon: the moon's top at its highest is clear of the picture's top.
        assertTrue(SakuraCycle.middleRow(0.5f, moonRadius, moonGlow, horizon) - moonRadius > 2)
    }

    @Test fun theSkyIsFullNightBeforeTheMoonRisesAndUntilItSets() {
        val from = SakuraCycle.start(Phase.MOONRISE)
        val to = SakuraCycle.start(Phase.DAWN)
        for (time in from..to step 50L) {
            assertEquals("light at $time", 0f, SakuraCycle.daylight(time))
            val (a, b, x) = SakuraCycle.sky(time)
            assertTrue("the sky at $time: $a to $b at $x", (a == Sky.NIGHT || x == 1f) && (b == Sky.NIGHT || x == 0f))
        }
        assertNull(SakuraCycle.moonRun(from - 1))
    }

    @Test fun duskDarkensAndDawnLightensWithoutAJump() {
        for (phase in listOf(Phase.DUSK, Phase.DAWN)) {
            val from = SakuraCycle.start(phase)
            val light = (from..from + phase.length step 125L).map { SakuraCycle.daylight(it) }
            val steps = light.zipWithNext { a, b -> b - a }
            assertTrue("the $phase's light goes the wrong way", steps.all { if (phase == Phase.DUSK) it <= 0f else it >= 0f })
            assertTrue("the $phase is a turn: ${light.first()} to ${light.last()}", abs(light.last() - light.first()) >= 0.6f)
        }
        // From one frame to the next (eight a second) the light and the sky change by a sliver.
        fun weights(time: Long) = SakuraCycle.sky(time).let { (a, b, x) -> Sky.entries.map { (if (it == a) 1 - x else 0f) + (if (it == b) x else 0f) } }
        for (time in 0L until SakuraCycle.LENGTH step 125L) {
            assertTrue("the light jumps at $time", abs(SakuraCycle.daylight(time + 125) - SakuraCycle.daylight(time)) < 0.01f)
            val change = weights(time).zip(weights(time + 125)).maxOf { (a, b) -> abs(a - b) }
            assertTrue("the sky jumps at $time by $change", change < 0.01f)
        }
    }

    @Test fun theSunRisesAfterDawnAndSetsBeforeDusk() {
        assertEquals(Phase.SUNRISE, SakuraCycle.phase(0L))
        assertEquals(0f, SakuraCycle.sunRun(SakuraCycle.start(Phase.SUNRISE)))
        assertEquals(1f, SakuraCycle.sunRun(SakuraCycle.start(Phase.DUSK)))
        assertNull(SakuraCycle.sunRun(SakuraCycle.start(Phase.DUSK) + 1))
        assertEquals(0f, SakuraCycle.moonRun(SakuraCycle.start(Phase.MOONRISE)))
        assertEquals(1f, SakuraCycle.moonRun(SakuraCycle.start(Phase.DAWN)))
        assertTrue("dawn has begun at sunrise", SakuraCycle.daylight(SakuraCycle.start(Phase.SUNRISE)) > 0.5f)
        assertEquals(1f, SakuraCycle.daylight(SakuraCycle.start(Phase.DAY) + SakuraCycle.Phase.DAY.length / 2))
    }
}
