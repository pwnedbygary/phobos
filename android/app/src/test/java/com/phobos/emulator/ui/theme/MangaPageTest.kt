package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.MangaBackdropScene
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.hypot

class MangaPageTest {
    private val paper = 0xFFF4F1E8.toInt()
    private val ink = 0xFF8C8A84.toInt()

    /** A dark theme's: the ink lighter than the paper. */
    private val darkPaper = 0xFF282828.toInt()
    private val darkInk = 0xFF5E5A50.toInt()

    private val font = File("src/main/res/font/bangers.ttf").readBytes()
    private val lettering = Lettering(TrueType(font))

    /** How far toward the ink each pixel is, 0 (paper) to 1 (ink), from its green channel. */
    private fun coverage(pixels: IntArray, paper: Int = this.paper, ink: Int = this.ink) =
        pixels.map { ((paper shr 8 and 0xFF) - (it shr 8 and 0xFF)) / ((paper shr 8 and 0xFF) - (ink shr 8 and 0xFF)).toFloat() }

    private fun render(w: Int, h: Int, scene: MangaBackdropScene, paper: Int = this.paper, ink: Int = this.ink, lettering: Lettering? = this.lettering) =
        MangaPage.render(w, h, paper, ink, scene, lettering)

    @Test fun everySceneDrawsEveryPixelBetweenPaperAndInk() {
        for ((p, k) in listOf(paper to ink, darkPaper to darkInk)) for (scene in MangaBackdropScene.entries) {
            // Down to a pixel, and pages far narrower than they're tall.
            for ((w, h) in listOf(1 to 1, 3 to 2, 1 to 30, 2 to 3000, 30 to 4000, 64 to 36, 36 to 64, 480 to 270, 270 to 480)) {
                val pixels = render(w, h, scene, p, k)
                assertEquals(w * h, pixels.size)
                assertTrue("$scene ${w}x$h", pixels.all { it ushr 24 == 0xFF })
                // PixelKit.mix clamps the coverage, so a pixel outside the paper and the ink is a NaN, which it draws black.
                for (shift in listOf(16, 8, 0)) for (pixel in pixels) {
                    val c = pixel shr shift and 0xFF
                    assertTrue("$scene ${w}x$h channel ${Integer.toHexString(pixel)}", c in minOf(p shr shift and 0xFF, k shr shift and 0xFF)..maxOf(p shr shift and 0xFF, k shr shift and 0xFF))
                }
            }
        }
    }

    @Test fun theSamePageForTheSameInput() {
        for (scene in MangaBackdropScene.entries) {
            // The lettering read from the font again draws the same.
            assertArrayEquals("$scene", render(320, 180, scene), render(320, 180, scene, lettering = Lettering(TrueType(font.copyOf()))))
            assertArrayEquals("$scene portrait", render(180, 320, scene), render(180, 320, scene))
        }
    }

    @Test fun everySceneStaysAFaintBackdrop() {
        // On average well short of the ink, so the pages over it stay the thing to read: wide, square and tall.
        for ((p, k) in listOf(paper to ink, darkPaper to darkInk)) for (scene in MangaBackdropScene.entries) {
            for ((w, h) in listOf(640 to 360, 360 to 640, 480 to 480, 640 to 480, 480 to 640)) {
                val mean = coverage(render(w, h, scene, p, k), p, k).average()
                assertTrue("$scene ${w}x$h on ${Integer.toHexString(p)}: $mean", mean in 0.05..0.4)
            }
        }
    }

    @Test fun eachSceneLeavesItsPaperAndInksItsMarks() {
        val (w, h) = 640 to 360
        val tone = coverage(render(w, h, MangaBackdropScene.TONE))
        assertTrue("the tone's top is paper", (0 until w * h / 4).all { tone[it] < 0.01f })
        assertTrue("the tone is dense at the bottom", (w * (h - 20) until w * h).map { tone[it] }.average() > 0.45)

        val lines = coverage(render(w, h, MangaBackdropScene.SPEED_LINES))
        val cx = w * 0.5f
        val cy = h * 0.46f
        val clear = (0 until w * h).filter { hypot(it % w + 0.5f - cx, it / w + 0.5f - cy) < h * 0.28f }
        assertTrue("the focus lines leave the middle clear", clear.all { lines[it] < 0.01f })
        val edges = (0 until w * h).filter { it % w < 20 || it % w >= w - 20 }
        assertTrue("and crowd the edges", edges.map { lines[it] }.average() in 0.15..0.7)
    }

