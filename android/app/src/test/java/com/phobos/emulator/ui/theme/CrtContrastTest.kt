package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import com.phobos.emulator.data.CrtBackdropScene
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * WCAG checks for the CRT terminal style, for every [styleVariants] palette, on the composited 8-bit
 * colors. Text drawn on the backdrop sits on a solid plate of the background, except the top bar's
 * title, which is large text (3:1) over the backdrop's phosphor bloom, scanlines and vignette.
 */
class CrtContrastTest {
    private val black = 0xFF000000.toInt()

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
    fun topBarTitleStandsOutFromTheBackdrop() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            // Brightest: each phosphor's bloom center, between scanlines and on one. Darkest: a scanline under the vignette's full strength.
            val blooms = CrtBackdropScene.entries.flatMap { scene ->
                val bloom = lerp(s.background, crtBloomColor(s, scene), crtBloom(v.isDark)).toArgb()
                listOf("the ${scene.label} bloom" to bloom, "a scanline on the ${scene.label} bloom" to blend(black, bloom, paintAlpha(crtScanline(v.isDark))))
            }
            val vignette = blend(black, s.background.toArgb(), paintAlpha(crtVignette(v.isDark)))
            val darkest = blend(black, vignette, paintAlpha(crtScanline(v.isDark)))
            (blooms + ("a dark scanline" to darkest)).mapNotNull { (where, backdrop) ->
                val ratio = wcagContrast(s.primary.toArgb(), backdrop)
                if (ratio < 3.0) "${v.name}: the top bar's title over $where: ${twoPlaces(ratio)}" else null
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
    fun libraryTileNamesReachAa() {
        // The Library's CRT tiles write the system's name in the primary color on the tile.
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.primary.toArgb(), libraryTileFill(v.scheme, v.isDark).toArgb())
            if (ratio < 4.5) "${v.name}: primary on a Library tile: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun inverseVideoKeepsItsLabels() {
        // The switch's ON, the filled buttons and the selected dock tab's icon (which needs only 3:1).
        val failures = styleVariants.mapNotNull { v ->
            val ratio = wcagContrast(v.scheme.onPrimary.toArgb(), v.scheme.primary.toArgb())
            if (ratio < 4.5) "${v.name}: onPrimary on primary: ${twoPlaces(ratio)}" else null
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }
}
