package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.MangaBackdropScene
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.hypot

class MangaPageTest {
    private val paper = 0xFFF4F1E8.toInt()
    private val ink = 0xFF8C8A84.toInt()

    /** How far toward the ink each pixel is, 0 (paper) to 1 (ink), from its green channel. */
    private fun coverage(pixels: IntArray) = pixels.map { ((paper shr 8 and 0xFF) - (it shr 8 and 0xFF)) / ((paper shr 8 and 0xFF) - (ink shr 8 and 0xFF)).toFloat() }

    @Test fun everySceneDrawsEveryPixelBetweenPaperAndInk() {
        for (scene in MangaBackdropScene.entries) for ((w, h) in listOf(1 to 1, 3 to 2, 64 to 36, 36 to 64, 480 to 270)) {
            val pixels = MangaPage.render(w, h, paper, ink, scene)
            assertEquals(w * h, pixels.size)
            assertTrue("$scene ${w}x$h", pixels.all { it ushr 24 == 0xFF })
            // Never denser than the ink, so the panels' ink lines keep their contrast over every mark (MangaContrastTest).
            for (shift in listOf(16, 8, 0)) for (pixel in pixels) {
                val c = pixel shr shift and 0xFF
                assertTrue("$scene ${w}x$h channel ${Integer.toHexString(pixel)}", c in minOf(paper shr shift and 0xFF, ink shr shift and 0xFF)..maxOf(paper shr shift and 0xFF, ink shr shift and 0xFF))
            }
        }
    }

    @Test fun theSamePageForTheSameInput() {
        for (scene in MangaBackdropScene.entries) {
            assertArrayEquals(MangaPage.render(320, 180, paper, ink, scene), MangaPage.render(320, 180, paper, ink, scene))
        }
    }

    @Test fun eachSceneLeavesItsPaperAndInksItsMarks() {
        val (w, h) = 640 to 360
        val tone = coverage(MangaPage.render(w, h, paper, ink, MangaBackdropScene.TONE))
        assertTrue("the tone's top is paper", (0 until w * h / 4).all { tone[it] < 0.01f })
        assertTrue("the tone is dense at the bottom", (w * (h - 20) until w * h).map { tone[it] }.average() > 0.45)

        val lines = coverage(MangaPage.render(w, h, paper, ink, MangaBackdropScene.SPEED_LINES))
        val cx = w * 0.5f
        val cy = h * 0.46f
        val clear = (0 until w * h).filter { hypot(it % w + 0.5f - cx, it / w + 0.5f - cy) < h * 0.28f }
        assertTrue("the focus lines leave the middle clear", clear.all { lines[it] < 0.01f })
        val edges = (0 until w * h).filter { it % w < 20 || it % w >= w - 20 }
        assertTrue("and crowd the edges", edges.map { lines[it] }.average() in 0.15..0.7)

        val impact = coverage(MangaPage.render(w, h, paper, ink, MangaBackdropScene.SPLASH))
        val core = (0 until w * h).filter { hypot(it % w + 0.5f - w * 0.56f, it / w + 0.5f - h * 0.46f) < h * 0.14f }
        assertTrue("the burst's heart is paper", core.all { impact[it] < 0.01f })
        val corner = (0 until w * h).filter { it % w < 60 && it / w < 60 }
        assertTrue("tone outside it", corner.map { impact[it] }.average() > 0.2)
    }
}
