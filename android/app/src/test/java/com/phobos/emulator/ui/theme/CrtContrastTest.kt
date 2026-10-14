package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.toArgb
import com.phobos.emulator.data.CrtBackdropScene
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * WCAG checks for the CRT terminal style, for every [styleVariants] palette, on the composited 8-bit
 * colors. Text drawn on the backdrop sits on a solid plate of the background, except the top bar's
 * title, which is large text (3:1) over the backdrop's phosphor bloom, reflection, refresh band, grain,
 * scanlines and vignette.
 */
class CrtContrastTest {
    private val black = 0xFF000000.toInt()
    private val white = 0xFFFFFFFF.toInt()

    /**
     * The most of the bloom's strength under the top bar (64 dp: on a landscape phone about a sixth of the
     * height, a fifth with a status bar above it edge to edge; less of a portrait one's), on landscape screens
     * from 4:3 to 21:9.
     */
    private val underTopBar: Float = listOf(1600 to 1200, 1920 to 1080, 2520 to 1080).maxOf { (w, h) ->
        (0 until h / 5 step 4).maxOf { y -> (0 until w step 16).maxOf { x -> CrtScreen.bloomAt(x, y, w, h) } }
    }

    /** [color] a level of grain lighter or darker. */
    private fun grain(color: Int, by: Int): Int {
        fun channel(shift: Int) = ((color shr shift and 0xFF) + by).coerceIn(0, 255) shl shift
        return (0xFF shl 24) or channel(16) or channel(8) or channel(0)
    }

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
            // Brightest: each phosphor's bloom as strong as it gets under the top bar, the glass's reflection, the
            // refresh band's peak and a level of grain, between scanlines and on one. Darkest: a scanline under
            // the vignette's full strength, a level of grain darker.
            val blooms = CrtBackdropScene.entries.flatMap { scene ->
                val phosphor = crtBloomColor(s, scene).toArgb()
                val glow = blend(phosphor, s.background.toArgb(), crtBloom(v.isDark) * underTopBar.toDouble())
                val lit = grain(blend(phosphor, blend(white, glow, CrtScreen.REFLECTION.toDouble()), paintAlpha(crtBand(v.isDark))), 1)
                listOf("the ${scene.label} bloom" to lit, "a scanline on the ${scene.label} bloom" to blend(black, lit, crtScanlineRows(v.isDark).max().toDouble()))
            }
            val vignette = blend(black, s.background.toArgb(), crtVignette(v.isDark).toDouble())
            val darkest = grain(blend(black, vignette, crtScanlineRows(v.isDark).max().toDouble()), -1)
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
