package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.cos
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
                    assertTrue("at $t: $values", values.all { it.isFinite() })
                    for (alpha in listOf(timeline.folderAlpha(t), timeline.pillAlpha(t), timeline.trayAlpha(t), failed.pillAlpha(t))) {
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
                assertEquals(0f, timeline.develop(t), 0f)
                assertEquals(0f, timeline.flap(t), 0f)
                assertEquals(1f, timeline.folderAlpha(t), 0f)
                assertEquals(1f, timeline.folderScaleX(t), 0f)
                assertEquals(1f, timeline.pillAlpha(t), 0f)
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
