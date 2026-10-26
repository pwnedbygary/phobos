package com.phobos.emulator.ui.theme

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class NightCityTest {
    private fun colors(id: String): CityColors {
        val theme = ThemeRegistry.all.first { it.id == id }
        return CityColors(theme.colors.scheme, theme.isDark)
    }

    private val synthwave = colors("synthwave_84")

    private fun luminance(argb: Int) = (0.2126 * (argb shr 16 and 0xFF) + 0.7152 * (argb shr 8 and 0xFF) + 0.0722 * (argb and 0xFF)) / 255

    private fun rows(pixels: IntArray, width: Int, rows: IntRange) = rows.flatMap { y -> (0 until width).map { pixels[y * width + it] } }

    @Test fun theSameSizeAndColorsGiveTheSamePicture() {
        assertArrayEquals(NightCity.render(320, 180, synthwave), NightCity.render(320, 180, synthwave))
        assertFalse(NightCity.render(320, 180, synthwave).contentEquals(NightCity.render(320, 180, synthwave, seed = 7)))
    }

    @Test fun everyPixelIsDrawnAtAnySize() {
        for ((w, h) in listOf(1 to 1, 2 to 3, 7 to 5, 64 to 36, 36 to 64, 640 to 360)) {
            val pixels = NightCity.render(w, h, synthwave)
            assertTrue("${w}x$h", pixels.size == w * h && pixels.all { it ushr 24 == 0xFF })
        }
    }

    @Test fun theSkyGlowsTowardTheCityAndTheWaterMirrorsItDarker() {
        val (w, h) = 640 to 360
        val pixels = NightCity.render(w, h, synthwave)
        val water = (h * NightCity.WATERLINE).toInt()
        val top = rows(pixels, w, 0 until 20).map(::luminance).average()
        val skyline = rows(pixels, w, water - 30 until water - 2).map(::luminance).average()
        val reflection = rows(pixels, w, water + 3 until water + 31).map(::luminance).average()
        val bottom = rows(pixels, w, h - 20 until h).map(::luminance).average()
        assertTrue("the sky at the top $top, over the skyline $skyline", skyline > top * 1.5)
        assertTrue("the city $skyline, its reflection $reflection", reflection < skyline && reflection > top * 0.5)
        assertTrue("the reflection fades: $reflection, then $bottom", bottom < reflection)
        // The neon: bright, colorful pixels in the city.
        val neon = rows(pixels, w, water / 3 until water).count { luminance(it) > 0.5 && saturation(it) > 0.35 }
        assertTrue("$neon neon pixels", neon > w * h / 2000)
    }

    @Test fun lightThemesGetThePaleCity() {
        val dawn = colors("rose_pine_dawn")
        val background = dawn.background
        val reach = (1 - dawn.lift) * 255 + 1
        for (pixel in NightCity.render(320, 180, dawn)) {
            for (shift in listOf(16, 8, 0)) assertTrue(abs((pixel shr shift and 0xFF) - (background shr shift and 0xFF)) <= reach)
        }
    }

    private fun saturation(argb: Int): Double {
        val channels = listOf(argb shr 16 and 0xFF, argb shr 8 and 0xFF, argb and 0xFF)
        val max = channels.max()
        return if (max == 0) 0.0 else (max - channels.min()) / max.toDouble()
    }
}
