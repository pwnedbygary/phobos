package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import com.google.android.material.color.utilities.Hct
import com.google.android.material.color.utilities.SchemeTonalSpot
import com.google.android.material.color.utilities.TonalPalette
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * WCAG checks for pixel art effects, on the composited 8-bit colors the screen shows. Covers every
 * palette of every registered theme plus Material You schemes from 24 seed hues, like
 * GlassContrastTest. Text drawn straight on the pixel backdrop sits on the pixel plate and is checked
 * over every color a rendered scene contains; pixel panels and the dock are opaque.
 */
class PixelContrastTest {
    private class Variant(val name: String, val scheme: ColorScheme, val success: Color, val warning: Color, val isDark: Boolean)

    private val variants: List<Variant> = ThemeRegistry.all.flatMap { theme ->
        listOfNotNull(
            theme.dark?.let { theme.colors(dark = true).let { c -> Variant("${theme.name} (dark)", c.scheme, c.success, c.warning, true) } },
            theme.light?.let { theme.colors(dark = false).let { c -> Variant("${theme.name} (light)", c.scheme, c.success, c.warning, false) } },
        )
    } + (0 until 360 step 15).flatMap { hue ->
        val seed = Hct.from(hue.toDouble(), 48.0, 50.0).toInt()
        listOf(true, false).map { dark ->
            val colors = SchemeBuilder.withStatusColors(materialYou(seed, dark), dark)
            Variant("Material You, seed hue $hue (${if (dark) "dark" else "light"})", colors.scheme, colors.success, colors.warning, dark)
        }
    }

    @Test
    fun backdropTextReachesAaOnThePixelPlate() {
        val failures = variants.flatMap { v ->
            val s = v.scheme
            val scene = PixelScene.render(60, 110, pixelSceneColors(s, v.isDark)).toSet()
            val text = listOf("onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
            scene.flatMap { backdrop ->
                val plate = composite(s.background.toArgb(), backdrop, paint(PIXEL_PLATE_ALPHA))
                text.mapNotNull { (role, color) ->
                    val ratio = contrast(color.toArgb(), plate)
                    if (ratio < 4.5) "${v.name}: $role on the plate over ${hex(backdrop)}: ${fmt(ratio)}" else null
                }
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun panelAndDockTextReachesAa() {
        val failures = variants.flatMap { v ->
            val s = v.scheme
            val panel = s.surfaceContainer.toArgb()
            val text = listOf(
                "onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary,
                "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
                "success" to v.success, "warning" to v.warning,
            )
            text.mapNotNull { (role, color) ->
                val ratio = contrast(color.toArgb(), panel)
                if (ratio < 4.5) "${v.name}: $role on a pixel panel: ${fmt(ratio)}" else null
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun dockSelectionKeepsItsIconVisible() {
        val failures = variants.mapNotNull { v ->
            val ratio = contrast(v.scheme.onPrimaryContainer.toArgb(), v.scheme.primaryContainer.toArgb())
            if (ratio < 3.0) "${v.name}: selected dock icon: ${fmt(ratio)}" else null
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /**
     * A Material You scheme built as GlassContrastTest builds it (Compose Material 3 1.2.1 on Android 12
     * and 13), plus the primary container roles the dock's pixel selection uses.
     */
    private fun materialYou(seed: Int, dark: Boolean): ColorScheme {
        val palettes = SchemeTonalSpot(Hct.fromInt(seed), dark, 0.0)
        fun TonalPalette.at(tone: Int) = Color(tone(tone))
        val nv = palettes.neutralVariantPalette
        val p = palettes.primaryPalette
        return if (dark) {
            darkColorScheme(
                primary = p.at(80), secondary = palettes.secondaryPalette.at(80), tertiary = palettes.tertiaryPalette.at(80),
                primaryContainer = p.at(30), onPrimaryContainer = p.at(90),
                background = nv.at(6), onBackground = nv.at(90), surface = nv.at(6), onSurface = nv.at(90),
                surfaceVariant = nv.at(30), onSurfaceVariant = nv.at(80), outline = nv.at(60), outlineVariant = nv.at(30),
                surfaceBright = nv.at(24), surfaceDim = nv.at(6), surfaceContainerLowest = nv.at(4), surfaceContainerLow = nv.at(10),
                surfaceContainer = nv.at(12), surfaceContainerHigh = nv.at(17), surfaceContainerHighest = nv.at(22),
            )
        } else {
            lightColorScheme(
                primary = p.at(40), secondary = palettes.secondaryPalette.at(40), tertiary = palettes.tertiaryPalette.at(40),
                primaryContainer = p.at(90), onPrimaryContainer = p.at(10),
                background = nv.at(98), onBackground = nv.at(10), surface = nv.at(98), onSurface = nv.at(10),
                surfaceVariant = nv.at(90), onSurfaceVariant = nv.at(30), outline = nv.at(50), outlineVariant = nv.at(80),
                surfaceBright = nv.at(98), surfaceDim = nv.at(87), surfaceContainerLowest = nv.at(100), surfaceContainerLow = nv.at(96),
                surfaceContainer = nv.at(94), surfaceContainerHigh = nv.at(92), surfaceContainerHighest = nv.at(90),
            )
        }
    }

    private fun composite(foreground: Int, background: Int, alpha: Double): Int {
        fun channel(shift: Int) = ((foreground shr shift and 0xFF) * alpha + (background shr shift and 0xFF) * (1 - alpha)).roundToInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    private fun paint(alpha: Float): Double = (alpha * 255f).roundToInt() / 255.0

    private fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    private fun luminance(argb: Int): Double {
        fun channel(value: Int): Double {
            val c = value / 255.0
            return if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
        }
        return 0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
    }

    private fun hex(argb: Int) = String.format(Locale.ROOT, "#%06x", argb and 0xFFFFFF)

    private fun fmt(x: Double) = String.format(Locale.ROOT, "%.2f", x)
}
