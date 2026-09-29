package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * WCAG checks for the 16-bit RPG style, for every [styleVariants] palette. A window's fill fades from
 * a tint of the primary color at its top to a deeper (dark themes) or paler (light themes) shade at its
 * bottom, so its text has to hold AA at both ends.
 */
class RpgContrastTest {
    private fun windowEnds(v: StyleVariant) = listOf(
        "top" to rpgWindowTop(v.scheme.surfaceContainer, v.scheme.primary, v.isDark),
        "bottom" to rpgWindowBottom(v.scheme.surfaceContainer, v.isDark),
    )

    @Test
    fun textOnThePlateReachesAa() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            listOf("primary" to s.primary, "onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant).mapNotNull { (role, color) ->
                val ratio = wcagContrast(color.toArgb(), s.background.toArgb())
                if (ratio < 4.5) "${v.name}: $role on the plate: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun windowTextReachesAaAtBothEnds() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            val text = listOf(
                "onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary,
                "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
                "success" to v.success, "warning" to v.warning,
            )
            windowEnds(v).flatMap { (end, fill) ->
                text.mapNotNull { (role, color) ->
                    val ratio = wcagContrast(color.toArgb(), fill.toArgb())
                    if (ratio < 4.5) "${v.name}: $role at a window's $end: ${twoPlaces(ratio)}" else null
                }
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun framesStandOutFromTheBackdropAndTheirWindows() {
        // The band is what marks a window's edge (WCAG 1.4.11, 3:1).
        val failures = styleVariants.flatMap { v ->
            val band = rpgFrameBand(v.scheme, v.isDark).toArgb()
            val against = listOf("the backdrop" to v.scheme.background, "the lattice" to rpgLattice(v.scheme, v.isDark)) + windowEnds(v).map { (end, fill) -> "the window's $end" to fill }
            against.mapNotNull { (what, color) ->
                val ratio = wcagContrast(band, color.toArgb())
                if (ratio < 3.0) "${v.name}: the frame against $what: ${twoPlaces(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun gaugeFillStandsOutFromItsTrough() {
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.primary.toArgb(), rpgTrough(v.scheme, v.isDark).toArgb())
            if (ratio < 3.0) "${v.name}: a gauge's fill against its trough: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun topBarTitleStandsOutFromTheLattice() {
        // The top bar's title is large text over the backdrop until content scrolls under it.
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.onSurface.toArgb(), rpgLattice(v.scheme, v.isDark).toArgb())
            if (ratio < 3.0) "${v.name}: the top bar's title over the lattice: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }
}
