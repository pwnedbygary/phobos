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
 * WCAG checks for the glass look, measured on the composited 8-bit colors the screen shows rather
 * than on opaque roles. Covers every palette of every registered theme plus Material You schemes
 * built the way Android 12 and 13 build them (Material's tonal-spot palettes from 24 seed hues,
 * mapped to roles at the tones Compose Material 3 1.2.1 uses), each with and without Retrowave
 * effects. Backdrops are sampled more finely than the app's own search: every glow at every tenth
 * of its strength in every combination, and the sunset across its gradients.
 */
class GlassContrastTest {

    private class Variant(val name: String, val scheme: ColorScheme, val success: Color, val warning: Color, val isDark: Boolean) {
        fun style(retrowave: Boolean) = GlassStyle.of(scheme, success, warning, isDark, retrowave)
    }

    private class Check(val label: String, val foreground: Int, val background: Int, val minimum: Double)

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
    fun panelTextReachesAaOverEveryBackdrop() = assertAll { v, retrowave ->
        val s = v.scheme
        val style = v.style(retrowave)
        val body = listOf("onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
        val accents = body + listOf(
            "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
            "success" to v.success, "warning" to v.warning,
        )
        val backdrops = panelBackdrops(v, style, retrowave)
        listOf("panel" to (style.panelAlpha to body), "accent panel" to (style.accentPanelAlpha to accents)).flatMap { (kind, pair) ->
            val (alpha, text) = pair
            backdrops.flatMap { backdrop ->
                val fill = composite(s.surfaceContainer.toArgb(), backdrop, alpha.toDouble())
                val lit = listOf(
                    "" to fill,
                    " under the gloss" to composite(WHITE, fill, style.glossAlpha.toDouble()),
                    " under the shade" to composite(BLACK, fill, style.shadeAlpha.toDouble()),
                )
                lit.flatMap { (where, background) ->
                    text.map { (role, color) -> Check("$role on a $kind$where over ${hex(backdrop)}", color.toArgb(), background, 4.5) }
                }
            }
        }
    }

    @Test
    fun screenTextReachesAaOverTheGlows() = assertAll { v, retrowave ->
        if (retrowave) return@assertAll emptyList()
        val s = v.scheme
        val style = v.style(retrowave = false)
        val text = listOf("onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
        glowBackdrops(v, style).flatMap { backdrop ->
            listOf(backdrop, composite(BLACK, backdrop, style.shadowAlpha * SHADOW_REACH)).flatMap { background ->
                text.map { (role, color) -> Check("$role on the backdrop ${hex(background)}", color.toArgb(), background, 4.5) }
            }
        }
    }

    @Test
    fun dockSelectionKeepsItsIconVisible() = assertAll { v, retrowave ->
        val style = v.style(retrowave)
        val primary = v.scheme.primary.toArgb()
        panelBackdrops(v, style, retrowave).map { backdrop ->
            val pill = composite(primary, composite(v.scheme.surfaceContainer.toArgb(), backdrop, style.panelAlpha.toDouble()), style.indicatorAlpha.toDouble())
            Check("primary icon on the dock's pill over ${hex(backdrop)}", primary, pill, 3.0)
        }
    }

    @Test
    fun glowsStayNearTheBackgroundsLuminance() {
        val failures = variants.flatMap { v ->
            val background = luminance(v.scheme.background.toArgb())
            v.style(retrowave = false).glows.mapNotNull { glow ->
                val l = luminance(glow.color.toArgb())
                val ceiling = if (v.isDark) max(background, DARK_GLOW_LUMINANCE) else background
                if (l <= ceiling + LUMINANCE_ROUNDING) null
                else "${v.name}: glow ${hex(glow.color.toArgb())} has luminance ${fmt(l)}, above ${fmt(ceiling)}"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun valuesStayInRange() {
        val failures = variants.flatMap { v ->
            listOf(false, true).mapNotNull { retrowave ->
                val style = v.style(retrowave)
                val problems = listOfNotNull(
                    "panel alpha ${style.panelAlpha}".takeUnless { style.panelAlpha in 0.6f..1f },
                    "accent panel alpha ${style.accentPanelAlpha} below the panel's".takeUnless { style.accentPanelAlpha in style.panelAlpha..1f },
                    "glows drawn with Retrowave effects".takeUnless { !retrowave || style.glows.all { it.alpha == 0f } },
                    "shadow drawn with Retrowave effects".takeUnless { !retrowave || style.shadowAlpha == 0f },
                )
                if (problems.isEmpty()) null else "${v.name}${if (retrowave) " with Retrowave" else ""}: ${problems.joinToString()}"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /** Colors under a panel: the glows (or the sunset), and a neighboring panel's shadow where it reaches. */
    private fun panelBackdrops(v: Variant, style: GlassStyle, retrowave: Boolean): List<Int> =
        if (retrowave) sunsetBackdrops(v) else glowBackdrops(v, style).flatMap { listOf(it, composite(BLACK, it, style.shadowAlpha * SHADOW_REACH)) }.distinct()

    /** The background under every combination of glows, each at every tenth of its strength, in drawing order. */
    private fun glowBackdrops(v: Variant, style: GlassStyle): List<Int> =
        style.glows.fold(listOf(v.scheme.background.toArgb())) { layers, glow ->
            layers.flatMap { under -> (0..10).map { composite(glow.color.toArgb(), under, glow.alpha * it / 10.0) } }.distinct()
        }

    /** The Retrowave sunset across its gradients, each point also under a card's neon halo at every strength. */
    private fun sunsetBackdrops(v: Variant): List<Int> {
        val sunset = SunsetColors(v.scheme, v.isDark)
        fun argb(color: Color) = color.copy(alpha = 1f).toArgb()
        val steps = (0..4).map { it / 4.0 }
        val sky = listOf(sunset.skyTop, sunset.skyMiddle, sunset.skyBottom).map(::argb).zipWithNext().flatMap { (a, b) -> steps.map { lerp(a, b, it) } }
        val glowAtSun = (0..6).map { composite(argb(sunset.sunGlow), argb(sunset.skyBottom), sunset.sunGlow.alpha * it / 6.0) }
        val sun = steps.flatMap { t ->
            val color = lerp(argb(sunset.sunTop), argb(sunset.sunBottom), t)
            glowAtSun.map { composite(color, it, sunset.sunAlpha.toDouble()) }
        }
        val floor = steps.map { lerp(argb(sunset.floorTop), argb(sunset.floorBottom), it) }
        val grid = floor.flatMap { under -> steps.map { composite(argb(sunset.grid), under, sunset.grid.alpha * it) } }
        val horizon = listOf(argb(sunset.skyBottom), argb(sunset.floorTop)).flatMap { under ->
            steps.map { composite(argb(sunset.horizonGlow), under, sunset.horizonGlow.alpha * it) } +
                composite(argb(sunset.horizonLine), under, sunset.horizonLine.alpha.toDouble())
        }
        val stars = if (sunset.isDark) steps.map { composite(argb(sunset.star), argb(sunset.skyTop), 0.15 + (sunset.starMaxAlpha - 0.15) * it) } else emptyList()
        val scene = (sky + glowAtSun + sun + floor + grid + horizon + stars).distinct()
        val primary = v.scheme.primary.toArgb()
        return scene.flatMap { under -> steps.map { composite(primary, under, NEON_HALO * it) } }.distinct()
    }

    private fun assertAll(checks: (Variant, Boolean) -> List<Check>) {
        val failures = variants.flatMap { v ->
            listOf(false, true).flatMap { retrowave ->
                checks(v, retrowave).mapNotNull { check ->
                    val ratio = contrast(check.foreground, check.background)
                    if (ratio >= check.minimum) null
                    else "${v.name}${if (retrowave) " with Retrowave" else ""}: ${check.label} is ${fmt(ratio)}:1, needs ${check.minimum}:1"
                }
            }
        }
        assertTrue("${failures.size} failures\n" + failures.take(25).joinToString("\n"), failures.isEmpty())
    }

    /** A Material You scheme as Compose Material 3 1.2.1 builds it on Android 12 and 13, from Material's tonal-spot palettes. */
    private fun materialYou(seed: Int, dark: Boolean): ColorScheme {
        val palettes = SchemeTonalSpot(Hct.fromInt(seed), dark, 0.0)
        fun TonalPalette.at(tone: Int) = Color(tone(tone))
        val nv = palettes.neutralVariantPalette
        return if (dark) {
            darkColorScheme(
                primary = palettes.primaryPalette.at(80), secondary = palettes.secondaryPalette.at(80), tertiary = palettes.tertiaryPalette.at(80),
                background = nv.at(6), onBackground = nv.at(90), surface = nv.at(6), onSurface = nv.at(90),
                surfaceVariant = nv.at(30), onSurfaceVariant = nv.at(80), outline = nv.at(60), outlineVariant = nv.at(30),
                surfaceBright = nv.at(24), surfaceDim = nv.at(6), surfaceContainerLowest = nv.at(4), surfaceContainerLow = nv.at(10),
                surfaceContainer = nv.at(12), surfaceContainerHigh = nv.at(17), surfaceContainerHighest = nv.at(22),
            )
        } else {
            lightColorScheme(
                primary = palettes.primaryPalette.at(40), secondary = palettes.secondaryPalette.at(40), tertiary = palettes.tertiaryPalette.at(40),
                background = nv.at(98), onBackground = nv.at(10), surface = nv.at(98), onSurface = nv.at(10),
                surfaceVariant = nv.at(90), onSurfaceVariant = nv.at(30), outline = nv.at(50), outlineVariant = nv.at(80),
                surfaceBright = nv.at(98), surfaceDim = nv.at(87), surfaceContainerLowest = nv.at(100), surfaceContainerLow = nv.at(96),
                surfaceContainer = nv.at(94), surfaceContainerHigh = nv.at(92), surfaceContainerHighest = nv.at(90),
            )
        }
    }

    /** [foreground] at [alpha] over opaque [background], blended per 8-bit channel as the screen does. */
    private fun composite(foreground: Int, background: Int, alpha: Double): Int {
        fun channel(shift: Int) = ((foreground shr shift and 0xFF) * alpha + (background shr shift and 0xFF) * (1 - alpha)).roundToInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    private fun composite(foreground: Int, background: Int, alpha: Float) = composite(foreground, background, alpha.toDouble())

    /** Gradient interpolation between two opaque colors, per 8-bit channel. */
    private fun lerp(a: Int, b: Int, t: Double) = composite(b, a, t)

    private fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    private val luminances = HashMap<Int, Double>()

    /** WCAG 2 relative luminance of an sRGB color. */
    private fun luminance(argb: Int): Double = luminances.getOrPut(argb) {
        fun channel(value: Int): Double {
            val c = value / 255.0
            return if (c <= 0.03928) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
        }
        0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
    }

    private fun hex(argb: Int) = String.format(Locale.ROOT, "#%06x", argb and 0xFFFFFF)

    private fun fmt(x: Double) = String.format(Locale.ROOT, "%.2f", x)

    private companion object {
        val WHITE = 0xFFFFFFFF.toInt()
        val BLACK = 0xFF000000.toInt()

        /**
         * Share of a panel's shadow that reaches text or a neighboring panel, as the app assumes: the
         * shadow is offset 4 dp with a 12 dp blur, and both sit at least 8 dp from a panel's edges.
         */
        const val SHADOW_REACH = 0.1

        /** Peak alpha of a card's neon halo at the cards' intensity. */
        const val NEON_HALO = 0.55 * 0.45

        const val DARK_GLOW_LUMINANCE = 0.03
        const val LUMINANCE_ROUNDING = 0.005
    }
}
