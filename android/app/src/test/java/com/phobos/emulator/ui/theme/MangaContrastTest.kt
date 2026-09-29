package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * WCAG checks for the manga ink style, for every [styleVariants] palette. Panels are the plain panel
 * color inside an ink line, and text drawn on the backdrop sits in a caption box of the background.
 */
class MangaContrastTest {
    @Test
    fun textInACaptionBoxReachesAa() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            listOf("primary" to s.primary, "onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant).mapNotNull { (role, color) ->
                val ratio = wcagContrast(color.toArgb(), s.background.toArgb())
                if (ratio < 4.5) "${v.name}: $role in a caption box: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun panelTextReachesAa() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            val text = listOf(
                "onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary,
                "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
                "success" to v.success, "warning" to v.warning,
            )
            text.mapNotNull { (role, color) ->
                val ratio = wcagContrast(color.toArgb(), s.surfaceContainer.toArgb())
                if (ratio < 4.5) "${v.name}: $role on a panel: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun inkLinesStandOut() {
        // Ink marks the panels, caption boxes and controls (WCAG 1.4.11, 3:1).
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            listOf("the backdrop" to s.background, "a panel" to s.surfaceContainer, "the densest tone" to mangaTone(s, v.isDark)).mapNotNull { (what, color) ->
                val ratio = wcagContrast(s.onSurface.toArgb(), color.toArgb())
                if (ratio < 3.0) "${v.name}: ink against $what: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun labelsOnThePrimaryColorReachAa() {
        // The switch's "ON!", the filled buttons and the selected dock tab's icon over its burst.
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.onPrimary.toArgb(), v.scheme.primary.toArgb())
            if (ratio < 4.5) "${v.name}: onPrimary on primary: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }
}
