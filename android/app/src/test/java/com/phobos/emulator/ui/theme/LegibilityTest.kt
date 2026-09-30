package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/**
 * The outline drawn around text and icons over backgrounds the theme doesn't control, and the Library
 * tiles' tinted fill, for every palette of every registered theme plus Material You schemes.
 */
class LegibilityTest {

    @Test
    fun outlinesContrastWithWhatTheySurround() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            listOf("onBackground" to s.onBackground, "onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
                .mapNotNull { (role, color) ->
                    val outline = legibilityOutline(color, s.primary)
                    // Mid-grey text can't reach the target with anything; then the outline has to be the best there is.
                    val needed = min(OUTLINE_CONTRAST, max(contrastRatio(color, Color.Black), contrastRatio(color, Color.White)))
                    val ratio = contrastRatio(outline, color)
                    if (ratio >= needed - ROUNDING) null
                    else "${v.name}: $role ${hexOf(color.toArgb())} has an outline ${hexOf(outline.toArgb())} at " +
                        "${twoPlaces(ratio.toDouble())}:1, needs ${twoPlaces(needed.toDouble())}:1"
                }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun outlinesStayInTheThemesColorsWhereTheyCan() {
        val failures = styleVariants.mapNotNull { v ->
            val s = v.scheme
            val outline = legibilityOutline(s.onSurface, s.primary)
            val plain = if (contrastRatio(s.onSurface, Color.Black) >= contrastRatio(s.onSurface, Color.White)) Color.Black else Color.White
            // Black or white only when nothing short of them reaches the target.
            val shortOf = lerp(s.primary, plain, 0.98f)
            if (outline != plain || contrastRatio(shortOf, s.onSurface) < OUTLINE_CONTRAST) null
            else "${v.name}: the onSurface outline is plain ${hexOf(outline.toArgb())} although ${hexOf(shortOf.toArgb())} reaches the target"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun libraryTileNamesReachAaOnSolidTiles() {
        val failures = styleVariants.flatMap { v ->
            val s = v.scheme
            val tile = libraryTileFill(s, v.isDark)
            listOf(
                "tile" to tile,
                "RPG window's top" to rpgWindowTop(tile, s.primary, v.isDark),
                "RPG window's bottom" to rpgWindowBottom(tile, v.isDark),
            ).mapNotNull { (where, fill) ->
                val ratio = contrastRatio(s.onSurface, fill)
                if (ratio >= TEXT_AA) null
                else "${v.name}: onSurface on the $where ${hexOf(fill.toArgb())} is ${twoPlaces(ratio.toDouble())}:1, needs $TEXT_AA:1"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun libraryTilesTakeTheThemesPrimary() {
        val failures = styleVariants.mapNotNull { v ->
            val s = v.scheme
            val tile = Oklab.of(libraryTileFill(s, v.isDark))
            val card = Oklab.of(s.surfaceContainer)
            val primary = Oklab.of(s.primary)
            // Distance to the primary in Oklab's color plane, lightness aside.
            fun distance(c: Oklab) = hypot(c.a - primary.a, c.b - primary.b)
            val where = "${v.name}: the Library tile ${hexOf(tile.toColor().toArgb())}"
            when {
                primary.chroma < GREY_CHROMA -> null
                // A grey card is the case the tint is for.
                card.chroma < GREY_CHROMA && distance(tile) >= distance(card) -> "$where is no closer to the primary than the grey card ${hexOf(s.surfaceContainer.toArgb())}"
                // A card already in the primary's hue (Material You's) may be as colorful as its lightness allows.
                distance(tile) > distance(card) + HUE_ROUNDING -> "$where is further from the primary than the card ${hexOf(s.surfaceContainer.toArgb())}"
                else -> null
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    private companion object {
        const val TEXT_AA = 4.5f
        const val ROUNDING = 0.01f
        const val GREY_CHROMA = 0.02f
        const val HUE_ROUNDING = 0.005f
    }
}
