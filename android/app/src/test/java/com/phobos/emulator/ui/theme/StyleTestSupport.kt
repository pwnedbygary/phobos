package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.ui.graphics.Color
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

/** A theme palette as the style effects' contrast tests check it. */
internal class StyleVariant(val name: String, val scheme: ColorScheme, val success: Color, val warning: Color, val isDark: Boolean)

/** Every registered theme's palette, like GlassContrastTest. */
internal val styleVariants: List<StyleVariant> by lazy {
    ThemeRegistry.all.map { theme ->
        theme.colors.let { c -> StyleVariant(theme.name, c.scheme, c.success, c.warning, theme.isDark) }
    }
}

/** [foreground] over [background] at [alpha], in 8-bit channels as the screen shows it. */
internal fun blend(foreground: Int, background: Int, alpha: Double): Int {
    fun channel(shift: Int) = ((foreground shr shift and 0xFF) * alpha + (background shr shift and 0xFF) * (1 - alpha)).roundToInt()
    return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
}

/** [alpha] as a paint stores it, in 1/255 steps. */
internal fun paintAlpha(alpha: Float): Double = (alpha * 255f).roundToInt() / 255.0

internal fun wcagContrast(a: Int, b: Int): Double {
    val la = relativeLuminance(a)
    val lb = relativeLuminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
}

private fun relativeLuminance(argb: Int): Double {
    fun channel(value: Int): Double {
        val c = value / 255.0
        return if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
    }
    return 0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
}

internal fun hexOf(argb: Int) = String.format(Locale.ROOT, "#%06x", argb and 0xFFFFFF)

internal fun twoPlaces(x: Double) = String.format(Locale.ROOT, "%.2f", x)
