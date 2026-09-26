package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

/**
 * WCAG 2 contrast checks for every palette of every registered theme, computed here from the 8-bit
 * ARGB values the screen actually shows. Failures name the theme and the color pair.
 */
class ThemeContrastTest {

    private val variants: List<Pair<String, ThemeColors>> = ThemeRegistry.all.flatMap { theme ->
        listOfNotNull(
            theme.dark?.let { "${theme.name} (dark)" to theme.colors(dark = true) },
            theme.light?.let { "${theme.name} (light)" to theme.colors(dark = false) },
        )
    }

    @Test
    fun onColorsReachAaOnTheirContainers() = assertAll { s, _ ->
        listOf(
            Check("onPrimary on primary", s.onPrimary, s.primary),
            Check("onPrimaryContainer on primaryContainer", s.onPrimaryContainer, s.primaryContainer),
            Check("onSecondary on secondary", s.onSecondary, s.secondary),
            Check("onSecondaryContainer on secondaryContainer", s.onSecondaryContainer, s.secondaryContainer),
            Check("onTertiary on tertiary", s.onTertiary, s.tertiary),
            Check("onTertiaryContainer on tertiaryContainer", s.onTertiaryContainer, s.tertiaryContainer),
            Check("onError on error", s.onError, s.error),
            Check("onErrorContainer on errorContainer", s.onErrorContainer, s.errorContainer),
            Check("onBackground on background", s.onBackground, s.background),
            Check("onSurface on surface", s.onSurface, s.surface),
            Check("onSurfaceVariant on surfaceVariant", s.onSurfaceVariant, s.surfaceVariant),
            Check("inverseOnSurface on inverseSurface", s.inverseOnSurface, s.inverseSurface),
            Check("inversePrimary on inverseSurface", s.inversePrimary, s.inverseSurface),
        )
    }

    @Test
    fun onSurfaceReachesAaOnEverySurfaceLevel() = assertAll { s, _ ->
        surfaceLevels(s).map { (surfaceName, surface) -> Check("onSurface on $surfaceName", s.onSurface, surface) }
    }

    @Test
    fun secondaryTextReachesAaOnScreensCardsAndDialogs() = assertAll { s, _ ->
        listOf(
            "background" to s.background, "surfaceContainerLowest" to s.surfaceContainerLowest,
            "surfaceContainerLow" to s.surfaceContainerLow, "surfaceContainer" to s.surfaceContainer,
            "surfaceContainerHigh" to s.surfaceContainerHigh,
        ).map { (surfaceName, surface) -> Check("onSurfaceVariant on $surfaceName", s.onSurfaceVariant, surface) }
    }

    @Test
    fun accentTextReachesAaOnScreensAndCards() = assertAll { s, colors ->
        val accents = listOf(
            "primary" to s.primary, "secondary" to s.secondary, "tertiary" to s.tertiary,
            "error" to s.error, "success" to colors.success, "warning" to colors.warning,
        )
        val surfaces = listOf(
            "background" to s.background, "surface" to s.surface,
            "surfaceContainerLow" to s.surfaceContainerLow, "surfaceContainer" to s.surfaceContainer,
        )
        accents.flatMap { (accentName, accent) ->
            surfaces.map { (surfaceName, surface) -> Check("$accentName on $surfaceName", accent, surface) }
        } + Check("primary on surfaceContainerHigh (dialog actions)", s.primary, s.surfaceContainerHigh)
    }

    @Test
    fun outlinesReachNonTextContrast() = assertAll { s, _ ->
        listOf(
            Check("outline on background", s.outline, s.background, minimum = 3.0),
            Check("outline on surfaceContainer", s.outline, s.surfaceContainer, minimum = 3.0),
        )
    }

    @Test
    fun everyRoleIsOpaque() {
        val failures = variants.flatMap { (name, colors) ->
            roles(colors.scheme).filter { (_, color) -> color.toArgb() ushr 24 != 0xFF }.map { (role, _) -> "$name: $role is translucent" }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    private class Check(val label: String, val foreground: Color, val background: Color, val minimum: Double = 4.5)

    private fun assertAll(checks: (ColorScheme, ThemeColors) -> List<Check>) {
        val failures = variants.flatMap { (name, colors) ->
            checks(colors.scheme, colors).mapNotNull { check ->
                val ratio = contrast(check.foreground.toArgb(), check.background.toArgb())
                if (ratio >= check.minimum) null
                else "$name: ${check.label} is ${String.format(Locale.ROOT, "%.2f", ratio)}:1, needs ${check.minimum}:1"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    private fun surfaceLevels(s: ColorScheme) = listOf(
        "background" to s.background,
        "surfaceContainerLowest" to s.surfaceContainerLowest,
        "surfaceContainerLow" to s.surfaceContainerLow,
        "surfaceContainer" to s.surfaceContainer,
        "surfaceContainerHigh" to s.surfaceContainerHigh,
        "surfaceContainerHighest" to s.surfaceContainerHighest,
        "surfaceDim" to s.surfaceDim,
        "surfaceBright" to s.surfaceBright,
    )

    private fun roles(s: ColorScheme) = listOf(
        "primary" to s.primary, "onPrimary" to s.onPrimary, "primaryContainer" to s.primaryContainer,
        "onPrimaryContainer" to s.onPrimaryContainer, "inversePrimary" to s.inversePrimary,
        "secondary" to s.secondary, "onSecondary" to s.onSecondary, "secondaryContainer" to s.secondaryContainer,
        "onSecondaryContainer" to s.onSecondaryContainer, "tertiary" to s.tertiary, "onTertiary" to s.onTertiary,
        "tertiaryContainer" to s.tertiaryContainer, "onTertiaryContainer" to s.onTertiaryContainer,
        "background" to s.background, "onBackground" to s.onBackground, "surface" to s.surface,
        "onSurface" to s.onSurface, "surfaceVariant" to s.surfaceVariant, "onSurfaceVariant" to s.onSurfaceVariant,
        "surfaceTint" to s.surfaceTint, "inverseSurface" to s.inverseSurface, "inverseOnSurface" to s.inverseOnSurface,
        "error" to s.error, "onError" to s.onError, "errorContainer" to s.errorContainer,
        "onErrorContainer" to s.onErrorContainer, "outline" to s.outline, "outlineVariant" to s.outlineVariant,
        "surfaceBright" to s.surfaceBright, "surfaceContainer" to s.surfaceContainer,
        "surfaceContainerHigh" to s.surfaceContainerHigh, "surfaceContainerHighest" to s.surfaceContainerHighest,
        "surfaceContainerLow" to s.surfaceContainerLow, "surfaceContainerLowest" to s.surfaceContainerLowest,
        "surfaceDim" to s.surfaceDim,
    )

    private fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    /** WCAG 2 relative luminance of an sRGB color. */
    private fun luminance(argb: Int): Double {
        fun channel(value: Int): Double {
            val c = value / 255.0
            return if (c <= 0.03928) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
        }
        return 0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
    }
}
