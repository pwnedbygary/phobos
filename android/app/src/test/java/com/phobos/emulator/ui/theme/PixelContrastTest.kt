package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * WCAG checks for pixel art effects, on the composited 8-bit colors the screen shows, for every
 * [styleVariants] palette. Text drawn straight on the pixel backdrop sits on the pixel plate and is
 * checked over every color a rendered scene contains; pixel panels and the dock are opaque.
 */
class PixelContrastTest {
    @Test
    fun backdropTextReachesAaOnThePixelPlate() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            val scene = PixelScene.render(60, 110, pixelSceneColors(s, v.isDark)).toSet()
            val text = listOf("onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
            scene.flatMap { backdrop ->
                val plate = blend(s.background.toArgb(), backdrop, paintAlpha(PIXEL_PLATE_ALPHA))
                text.mapNotNull { (role, color) ->
                    val ratio = wcagContrast(color.toArgb(), plate)
                    if (ratio < 4.5) "${v.name}: $role on the plate over ${hexOf(backdrop)}: ${twoPlaces(ratio)}" else null
                }
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun panelAndDockTextReachesAa() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            val panel = s.surfaceContainer.toArgb()
            val text = listOf(
                "onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary,
                "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
                "success" to v.success, "warning" to v.warning,
            )
            text.mapNotNull { (role, color) ->
                val ratio = wcagContrast(color.toArgb(), panel)
                if (ratio < 4.5) "${v.name}: $role on a pixel panel: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun dockSelectionKeepsItsIconVisible() {
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.onPrimaryContainer.toArgb(), v.scheme.primaryContainer.toArgb())
            if (ratio < 3.0) "${v.name}: selected dock icon: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }
}
