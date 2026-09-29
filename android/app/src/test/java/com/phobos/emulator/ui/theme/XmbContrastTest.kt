package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Checks for the XMB waves style beyond the glass panels' (GlassContrastTest covers those over the
 * waves), for every [styleVariants] palette.
 */
class XmbContrastTest {
    @Test
    fun topBarTitleStandsOutFromTheGradient() {
        // The top bar's title is large text over the top of the gradient, above the band the ribbons reach.
        val failures = styleVariants.mapNotNull { v ->
            val waves = WaveColors(v.scheme, v.isDark)
            val ratio = wcagContrast(v.scheme.onSurface.toArgb(), waves.top.toArgb())
            if (ratio < 3.0) "${v.name}: the top bar's title over the gradient: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun ribbonsStayClearOfTheTopBar() {
        // The top bar is 64 dp tall; on a landscape phone that is about a sixth of the height.
        assertTrue("the ribbons reach ${WaveColors.BAND.start} of the height", WaveColors.BAND.start > 0.2f)
    }
}
