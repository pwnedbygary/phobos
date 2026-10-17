package com.phobos.emulator.util

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin

class ShotTimelineTest {
    // A PSP picture on a 20:9 phone; a 4:3 one on a tablet; a phone held upright with the touch controls, its picture at
    // the top under the camera's cutout; a WonderSwan held upright (a tall picture); none known yet (the PSP's shape); a
    // phone turned the other way with its three-button bar on the left and the status bar showing.
    private val psp = ShotGeometry(2400f, 1080f, PxRect(247.06f, 0f, 1905.88f, 1080f), 0f, top = false)
    private val snes = ShotGeometry(2048f, 1536f, PxRect(0f, 0f, 2048f, 1536f), 0f, top = false)
    private val upright = ShotGeometry(1080f, 2400f, PxRect(0f, 90f, 1080f, 612f), 0f, top = true, PxInsets(top = 90f, bottom = 63f))
    private val wonderSwan = ShotGeometry(2400f, 1080f, PxRect(852.86f, 0f, 694.29f, 1080f), 0f, top = false)
    private val unknown = ShotGeometry(2400f, 1080f, null, 0f, top = false)
    private val barOnTheLeft = ShotGeometry(2400f, 1080f, PxRect(318f, 0f, 1905.88f, 1080f), 0f, top = true, PxInsets(left = 126f, top = 63f))
    private val screens = listOf(psp, snes, upright, wonderSwan, unknown, barOnTheLeft)

    private fun assertNear(expected: Float, actual: Float, tolerance: Float = 0.05f, what: String = "") =
        assertTrue("$what: expected $expected, was $actual", abs(expected - actual) <= tolerance)

    /** Where the corners of [r], a rectangle on the card in its own pixels, are on the screen at [t]: its layer turns and scales it about its middle. */
    private fun onScreen(g: ShotGeometry, timeline: ShotTimeline, t: Float, r: PxRect): List<Pair<Float, Float>> {
        val scale = timeline.cardScale(t)
        val turn = Math.toRadians(timeline.cardTurn(t).toDouble())
        val c = cos(turn).toFloat()
        val s = sin(turn).toFloat()
        val px = g.film.width / 2f
        val py = g.film.height / 2f
        return listOf(r.left to r.top, r.right to r.top, r.right to r.bottom, r.left to r.bottom).map { (x, y) ->
            val dx = (x - px) * scale
            val dy = (y - py) * scale
            (g.film.left + px + dx * c - dy * s + timeline.cardX(t) + timeline.trayX(t)) to (g.film.top + py + dx * s + dy * c + timeline.cardY(t))
        }
    }

    private val wholeCard = { g: ShotGeometry -> PxRect(0f, 0f, g.film.width, g.film.height) }

