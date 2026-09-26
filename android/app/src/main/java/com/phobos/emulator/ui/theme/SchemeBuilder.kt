package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import kotlin.math.max
import kotlin.math.min

/** A Material 3 scheme plus the status colors Material doesn't define. */
class ThemeColors(val scheme: ColorScheme, val success: Color, val warning: Color)

/** WCAG 2 contrast ratio between two opaque colors. */
fun contrastRatio(a: Color, b: Color): Float {
    val la = a.luminance()
    val lb = b.luminance()
    return (max(la, lb) + 0.05f) / (min(la, lb) + 0.05f)
}

/**
 * Maps a [Palette] onto Material 3 roles. Surface tones step from the background toward the
 * palette's raised panel tone; text and accent colors keep their published hue but move away from
 * the background until they reach WCAG AA on every surface they are drawn on.
 */
internal object SchemeBuilder {
    const val TEXT_CONTRAST = 4.5f
    const val OUTLINE_CONTRAST = 3f

    // Aim slightly above the thresholds so 8-bit rounding can't leave a color just under them.
    private const val MARGIN = 0.05f
    private const val STEPS = 50

    fun build(p: Palette): ThemeColors {
        val dark = p.isDark
        val away = if (dark) Color.White else Color.Black
        val towardBackground = if (dark) Color.Black else Color.White

        val bg = Color(p.background)
        val fg = Color(p.foreground)
        // The palette's panel tone is the top surface level; the levels between ramp up to it.
        val highest = (p.raised?.let(::Color) ?: bg).withContrast(listOf(bg), if (dark) 1.25f else 1.15f, away, margin = 0f)
        val lowest = p.backgroundAlt?.let(::Color)?.takeIf { (it.luminance() < bg.luminance()) == dark }
            ?: mix(bg, towardBackground, if (dark) 0.35f else 0.7f)
        val low = mix(bg, highest, 0.3f)
        val container = mix(bg, highest, 0.5f)
        val high = mix(bg, highest, 0.75f)
        val dim = if (dark) bg else mix(highest, fg, 0.06f)
        val bright = if (dark) mix(highest, fg, 0.06f) else bg
        val allSurfaces = listOf(bg, lowest, low, container, high, highest, dim, bright)
        // Secondary text sits on screens, cards, list rows and dialogs, not on the top levels.
        val textSurfaces = listOf(bg, lowest, low, container, high)
        val cardSurfaces = listOf(bg, low, container)

        fun text(color: Color, on: List<Color>) = color.withContrast(on, TEXT_CONTRAST, away)
        fun onFill(fill: Color): Color =
            listOf(bg, lowest).firstOrNull { contrastRatio(it, fill) >= TEXT_CONTRAST + MARGIN }
                ?: if (contrastRatio(Color.White, fill) >= contrastRatio(Color.Black, fill)) Color.White else Color.Black
        fun containerOf(argb: Long, override: Long?) =
            override?.let(::Color) ?: mix(bg, Color(argb), if (dark) 0.3f else 0.2f)
        // A container on the far side of mid-grey from the background (a bright one on a dark theme)
        // takes the background as its text color; otherwise the accent is pushed until it reads.
        fun onContainer(accent: Color, container: Color) =
            if ((container.luminance() > 0.18f) == dark) onFill(container)
            else accent.withContrast(listOf(container), TEXT_CONTRAST, away)

        // Primary also colors dialog actions, so it has to read on the dialog surface too.
        val primary = text(Color(p.primary), cardSurfaces + high)
        val secondary = text(Color(p.secondary), cardSurfaces)
        val tertiary = text(Color(p.tertiary), cardSurfaces)
        val error = text(Color(p.error), cardSurfaces)
        val primaryContainer = containerOf(p.primary, p.primaryContainer)
        val secondaryContainer = containerOf(p.secondary, p.secondaryContainer)
        val tertiaryContainer = containerOf(p.tertiary, p.tertiaryContainer)
        val errorContainer = containerOf(p.error, null)
        val onSurface = text(fg, allSurfaces)
        val inverseSurface = onSurface

        val scheme = ColorScheme(
            primary = primary,
            onPrimary = onFill(primary),
            primaryContainer = primaryContainer,
            onPrimaryContainer = onContainer(primary, primaryContainer),
            inversePrimary = Color(p.primary).withContrast(listOf(inverseSurface), TEXT_CONTRAST, towardBackground),
            secondary = secondary,
            onSecondary = onFill(secondary),
            secondaryContainer = secondaryContainer,
            onSecondaryContainer = onContainer(secondary, secondaryContainer),
            tertiary = tertiary,
            onTertiary = onFill(tertiary),
            tertiaryContainer = tertiaryContainer,
            onTertiaryContainer = onContainer(tertiary, tertiaryContainer),
            background = bg,
            onBackground = onSurface,
            surface = bg,
            onSurface = onSurface,
            surfaceVariant = high,
            onSurfaceVariant = text(p.subtle?.let(::Color) ?: mix(fg, bg, 0.3f), textSurfaces),
            surfaceTint = primary,
            inverseSurface = inverseSurface,
            inverseOnSurface = bg.withContrast(listOf(inverseSurface), TEXT_CONTRAST, towardBackground),
            error = error,
            onError = onFill(error),
            errorContainer = errorContainer,
            onErrorContainer = onContainer(error, errorContainer),
            outline = (p.muted?.let(::Color) ?: mix(bg, fg, 0.45f))
                .withContrast(listOf(bg, low, container), OUTLINE_CONTRAST, away),
            outlineVariant = mix(highest, fg, 0.12f),
            scrim = Color.Black,
            surfaceBright = bright,
            surfaceContainer = container,
            surfaceContainerHigh = high,
            surfaceContainerHighest = highest,
            surfaceContainerLow = low,
            surfaceContainerLowest = lowest,
            surfaceDim = dim,
        )
        return ThemeColors(scheme, success = text(Color(p.success), cardSurfaces), warning = text(Color(p.warning), cardSurfaces))
    }

    /** Adds readable success/warning colors to a scheme that wasn't built from a palette (Material You). */
    fun withStatusColors(scheme: ColorScheme, isDark: Boolean): ThemeColors {
        val away = if (isDark) Color.White else Color.Black
        val on = listOf(scheme.background, scheme.surfaceContainerLow, scheme.surfaceContainer, scheme.surfaceContainerHigh)
        val success = Color(if (isDark) 0xFF7BD88F else 0xFF1E7B34).withContrast(on, TEXT_CONTRAST, away)
        val warning = Color(if (isDark) 0xFFFFB454 else 0xFF8A5A00).withContrast(on, TEXT_CONTRAST, away)
        return ThemeColors(scheme, success, warning)
    }

    private fun mix(from: Color, to: Color, fraction: Float) = lerp(from, to, fraction).quantized()

    private fun Color.quantized() = Color(toArgb())

    /** This color moved toward [toward] until it reaches [minRatio] against every color in [against]. */
    private fun Color.withContrast(against: List<Color>, minRatio: Float, toward: Color, margin: Float = MARGIN): Color {
        var candidate = quantized()
        var step = 0
        while (step < STEPS && against.minOf { contrastRatio(candidate, it) } < minRatio + margin) {
            step++
            candidate = mix(this, toward, step / STEPS.toFloat())
        }
        return candidate
    }
}