    @Test fun theCoverLettersItsMastheadOverTheMoon() {
        val (w, h) = 640 to 360
        val lettered = coverage(render(w, h, MangaBackdropScene.COVER))
        val bare = coverage(render(w, h, MangaBackdropScene.COVER, lettering = null))
        // The masthead, in the top fifth's middle, where the Library's cards leave the page showing.
        val masthead = (0 until w * h).filter { it / w in (h * 0.03f).toInt() until (h * 0.2f).toInt() && it % w in (w * 0.3f).toInt() until (w * 0.7f).toInt() }
        val inked = masthead.map { lettered[it] }.average() - masthead.map { bare[it] }.average()
        assertTrue("the masthead is lettered: $inked more ink", inked > 0.15)
        val moon = (0 until w * h).filter { hypot(it % w + 0.5f - w * 0.5f, it / w + 0.5f - h * 0.6f) < h * 0.2f }
        assertTrue("the moon is inked and toned", moon.map { bare[it] }.average() in 0.15..0.85)
        assertTrue("the words are all the lettering changes", (0 until w * h).count { lettered[it] != bare[it] } < w * h / 2)
    }

    @Test fun truckKunsHeadlightsBlazeOnLightThemesAndDark() {
        for ((w, h) in listOf(640 to 360, 360 to 640)) {
            val frame = truckFrame(w, h)
            val at = FloatArray(2)
            for ((p, k) in listOf(paper to ink, darkPaper to darkInk)) {
                val page = coverage(render(w, h, MangaBackdropScene.TRUCK, p, k), p, k)
                for (side in listOf(-1f, 1f)) {
                    frame.toPage(side * HEADLIGHT_X, HEADLIGHT_Y, at)
                    val x = at[0].toInt()
                    val y = at[1].toInt()
                    val lamp = (-1..1).flatMap { dy -> (-1..1).map { dx -> page[(y + dy) * w + x + dx] } }.average()
                    // Light is the paper on a light theme, the ink on a dark one.
                    if (k == ink) assertTrue("${w}x$h light theme, headlight $side: $lamp", lamp < 0.05)
                    else assertTrue("${w}x$h dark theme, headlight $side: $lamp", lamp > 0.95)
                }
            }
        }
    }

    @Test fun theBarcodeReadsBackAsItsDigits() {
        assertEquals("a published EAN-13's check digit", 1, ean13Check("400638133393"))
        // Every leading digit: each sets the left half's parities its own way.
        for (first in 0..9) for (rest in listOf("91020261042", "00638133393", "12345678901")) {
            val digits = "$first$rest"
            val code = digits + ean13Check(digits)
            assertEquals(code, readEan13(ean13(code)))
        }
    }

    /** Reads EAN-13 bars by the published patterns: the guards, then each digit's seven modules. */
    private fun readEan13(modules: BooleanArray): String {
        assertEquals(95, modules.size)
        fun bits(from: Int, count: Int) = (from until from + count).joinToString("") { if (modules[it]) "1" else "0" }
        assertEquals("101", bits(0, 3))
        assertEquals("01010", bits(45, 5))
        assertEquals("101", bits(92, 3))
        val odd = listOf("0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011")
        val right = odd.map { pattern -> pattern.map { if (it == '0') '1' else '0' }.joinToString("") }
        val even = right.map { it.reversed() }
        val parities = listOf("OOOOOO", "OOEOEE", "OOEEOE", "OOEEEO", "OEOOEE", "OEEOOE", "OEEEOO", "OEOEOE", "OEOEEO", "OEEOEO")
        var parity = ""
        var left = ""
        for (k in 0 until 6) {
            val pattern = bits(3 + 7 * k, 7)
            if (pattern in odd) {
                parity += "O"
                left += odd.indexOf(pattern)
            } else {
                parity += "E"
                left += even.indexOf(pattern).also { assertTrue(pattern, it >= 0) }
            }
        }
        val first = parities.indexOf(parity).also { assertTrue(parity, it >= 0) }
        val rightDigits = (0 until 6).joinToString("") { k -> right.indexOf(bits(50 + 7 * k, 7)).also { assertTrue(it >= 0) }.toString() }
        return "$first$left$rightDigits"
    }

