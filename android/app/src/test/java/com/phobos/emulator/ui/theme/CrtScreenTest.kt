package com.phobos.emulator.ui.theme

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CrtScreenTest {
    private val background = 0xFF1E2127.toInt()
    private val phosphor = 0xFF5CE68E.toInt()

    private fun luminance(argb: Int) = 0.2126 * (argb shr 16 and 0xFF) + 0.7152 * (argb shr 8 and 0xFF) + 0.0722 * (argb and 0xFF)

    @Test fun everyPixelIsDrawnAtAnySizeAndTheSameEachTime() {
        for ((w, h) in listOf(1 to 1, 2 to 3, 64 to 36, 36 to 64, 640 to 360)) for (dark in listOf(true, false)) {
            val pixels = CrtScreen.render(w, h, background, phosphor, dark)
            assertTrue("${w}x$h", pixels.size == w * h && pixels.all { it ushr 24 == 0xFF })
        }
        assertArrayEquals(CrtScreen.render(320, 180, background, phosphor, true), CrtScreen.render(320, 180, background, phosphor, true))
    }

    @Test fun underTheTopBarNothingIsBrighterThanTheContrastTestAssumes() {
        // CrtContrastTest takes the bloom there as [CrtScreen.bloomAt] gives it, with the reflection and a level of grain.
        for ((w, h) in listOf(640 to 360, 640 to 480, 360 to 640)) {
            val pixels = CrtScreen.render(w, h, background, phosphor, true)
            val strongest = (0 until h / 5).maxOf { y -> (0 until w).maxOf { x -> CrtScreen.bloomAt(x, y, w, h) } }
            val ceiling = luminance(PixelKit.mix(PixelKit.mix(background, phosphor, crtBloom(true) * strongest), 0xFFFFFFFF.toInt(), CrtScreen.REFLECTION)) + 1.0
            assertTrue("${w}x$h", (0 until w * (h / 5)).all { luminance(pixels[it]) <= ceiling })
        }
    }

    @Test fun theScreenGlowsBelowTheMiddleAndDarkensIntoItsCorners() {
        val (w, h) = 640 to 360
        val pixels = CrtScreen.render(w, h, background, phosphor, true)
        fun around(x: Int, y: Int) = (y - 2..y + 2).flatMap { yy -> (x - 2..x + 2).map { luminance(pixels[yy * w + it]) } }.average()
        assertTrue("the middle glows", around(w / 2, (h * 0.6f).toInt()) > around(w / 2, 6) + 2)
        assertTrue("the corners are dark", around(4, 4) < around(w / 2, h / 2) - 4)
        // The scanlines are drawn over the picture at the screen's pixels, so it has none of its own to be scaled.
        val rows = (0 until h).map { y -> (w / 4 until w * 3 / 4).map { luminance(pixels[y * w + it]) }.average() }
        assertTrue("rows stay smooth", (h / 4 until h / 2).all { kotlin.math.abs(rows[it] - rows[it - 1]) < 1.5 })
    }

    @Test fun theBootTextIsBurnedInAtTheBottomLeft() {
        val (w, h) = 640 to 360
        val pixels = CrtScreen.render(w, h, background, phosphor, true)
        val text = (h * 3 / 4 until h * 92 / 100).flatMap { y -> (w * 6 / 100 until w * 30 / 100).map { pixels[y * w + it] } }
        val greenest = text.maxOf { (it shr 8 and 0xFF) - (it shr 16 and 0xFF) }
        val elsewhere = (h * 3 / 4 until h * 92 / 100).flatMap { y -> (w * 70 / 100 until w * 94 / 100).map { pixels[y * w + it] } }.maxOf { (it shr 8 and 0xFF) - (it shr 16 and 0xFF) }
        assertTrue("the text's strokes $greenest, the same rows on the right $elsewhere", greenest > elsewhere + 4)
    }
}
