package com.phobos.emulator.ui.theme

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PixelSceneTest {
    private val colors = PixelSceneColors(
        skyTop = 0xFF0A0A1A.toInt(),
        skyBottom = 0xFF2A1A3A.toInt(),
        star = 0xFFFFFFFF.toInt(),
        planetLit = 0xFFB04020.toInt(),
        planetShade = 0xFF702010.toInt(),
        crater = 0xFF401008.toInt(),
        rim = 0xFFFF9060.toInt(),
        glow = 0xFFD06030.toInt(),
    )

    @Test
    fun sameInputsGiveTheSameScene() {
        assertArrayEquals(PixelScene.render(90, 160, colors), PixelScene.render(90, 160, colors))
        assertFalse(PixelScene.render(90, 160, colors).contentEquals(PixelScene.render(90, 160, colors, seed = 7)))
    }

    @Test
    fun skyStartsAtTheBackgroundAndHasStars() {
        val cols = 120
        val rows = 200
        val scene = PixelScene.render(cols, rows, colors)
        val topRow = scene.copyOfRange(0, cols)
        assertTrue(topRow.count { it == colors.skyTop } > cols * 0.8)
        val sky = scene.copyOfRange(0, cols * (rows * 0.6).toInt())
        assertTrue("stars in the sky", sky.count { brightness(it) > brightness(colors.skyBottom) + 60 } >= 10)
    }

    @Test
    fun planetFillsTheBottomAndNotTheTop() {
        val cols = 120
        val rows = 200
        val scene = PixelScene.render(cols, rows, colors)
        val planetColors = setOf(colors.planetLit, colors.planetShade, colors.crater)
        val bottomCenter = scene[(rows - 2) * cols + cols / 2]
        assertTrue("bottom center is planet: ${Integer.toHexString(bottomCenter)}", bottomCenter in planetColors || isMix(bottomCenter))
        val horizon = (rows * PixelScene.HORIZON).toInt()
        val aboveHorizon = scene.copyOfRange(0, cols * (horizon - 6))
        assertEquals(0, aboveHorizon.count { it == colors.planetLit || it == colors.planetShade })
    }

    @Test
    fun mixBlendsOpaqueColors() {
        assertEquals(0xFF808080.toInt(), PixelScene.mix(0xFF000000.toInt(), 0xFFFFFFFF.toInt(), 0.5f))
        assertEquals(colors.star, PixelScene.mix(colors.skyTop, colors.star, 1f))
        assertEquals(colors.skyTop, PixelScene.mix(colors.skyTop, colors.star, 0f))
    }

    // Crater rims are the planet mixed toward the rim color.
    private fun isMix(argb: Int) = brightness(argb) in brightness(colors.crater)..brightness(colors.rim)

    private fun brightness(argb: Int) = (argb shr 16 and 0xFF) + (argb shr 8 and 0xFF) + (argb and 0xFF)
}