    @Test fun aLetterIsInkedAndItsCounterAndItsSurroundsAreNot() {
        val mask = lettering.mask(lettering.line("O", 60f, 10f, 70f), 4)!!
        val inked = mask.alpha.indices.filter { mask.alpha[it] > 0f }
        val left = inked.minOf { it % mask.width }
        val right = inked.maxOf { it % mask.width }
        val top = inked.minOf { it / mask.width }
        val bottom = inked.maxOf { it / mask.width }
        val middle = (top + bottom) / 2 * mask.width
        assertEquals("the O's counter is clear", 0f, mask.alpha[middle + (left + right) / 2])
        assertTrue("its sides are solid", (left..right).count { mask.alpha[middle + it] >= 0.999f } >= 6)
        // Every row's crossings cancel, across a whole word: its margin, which no letter reaches, is bare paper.
        val word = lettering.mask(lettering.line("PHOBOS", 48f, 10f, 60f, slant = 0.1f), 4)!!
        for (y in 0 until word.height) for (x in word.width - 4 until word.width) assertEquals("row $y", 0f, word.alpha[y * word.width + x])
    }

    @Test fun aLettersInkGrowsWithTheSquareOfItsSize() {
        fun ink(size: Float) = lettering.mask(lettering.line("B", size, 4f, size), 2)!!.alpha.sum()
        assertEquals(16.0, (ink(160f) / ink(40f)).toDouble(), 0.4)
    }

    @Test fun aCompositeGlyphIsItsPartsAndAMissingOneTheFontsBox() {
        val font = lettering.font
        val e = font.contours(font.glyph('E'.code))
        val accented = font.contours(font.glyph('É'.code))
        // É is composed of E, in place, and an acute standing above it.
        assertTrue(accented.size > e.size)
        assertTrue("E's outlines, unmoved", e.all { c -> accented.any { it.x.contentEquals(c.x) && it.y.contentEquals(c.y) } })
        assertTrue("the accent above the capital", accented.maxOf { it.y.max() } > e.maxOf { it.y.max() })
        // A character the font lacks is its missing glyph: still lettered, as the font's box.
        assertEquals(0, font.glyph(0x4E00))
        assertTrue(font.contours(0).isNotEmpty())
        assertTrue(lettering.width("\u4E00", 40f) > 0f)
        assertNotNull(lettering.mask(lettering.line("\u4E00", 40f, 4f, 40f)))
    }

    @Test fun aFontThatCantBeReadLeavesTheWordsOut() {
        assertNotNull(letteringOf(font))
        assertNull("a truncated file", letteringOf(font.copyOf(font.size / 3)))
        assertNull("no font at all", letteringOf(ByteArray(64)))
        for (scene in listOf(MangaBackdropScene.COVER, MangaBackdropScene.TRUCK)) assertEquals(320 * 180, render(320, 180, scene, lettering = null).size)
    }

    @Test fun distancesFromTheInkAreEuclidean() {
        val side = 21
        val alpha = FloatArray(side * side).also { it[10 * side + 10] = 1f }
        val distances = Mask(0, 0, side, side, alpha).distances()
        for (y in 0 until side) for (x in 0 until side) assertEquals(hypot(x - 10f, y - 10f), distances[y * side + x], 1e-4f)
    }
}