    @Test
    fun theCardsPictureStartsExactlyOverTheGamePicture() {
        assertEquals(PxRect(247.06f, 0f, 1905.88f, 1080f), psp.picture)
        // Not known: a PSP picture fitted to the screen, as the game screen draws one.
        assertNear(1905.88f, unknown.picture.width, 0.01f)
        assertNear(247.06f, unknown.picture.left, 0.01f)
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            val corners = onScreen(g, timeline, 0f, g.frame)
            assertNear(g.picture.left, corners[0].first, what = "left")
            assertNear(g.picture.top, corners[0].second, what = "top")
            assertNear(g.picture.right, corners[2].first, what = "right")
            assertNear(g.picture.bottom, corners[2].second, what = "bottom")
            // Washed white, the flash.
            assertEquals(0.7f, timeline.develop(0f), 0f)
        }
    }

    @Test
    fun theCardRestsInItsCornerWithTheFilmWoundOnTwoHoles() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            assertEquals(1f, timeline.cardScale(600f), 0f)
            assertEquals(-3.5f, timeline.cardTurn(600f), 0f)
            assertEquals(0f, timeline.cardX(1000f), 0f)
            assertEquals(0f, timeline.develop(650f), 0f)
            assertEquals(-2f * g.pitch, timeline.holes(980f), 0f)
            // A quarter of the screen's width, 3u from its left edge; 3.2u above its bottom, or below its top with the
            // touch controls showing (their D-pad has the bottom-left corner); clear of the bars and the cutout.
            assertNear(g.inset.left + 0.03f * g.width, g.film.left)
            assertTrue(g.film.width in 0.14f * g.width..0.28f * g.width)
            if (g.top) assertNear(g.inset.top + 3.2f * g.u, g.film.top) else assertNear(g.height - g.inset.bottom - 3.2f * g.u, g.film.bottom)
        }
    }

    @Test
    fun theCardEndsInTheFolderBetweenItsPanelsWithItsTopShowing() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            for (t in listOf(1830f, 2000f, 2380f)) {
                val corners = onScreen(g, timeline, t, wholeCard(g))
                val xs = corners.map { it.first }
                val ys = corners.map { it.second }
                assertNear(g.back.centerX, xs.average().toFloat(), what = "middle at $t")
                assertTrue("inside the folder at $t", xs.min() > g.back.left && xs.max() < g.back.right)
                assertTrue("top showing at $t", ys.min() < g.front.top)
                assertTrue("sunk in at $t", ys.max() > g.front.top)
                assertTrue("bottom behind the front panel at $t", ys.max() < g.front.bottom)
            }
            // 80% of the folder's width, but for a tall picture.
            val width = g.film.width * timeline.cardScale(2000f)
            if (g === wonderSwan) assertTrue(width < 0.8f * g.back.width) else assertNear(0.8f * g.back.width, width)
        }
    }

    @Test
    fun itHopsUpOverTheFolderOnTheWay() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            val corners = onScreen(g, timeline, 1390f, wholeCard(g))
            // Its middle 5u above the folder's top, all of it still on the screen.
            assertNear(g.back.top - 5f * g.u, corners.map { it.second }.average().toFloat(), what = "middle")
            assertTrue("on the screen", corners.minOf { it.second } > 0f)
            assertEquals(9f, timeline.cardTurn(1390f), 0f)
        }
    }

    @Test
    fun theFolderOpensForTheCardAndShutsAfterIt() {
        val timeline = ShotTimeline(psp, reduced = false)
        assertEquals(0f, timeline.folderAlpha(559f), 0f)
        assertEquals(0f, timeline.folderScaleX(559f), 0f)
        assertEquals(1f, timeline.folderScaleX(960f), 0f)
        assertEquals(0f, timeline.flap(960f), 0f)
        assertEquals(-44f, timeline.flap(1100f), 0f)
        assertEquals(-44f, timeline.flap(1570f), 0f)
        // Snaps shut a little past closed, and the folder squashes.
        assertEquals(5f, timeline.flap(1650f), 0f)
        assertEquals(0f, timeline.flap(1760f), 0f)
        assertEquals(1.12f, timeline.folderScaleX(1690f), 0f)
        assertEquals(0.86f, timeline.folderScaleY(1690f), 0f)
        assertEquals(1f, timeline.folderScaleY(1910f), 0f)
        // The sparkles burst as it shuts, and are gone in under half a second.
        for (i in 0 until ShotTimeline.SPARKS) {
            assertEquals(0f, timeline.sparkAlpha(i, 1640f), 0f)
            assertEquals(1f, timeline.sparkAlpha(i, 1640f + 12f * i + 100f), 0f)
            assertEquals(0f, timeline.sparkAlpha(i, 1640f + 12f * i + 460f), 0f)
        }
        assertEquals(0f, timeline.pillAlpha(1720f), 0f)
        assertEquals(1f, timeline.pillAlpha(2000f), 0f)
    }

    @Test
    fun atTheEndEverythingIsGoneOffToTheLeft() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            val end = ShotTimeline.TOTAL_MS
            assertEquals(end, timeline.duration, 0f)
            assertEquals(0f, timeline.trayAlpha(end), 0f)
            assertEquals(0f, timeline.trayAlpha(end + 100f), 0f)
            for (i in 0 until ShotTimeline.SPARKS) assertEquals(0f, timeline.sparkAlpha(i, end), 0f)
            // The card, the folder and the pill's start are past the screen's left edge.
            assertTrue(onScreen(g, timeline, end, wholeCard(g)).maxOf { it.first } < 0f)
            assertTrue(g.back.right + timeline.trayX(end) < 0f)
            assertTrue(g.pillLeft + timeline.trayX(end) < 0f)
        }
        val failed = ShotErrorTimeline(psp, reduced = false)
        assertEquals(0f, failed.pillAlpha(ShotErrorTimeline.TOTAL_MS), 0f)
    }

    @Test
    fun nothingIsEverNotANumber() {
        for (g in screens) {
            for (reduced in listOf(false, true)) {
                val timeline = ShotTimeline(g, reduced)
                val failed = ShotErrorTimeline(g, reduced)
                var t = -100f
                while (t <= 3100f) {
                    val values = mutableListOf(
                        timeline.cardX(t), timeline.cardY(t), timeline.cardScale(t), timeline.cardTurn(t), timeline.develop(t),
                        timeline.holes(t), timeline.folderScaleX(t), timeline.folderScaleY(t), timeline.folderAlpha(t), timeline.flap(t),
                        timeline.pillX(t), timeline.pillScale(t), timeline.pillAlpha(t), timeline.trayX(t), timeline.trayAlpha(t),
                        failed.pillX(t), failed.pillScale(t), failed.pillAlpha(t),
                    )
                    for (i in 0 until ShotTimeline.SPARKS) {
                        values += listOf(timeline.sparkX(i, t), timeline.sparkY(i, t), timeline.sparkScale(i, t), timeline.sparkTurn(i, t), timeline.sparkAlpha(i, t))
                    }
                    val opacities = mutableListOf(timeline.folderAlpha(t), timeline.pillAlpha(t), timeline.trayAlpha(t), failed.pillAlpha(t))
                    // A note that comes late, or never.
                    for (noteAt in listOf(2500f, Float.POSITIVE_INFINITY)) {
                        values += listOf(timeline.pillX(t, noteAt), timeline.pillScale(t, noteAt), timeline.trayX(t, noteAt))
                        opacities += listOf(timeline.pillAlpha(t, noteAt), timeline.trayAlpha(t, noteAt))
                    }
                    assertTrue("at $t: $values", (values + opacities).all { it.isFinite() })
                    for (alpha in opacities) {
                        assertTrue("an opacity of $alpha at $t", alpha in 0f..1f)
                    }
                    t += 0.5f
                }
            }
        }
    }

    @Test
    fun nothingJumpsWhereOneKeyframeMeetsTheNext() {
        for (g in screens) {
            val tracks = ShotTimeline(g, reduced = false).tracks + ShotErrorTimeline(g, reduced = false).tracks
            for (track in tracks) {
                val times = track.keyTimes
                val values = times.map { track.at(it) }
                val range = maxOf(1f, values.max() - values.min())
                for (k in times) {
                    val before = track.at(k - 0.01f)
                    val after = track.at(k + 0.01f)
                    assertTrue("at $k: $before, then $after", abs(after - before) <= 0.01f * range)
                }
            }
        }
    }

    @Test
    fun removeAnimationsHoldsTheCardInTheFolderWithThePill() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = true)
            assertEquals(ShotTimeline.REDUCED_MS, timeline.duration, 0f)
            var t = 0f
            while (t <= ShotTimeline.REDUCED_MS) {
                assertEquals(g.inX, timeline.cardX(t), 0f)
                assertEquals(g.inY, timeline.cardY(t), 0f)
                assertEquals(g.inScale, timeline.cardScale(t), 0f)
                assertEquals(-2f, timeline.cardTurn(t), 0f)
                assertEquals(0f, timeline.develop(t), 0f)
                assertEquals(0f, timeline.holes(t), 0f)
                assertEquals(0f, timeline.flap(t), 0f)
                assertEquals(1f, timeline.folderAlpha(t), 0f)
                assertEquals(1f, timeline.folderScaleX(t), 0f)
                assertEquals(1f, timeline.folderScaleY(t), 0f)
                assertEquals(1f, timeline.pillAlpha(t), 0f)
                assertEquals(1f, timeline.pillScale(t), 0f)
                assertEquals(0f, timeline.pillX(t), 0f)
                assertEquals(1f, timeline.trayAlpha(t), 0f)
                assertEquals(0f, timeline.trayX(t), 0f)
                for (i in 0 until ShotTimeline.SPARKS) assertEquals(0f, timeline.sparkAlpha(i, t), 0f)
                t += 25f
            }
            val failed = ShotErrorTimeline(g, reduced = true)
            assertEquals(1f, failed.pillAlpha(0f), 0f)
            assertEquals(1f, failed.pillAlpha(ShotErrorTimeline.TOTAL_MS), 0f)
            assertEquals(0f, failed.pillX(0f), 0f)
        }
    }

    /** A value of the preview's: [what] at [ms]; with a span ([from] to [to]), the eased share of its travel across it. */
    private class Golden(val what: String, val ms: Float, val value: Float, val from: Float = 0f, val to: Float = 0f)

    // The preview (scratch/screenshot-fx/preview.html) run in Chrome, its animations sampled mid-span as the browser
    // computed them (scratch/screenshot-feedback/golden): turns in degrees, the pill's offset in hundredths of the
    // screen's width, the holes in hole pitches, a sparkle's reach as a share of its own. Travel is compared as the eased
    // share of its span: the preview builds its keyframes from whole-pixel layout values.
    private val golden = listOf(
        Golden("cardX", 120f, 0.83387f, 0f, 330f),
        Golden("cardY", 120f, 0.83387f, 0f, 330f),
        Golden("cardX", 1115f, 0.68464f, 1060f, 1170f),
        Golden("cardY", 1115f, 0.68464f, 1060f, 1170f),
        Golden("cardX", 1280f, 0.87645f, 1170f, 1390f),
        Golden("cardY", 1280f, 0.87645f, 1170f, 1390f),
        Golden("cardX", 1500f, 0.18657f, 1390f, 1600f),
        Golden("cardY", 1500f, 0.18657f, 1390f, 1600f),
        Golden("cardY", 1610f, 0.16057f, 1600f, 1700f),
        Golden("cardY", 1735f, 0.15033f, 1700f, 1830f),
        Golden("cardScale", 120f, 0.83387f, 0f, 330f),
        Golden("cardTurn", 120f, -5.00321f),
        Golden("cardTurn", 400f, -3.60375f),
        Golden("cardTurn", 530f, -2.93393f),
        Golden("cardTurn", 1115f, -5.89622f),
        Golden("cardTurn", 1280f, 7.02326f),
        Golden("cardTurn", 1500f, 6.94769f),
        Golden("cardScale", 400f, 0.99608f),
        Golden("cardScale", 530f, 1.00849f),
        Golden("cardScale", 1115f, 1.02397f),
        Golden("cardScale", 1280f, 0.67127f),
        Golden("cardScale", 1500f, 0.57242f),
        Golden("develop", 120f, 0.49955f),
        Golden("holes", 400f, -0.00722f),
        Golden("holes", 530f, -0.83433f),
        Golden("holes", 800f, -1.08765f),
        Golden("folderScaleX", 650f, 1.03199f),
        Golden("folderScaleY", 650f, 1.03199f),
        Golden("folderScaleX", 800f, 1.04000f),
        Golden("folderScaleY", 800f, 1.04000f),
        Golden("folderScaleX", 900f, 0.97328f),
        Golden("folderScaleY", 900f, 0.97328f),
        Golden("folderScaleX", 1650f, 1.08216f),
        Golden("folderScaleY", 1650f, 0.90415f),
        Golden("folderScaleX", 1720f, 1.09376f),
        Golden("folderScaleY", 1720f, 0.88933f),
        Golden("folderScaleX", 1880f, 0.99228f),
        Golden("folderScaleY", 1880f, 1.00772f),
        Golden("folderAlpha", 650f, 0.92142f),
        Golden("flap", 1030f, -40.03879f),
        Golden("flap", 1610f, -36.86266f),
        Golden("flap", 1720f, 0.90981f),
        Golden("pillX", 1800f, 0.33875f),
        Golden("pillScale", 1800f, 1.03303f),
        Golden("pillAlpha", 1800f, 1.00000f),
        Golden("pillX", 1950f, 0.09461f),
        Golden("pillScale", 1950f, 1.00946f),
        Golden("pillAlpha", 1950f, 1.00000f),
        Golden("trayX", 2500f, 0.06800f, 2380f, 2700f),
        Golden("trayAlpha", 2500f, 0.93200f),
        Golden("sparkReach0", 1650f, 0.00000f),
        Golden("sparkScale0", 1650f, 0.35000f),
        Golden("sparkTurn0", 1650f, 0.00000f),
        Golden("sparkAlpha0", 1650f, 0.50000f),
        Golden("sparkReach0", 1720f, 0.57672f),
        Golden("sparkScale0", 1720f, 0.86112f),
        Golden("sparkTurn0", 1720f, 53.79741f),
        Golden("sparkAlpha0", 1720f, 1.00000f),
        Golden("sparkReach0", 2000f, 0.84737f),
        Golden("sparkScale0", 2000f, 0.68949f),
        Golden("sparkTurn0", 2000f, 97.16980f),
        Golden("sparkAlpha0", 2000f, 0.61186f),
        Golden("sparkReach8", 1765f, 0.12543f),
        Golden("sparkScale8", 1765f, 0.50033f),
        Golden("sparkTurn8", 1765f, 11.70480f),
        Golden("sparkAlpha8", 1765f, 1.00000f),
        Golden("sparkReach8", 2000f, 0.75902f),
        Golden("sparkScale8", 2000f, 0.97152f),
        Golden("sparkTurn8", 2000f, 72.49160f),
        Golden("sparkAlpha8", 2000f, 0.96441f),
    )

    @Test
    fun midSpanEverythingMovesAsThePreviewDoes() {
        val g = unknown
        val timeline = ShotTimeline(g, reduced = false)
        val reach = mapOf(0 to 8f, 8 to 12.5f)
        fun share(of: (Float) -> Float, gold: Golden) = (of(gold.ms) - of(gold.from)) / (of(gold.to) - of(gold.from))
        for (gold in golden) {
            val t = gold.ms
            val spark = gold.what.last().digitToIntOrNull() ?: 0
            val actual = when (gold.what) {
                "cardX" -> share(timeline::cardX, gold)
                "cardY" -> share(timeline::cardY, gold)
                "cardScale" -> if (gold.to > 0f) share(timeline::cardScale, gold) else timeline.cardScale(t)
                "cardTurn" -> timeline.cardTurn(t)
                "develop" -> timeline.develop(t)
                "holes" -> timeline.holes(t) / g.pitch
                "folderScaleX" -> timeline.folderScaleX(t)
                "folderScaleY" -> timeline.folderScaleY(t)
                "folderAlpha" -> timeline.folderAlpha(t)
                "flap" -> timeline.flap(t)
                "pillX" -> timeline.pillX(t) / g.u
                "pillScale" -> timeline.pillScale(t)
                "pillAlpha" -> timeline.pillAlpha(t)
                "trayX" -> share(timeline::trayX, gold)
                "trayAlpha" -> timeline.trayAlpha(t)
                "sparkReach$spark" -> hypot(timeline.sparkX(spark, t), timeline.sparkY(spark, t)) / (reach.getValue(spark) * g.u)
                "sparkScale$spark" -> timeline.sparkScale(spark, t)
                "sparkTurn$spark" -> timeline.sparkTurn(spark, t)
                "sparkAlpha$spark" -> timeline.sparkAlpha(spark, t)
                else -> error(gold.what)
            }
            val tolerance = if (gold.what.startsWith("sparkTurn") || gold.what == "cardTurn" || gold.what == "flap") 0.01f else 2e-3f
            assertNear(gold.value, actual, tolerance, "${gold.what} at ${gold.ms} ms")
        }
    }

    /** The pill and the slide-off at [t], their note known at [noteAt], are an on-time note's [by] ms earlier. */
    private fun assertLater(timeline: ShotTimeline, t: Float, noteAt: Float, by: Float) {
        val what = "at $t, the note at $noteAt"
        assertEquals(what, timeline.pillX(t - by), timeline.pillX(t, noteAt), 1e-3f)
        assertEquals(what, timeline.pillScale(t - by), timeline.pillScale(t, noteAt), 1e-5f)
        assertEquals(what, timeline.pillAlpha(t - by), timeline.pillAlpha(t, noteAt), 1e-5f)
        assertEquals(what, timeline.trayX(t - by), timeline.trayX(t, noteAt), 1e-3f)
        assertEquals(what, timeline.trayAlpha(t - by), timeline.trayAlpha(t, noteAt), 1e-5f)
    }

    @Test
    fun aNoteKnownByThePillsTimeChangesNothing() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            for (noteAt in listOf(0f, 900f, ShotTimeline.PILL_AT)) {
                assertEquals(ShotTimeline.TOTAL_MS, ShotTimeline.end(noteAt), 0f)
                var t = -10f
                while (t <= ShotTimeline.TOTAL_MS + 100f) {
                    assertEquals(timeline.pillX(t), timeline.pillX(t, noteAt), 0f)
                    assertEquals(timeline.pillScale(t), timeline.pillScale(t, noteAt), 0f)
                    assertEquals(timeline.pillAlpha(t), timeline.pillAlpha(t, noteAt), 0f)
                    assertEquals(timeline.trayX(t), timeline.trayX(t, noteAt), 0f)
                    assertEquals(timeline.trayAlpha(t), timeline.trayAlpha(t, noteAt), 0f)
                    t += 0.5f
                }
            }
        }
    }

    @Test
    fun aLateNoteKeepsThePillsWholeSlideAndItsTimeUpBeforeTheSlideOff() {
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            // The file's written at 2.5 s, when the slide-off would have been under way.
            val noteAt = 2500f
            val late = noteAt - ShotTimeline.PILL_AT
            assertEquals(ShotTimeline.TOTAL_MS + late, ShotTimeline.end(noteAt), 0f)
            // Until then the pill waits, unseen, and everything else stays where it is.
            var t = ShotTimeline.PILL_AT
            while (t <= noteAt) {
                assertEquals(0f, timeline.pillAlpha(t, noteAt), 0f)
                assertEquals(1f, timeline.trayAlpha(t, noteAt), 0f)
                assertEquals(0f, timeline.trayX(t, noteAt), 0f)
                t += 5f
            }
            // Then all of it as on time, that much later: the slide-in, the pill up 660 ms before the slide-off begins.
            t = noteAt
            while (t <= ShotTimeline.end(noteAt) + 100f) {
                assertLater(timeline, t, noteAt, late)
                t += 2.5f
            }
            assertEquals(1f, timeline.pillAlpha(noteAt + 180f, noteAt), 0f)
            assertEquals(0f, timeline.pillX(noteAt + 280f, noteAt), 0f)
            assertEquals(1f, timeline.trayAlpha(noteAt + 660f, noteAt), 0f)
            assertEquals(0f, timeline.trayX(noteAt + 660f, noteAt), 0f)
            assertEquals(0f, timeline.trayAlpha(ShotTimeline.end(noteAt), noteAt), 0f)
            // Nothing jumps as the note comes.
            assertEquals(timeline.pillAlpha(noteAt - 0.01f, noteAt), timeline.pillAlpha(noteAt + 0.01f, noteAt), 1e-3f)
            assertEquals(timeline.pillX(noteAt - 0.01f, noteAt), timeline.pillX(noteAt + 0.01f, noteAt), 0.05f)
        }
    }

    @Test
    fun aWriteThatFailsAfterTheCardIsInStillShowsItsPill() {
        // The note comes after the on-time effect would have ended (a slow card's write failing at last): the card waits
        // in the folder, then the pill (the error pill, for a failure) slides out and stays up before it all goes.
        val timeline = ShotTimeline(psp, reduced = false)
        val noteAt = 3200f
        var t = 2380f
        while (t < noteAt) {
            assertEquals(1f, timeline.trayAlpha(t, noteAt), 0f)
            assertEquals(0f, timeline.trayX(t, noteAt), 0f)
            assertEquals(0f, timeline.pillAlpha(t, noteAt), 0f)
            assertEquals(psp.inX, timeline.cardX(t), 0f)
            t += 10f
        }
        assertEquals(1f, timeline.pillAlpha(noteAt + 180f, noteAt), 0f)
        assertEquals(1f, timeline.trayAlpha(noteAt + 660f, noteAt), 0f)
        assertEquals(ShotTimeline.TOTAL_MS + noteAt - ShotTimeline.PILL_AT, ShotTimeline.end(noteAt), 0f)
        assertEquals(0f, timeline.trayAlpha(ShotTimeline.end(noteAt), noteAt), 0f)
    }

    @Test
    fun withoutANoteTheEffectStillEnds() {
        val timeline = ShotTimeline(psp, reduced = false)
        val never = Float.POSITIVE_INFINITY
        val end = ShotTimeline.end(never)
        assertEquals(ShotTimeline.TOTAL_MS + ShotTimeline.NOTE_WAIT_MS, end, 0f)
        assertEquals(1f, timeline.trayAlpha(ShotTimeline.PILL_AT + ShotTimeline.NOTE_WAIT_MS, never), 0f)
        assertEquals(0f, timeline.trayAlpha(end, never), 0f)
        assertTrue(timeline.trayX(end, never) + psp.back.right < 0f)
    }

    @Test
    fun onceSettledNothingButThePillAndTheSlideOffMoves() {
        // What lets the clock hold still at SETTLED_MS while a late note is awaited.
        assertEquals(2196f, ShotTimeline.SETTLED_MS, 0f)
        for (g in screens) {
            val timeline = ShotTimeline(g, reduced = false)
            val settled = ShotTimeline.SETTLED_MS
            fun still(t: Float): List<Float> {
                val values = mutableListOf(
                    timeline.cardX(t), timeline.cardY(t), timeline.cardScale(t), timeline.cardTurn(t), timeline.develop(t),
                    timeline.holes(t), timeline.folderScaleX(t), timeline.folderScaleY(t), timeline.folderAlpha(t), timeline.flap(t),
                )
                for (i in 0 until ShotTimeline.SPARKS) {
                    values += listOf(timeline.sparkX(i, t), timeline.sparkY(i, t), timeline.sparkScale(i, t), timeline.sparkTurn(i, t), timeline.sparkAlpha(i, t))
                }
                return values
            }
            val atRest = still(settled)
            var t = settled
            while (t <= ShotTimeline.TOTAL_MS + ShotTimeline.NOTE_WAIT_MS) {
                assertEquals("at $t", atRest, still(t))
                t += 1f
            }
        }
    }

    /** A saved shot's clock for runSavedShot, logging what it's told: its note known [from] the start, or coming as the clock passes [comesAt], or while it's held ([whileHeld]), or never. */
    private class FakeClock(from: Float = Float.POSITIVE_INFINITY, private val comesAt: Float? = null, private val whileHeld: Boolean = false) :
        ShotClockRun {
        override var time = 0f
        override var noteAt = from
        val log = mutableListOf<String>()

        override suspend fun runTo(target: Float) {
            log += "run to $target"
            if (comesAt != null && comesAt <= target && noteAt > comesAt) noteAt = comesAt
            time = target
        }

        override suspend fun awaitNote(ms: Long): Boolean {
            log += "held at $time for at most $ms ms"
            if (whileHeld) noteAt = time
            return whileHeld
        }

        override fun settleNoteAt(time: Float) {
            log += "note taken at $time"
            noteAt = time
        }
    }

    private fun runs(clock: FakeClock): List<String> {
        runBlocking { clock.runSavedShot() }
        return clock.log
    }

    @Test
    fun theClockHoldsStillWhileALateNoteIsAwaited() {
        val settled = ShotTimeline.SETTLED_MS
        val after = ShotTimeline.end(settled)
        // Late: on to where everything but the pill has stopped, held there (no run, so no frames) until the note
        // comes, then on from there, the held time standing for the note's.
        val late = FakeClock(whileHeld = true)
        assertEquals(listOf("run to $settled", "held at $settled for at most 8000 ms", "run to $after"), runs(late))
        assertEquals(settled, late.noteAt, 0f)
        // Never: the same hold, given up after 8 s, the held time taken for the note's.
        val never = FakeClock()
        assertEquals(listOf("run to $settled", "held at $settled for at most 8000 ms", "note taken at $settled", "run to $after"), runs(never))
        // Either way the pill's whole slide-in and stay, and the slide-off, play after the hold.
        assertEquals(ShotTimeline.TOTAL_MS + settled - ShotTimeline.PILL_AT, after, 0f)
    }

    @Test
    fun anOnTimeNoteNeverHoldsTheClock() {
        val settled = ShotTimeline.SETTLED_MS
        // Known from the start: one run, straight to the end.
        assertEquals(listOf("run to ${ShotTimeline.TOTAL_MS}"), runs(FakeClock(from = 0f)))
        // Coming while the clock runs, before the pill's time or after it: no hold, on to its end.
        assertEquals(listOf("run to $settled", "run to ${ShotTimeline.TOTAL_MS}"), runs(FakeClock(comesAt = 900f)))
        assertEquals(listOf("run to $settled", "run to ${ShotTimeline.end(1900f)}"), runs(FakeClock(comesAt = 1900f)))
    }

    @Test
    fun aShotNotSavedShowsThePillForThreeSeconds() {
        val failed = ShotErrorTimeline(psp, reduced = false)
        assertEquals(3000f, failed.duration, 0f)
        assertEquals(0f, failed.pillAlpha(0f), 0f)
        assertEquals(1f, failed.pillAlpha(180f), 0f)
        assertEquals(1f, failed.pillAlpha(2700f), 0f)
        assertEquals(0f, failed.pillX(1500f), 0f)
        assertEquals(1f, failed.pillScale(1500f), 0f)
    }

    @Test
    fun easingsAreCssCubicBeziers() {
        // Values from the curves solved apart, in double precision.
        assertEquals(0.802403f, CssEase(0.25f, 0.1f, 0.25f, 1f).at(0.5f), 1e-4f)
        assertEquals(0.378138f, CssEase.EASE_OUT.at(0.25f), 1e-4f)
        assertEquals(0.621862f, CssEase.EASE_IN.at(0.75f), 1e-4f)
        assertEquals(0.727549f, CssEase(0.3f, 0.9f, 0.4f, 1f).at(0.3f), 1e-4f)
        val pop = CssEase(0.2f, 0.9f, 0.3f, 1.2f)
        assertEquals(1.038528f, pop.at(0.5f), 1e-4f)
        // It overshoots, as CSS lets it, and comes back to its end.
        assertEquals(1.059223f, (0..1000).maxOf { pop.at(it / 1000f) }, 1e-4f)
        assertEquals(1f, pop.at(1f), 0f)
        for (p in listOf(0f, 0.1f, 0.37f, 0.5f, 0.9f, 1f)) {
            assertEquals(p, CssEase(0f, 0f, 1f, 1f).at(p), 1e-5f)
            assertEquals(1f, CssEase.EASE_IN_OUT.at(p) + CssEase.EASE_IN_OUT.at(1f - p), 1e-5f)
        }
        assertEquals(0f, CssEase.EASE_OUT.at(-0.5f), 0f)
        assertEquals(1f, CssEase.EASE_OUT.at(1.5f), 0f)
    }

    @Test
    fun theFrontPanelTipsTowardTheViewerAboutItsBottomEdge() {
        val w = 302f
        val h = 187f
        val p = 1440f
        val corners = FloatArray(8)
        flapCorners(0f, w, h, p, corners)
        assertEquals(listOf(0f, 0f, w, 0f, w, h, 0f, h), corners.toList())
        // Open: its top nearer, so wider and lower; the bottom edge stays put.
        flapCorners(-44f, w, h, p, corners)
        assertTrue(corners[0] < 0f && corners[2] > w)
        assertTrue(corners[1] > 0f && corners[1] < h)
        assertEquals(w / 2f, (corners[0] + corners[2]) / 2f, 1e-3f)
        assertEquals(listOf(w, h, 0f, h), corners.toList().subList(4, 8))
        // CSS's perspective(p) rotateX(-44deg): the top edge 0.719h above the bottom, 0.695h nearer the viewer.
        val k = p / (p - h * sin(Math.toRadians(44.0)).toFloat())
        assertEquals(h - h * cos(Math.toRadians(44.0)).toFloat() * k, corners[1], 1e-2f)
        // Snapped a little past closed: its top further away, so narrower.
        flapCorners(5f, w, h, p, corners)
        assertTrue(corners[0] > 0f && corners[2] < w)
    }
}
