package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.toArgb
import com.phobos.emulator.data.XmbBackdropScene
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
        val failures = styleVariants.flatMap { v ->
            XmbBackdropScene.entries.mapNotNull { scene ->
                val waves = WaveColors(v.scheme, v.isDark, scene)
                // Deep's pool of light reaches the top on wide screens: taken at its middle's strength.
                val under = if (scene == XmbBackdropScene.DEEP) waves.pool.compositeOver(waves.top) else waves.top
                val ratio = wcagContrast(v.scheme.onSurface.toArgb(), under.toArgb())
                if (ratio < 3.0) "${v.name}: the top bar's title over the ${scene.label} gradient: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun ribbonsStayClearOfTheTopBar() {
        // The top bar is 64 dp tall; on a landscape phone that is about a sixth of the height.
        assertTrue("the ribbons reach ${WaveColors.BAND.start} of the height", WaveColors.BAND.start > 0.2f)
        assertTrue("Deep's sheets and sparkles reach ${WaveColors.DEEP_BAND.start} of the height", WaveColors.DEEP_BAND.start > 0.2f)
    }
}
