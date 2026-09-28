package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.Locale
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cbrt
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * Recolors every console illustration for every palette of every registered theme and checks the
 * result: each color is rewritten and nothing else changes, grey shading keeps its order, dark
 * consoles stay darker than their tile with a visible halo on dark themes, and red becomes the
 * theme's reddest accent. Colors are measured here, independently of the app's Oklab code, from the
 * 8-bit values written into the SVG.
 */
class ConsoleArtTest {

    private class Variant(val name: String, val colors: ThemeColors, val isDark: Boolean) {
        val palette = ConsoleArtPalette(colors.scheme, colors.success, colors.warning)
    }

    private class Parts(val text: List<String>, val values: List<String>)

    private val variants: List<Variant> = ThemeRegistry.all.flatMap { theme ->
        listOfNotNull(
            theme.dark?.let { Variant("${theme.name} (dark)", theme.colors(dark = true), isDark = true) },
            theme.light?.let { Variant("${theme.name} (light)", theme.colors(dark = false), isDark = false) },
        )
    }

    private val art: Map<String, String> = File("src/main/assets/platforms")
        .listFiles { file -> file.extension == "svg" }.orEmpty()
        .sortedBy { it.name }
        .associate { it.name to it.readText() }

    /** Every value given to a color property, as an attribute, an inline style or a `<style>` rule. */
    private val colorValue = Regex("""(?<![\w-])(?:fill|stroke|stop-color)\s*(?::|=\s*["'])\s*([^;"'}\s]+)""")

    /** Distinct grey colors across all the art, darkest first. */
    private val greys: List<Int> by lazy {
        art.values.flatMap { split(it).values }.mapNotNull(::parse).distinct()
            .filter { chroma(it) < GREY_CHROMA }
            .sortedBy { lightness(it) }
    }

    @Test
    fun everyColorIsRewrittenAndNothingElseChanges() {
        assertTrue("no console art found", art.size >= 30)
        val failures = variants.flatMap { v ->
            art.flatMap { (file, svg) ->
                val before = split(svg)
                val after = split(v.palette.recolorSvg(svg))
                if (before.text != after.text || before.values.size != after.values.size) {
                    listOf("${v.name}, $file: text other than colors changed")
                } else {
                    before.values.zip(after.values).mapNotNull { (old, new) ->
                        val color = parse(old)
                        val expected = when {
                            color != null -> hex(v.palette.map(Color(color)).toArgb())
                            old == "none" || old.startsWith("url(") -> old
                            else -> return@mapNotNull "${v.name}, $file: unrecognized color value $old"
                        }
                        if (new == expected) null else "${v.name}, $file: $old became $new, expected $expected"
                    }
                }
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun greyShadingKeepsItsOrder() {
        val original = greys.map(::lightness)
        val failures = variants.flatMap { v ->
            val mapped = greys.map { lightness(v.palette.map(Color(it)).toArgb()) }
            greys.indices.flatMap { j ->
                (0 until j).mapNotNull { i ->
                    val clearlyLighter = original[j] - original[i] >= DISTINCT_GREYS
                    val broken = if (clearlyLighter) mapped[j] <= mapped[i] else mapped[j] < mapped[i] - ROUNDING
                    if (!broken) null
                    else "${v.name}: ${hex(greys[j])} (${fmt(mapped[j])}) should not be darker than ${hex(greys[i])} (${fmt(mapped[i])})"
                }
            }
        }
        assertTrue(failures.take(20).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun darkThemesKeepDarkConsolesBelowTheTileWithAVisibleHalo() {
        val bodies = greys.filter { lightness(it) <= DARK_BODY }
        assertTrue("no dark greys found", bodies.isNotEmpty())
        val failures = variants.filter { it.isDark }.flatMap { v ->
            val s = v.colors.scheme
            // The Library draws tiles as glass: surfaceContainer at the panel alpha over the background.
            val glassAlpha = GlassStyle.of(s, v.colors.success, v.colors.warning, isDark = true, retrowave = false).panelAlpha
            val glassTile = composite(s.surfaceContainer.toArgb(), s.background.toArgb(), glassAlpha.toDouble())
            val tiles = listOf(
                "tile" to (v.palette to s.surfaceContainer.toArgb()),
                "glass tile" to (ConsoleArtPalette(s, v.colors.success, v.colors.warning, Color(glassTile)) to glassTile),
            )
            tiles.flatMap { (label, pair) ->
                val (palette, tile) = pair
                val tileLightness = lightness(tile)
                val darkBodies = bodies.mapNotNull { grey ->
                    val mapped = lightness(palette.map(Color(grey)).toArgb())
                    if (mapped < tileLightness) null
                    else "${v.name}: ${hex(grey)} became lightness ${fmt(mapped)}, not darker than the $label's ${fmt(tileLightness)}"
                }
                val halo = composite(palette.map(Color.White).toArgb(), tile, HALO_OPACITY)
                val haloContrast = contrast(halo, tile)
                darkBodies + listOfNotNull(
                    if (haloContrast >= HALO_CONTRAST) null else "${v.name}: halo is ${fmt(haloContrast)}:1 against the $label",
                )
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun redBecomesTheReddestAccent() {
        val red = 0xFFFF0000.toInt()
        var checked = 0
        val failures = variants.flatMap { v ->
            val s = v.colors.scheme
            val accents = listOf(
                "primary" to s.primary, "secondary" to s.secondary, "tertiary" to s.tertiary,
                "error" to s.error, "success" to v.colors.success, "warning" to v.colors.warning,
            ).map { (name, color) -> name to color.toArgb() }
            // The reddest accent is the one closest to pure red, lightness aside: a pale accent of
            // nearly the same hue (Rosé Pine's rose) is further away than the theme's red.
            val redAxes = oklab(red)
            val reddest = accents.minBy { (_, argb) -> oklab(argb).let { hypot(it[1] - redAxes[1], it[2] - redAxes[2]) } }.first
            val hued = accents.filter { chroma(it.second) >= HUED }
            fun nearest(argb: Int) = hued.minBy { hueDistance(hue(it.second), hue(argb)) }.first
            art.flatMap { (file, svg) ->
                val before = split(svg).values
                val after = split(v.palette.recolorSvg(svg)).values
                before.indices.filter { parse(before[it]) == red }.mapNotNull { index ->
                    checked++
                    val got = nearest(parse(after[index])!!)
                    if (got == reddest) null else "${v.name}, $file: red became ${after[index]}, closest to $got, not $reddest"
                }
            }
        }
        assertTrue("no red found in the art", checked > 0)
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun logoRunsFromTheDarkestToTheLightestTone() {
        val failures = variants.flatMap { v ->
            val s = v.colors.scheme
            val tones = listOf(
                s.background, s.surface, s.surfaceDim, s.surfaceBright, s.surfaceContainerLowest, s.surfaceContainerLow,
                s.surfaceContainer, s.surfaceContainerHigh, s.surfaceContainerHighest, s.surfaceVariant,
                s.outline, s.outlineVariant, s.onSurfaceVariant, s.onSurface,
            ).map { it.toArgb() }
            val m = v.palette.logoMatrix.values
            // A grey input (every channel the same), filtered as Android applies a color matrix.
            fun filtered(grey: Int) = (0 until 3).map { row ->
                (m[row * 5] * grey + m[row * 5 + 1] * grey + m[row * 5 + 2] * grey + m[row * 5 + 4]).roundToInt().coerceIn(0, 255)
            }
            listOf(0 to tones.minBy(::lightness), 255 to tones.maxBy(::lightness)).mapNotNull { (input, tone) ->
                val want = listOf(tone shr 16 and 0xFF, tone shr 8 and 0xFF, tone and 0xFF)
                val got = filtered(input)
                if (got.zip(want).all { (a, b) -> abs(a - b) <= 1 }) null else "${v.name}: logo $input became $got, expected $want"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    private fun split(svg: String): Parts {
        val text = mutableListOf<String>()
        val values = mutableListOf<String>()
        var last = 0
        colorValue.findAll(svg).forEach { match ->
            val value = match.groups[1]!!
            text += svg.substring(last, value.range.first)
            values += value.value
            last = value.range.last + 1
        }
        text += svg.substring(last)
        return Parts(text, values)
    }

    /** ARGB of a hex or named color value, or null for anything else. */
    private fun parse(value: String): Int? {
        val digits = value.removePrefix("#")
        val rgb = when {
            value.startsWith("#") && digits.length == 3 -> digits.map { "$it$it" }.joinToString("").toInt(16)
            value.startsWith("#") && digits.length == 6 -> digits.toInt(16)
            else -> NAMED[value.lowercase()] ?: return null
        }
        return rgb or (0xFF shl 24)
    }

    private fun hex(argb: Int) = String.format(Locale.ROOT, "#%06x", argb and 0xFFFFFF)

    private fun fmt(x: Double) = String.format(Locale.ROOT, "%.3f", x)

    private fun oklab(argb: Int): DoubleArray {
        fun linear(channel: Int): Double {
            val c = channel / 255.0
            return if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
        }
        val r = linear(argb shr 16 and 0xFF)
        val g = linear(argb shr 8 and 0xFF)
        val b = linear(argb and 0xFF)
        val l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
        val m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
        val s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
        return doubleArrayOf(
            0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
        )
    }

    private fun lightness(argb: Int) = oklab(argb)[0]

    private fun chroma(argb: Int) = oklab(argb).let { hypot(it[1], it[2]) }

    private fun hue(argb: Int) = oklab(argb).let { atan2(it[2], it[1]) }

    private fun hueDistance(a: Double, b: Double): Double {
        val d = abs(a - b) % (2 * PI)
        return min(d, 2 * PI - d)
    }

    /** [foreground] at [alpha] over opaque [background], blended per 8-bit channel as the screen does. */
    private fun composite(foreground: Int, background: Int, alpha: Double): Int {
        fun channel(shift: Int) = ((foreground shr shift and 0xFF) * alpha + (background shr shift and 0xFF) * (1 - alpha)).roundToInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    /** WCAG 2 contrast ratio. */
    private fun contrast(a: Int, b: Int): Double {
        fun luminance(argb: Int): Double {
            fun channel(value: Int): Double {
                val c = value / 255.0
                return if (c <= 0.03928) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
            }
            return 0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
        }
        val la = luminance(a)
        val lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    private companion object {
        /** Oklab chroma below which a color counts as grey. */
        const val GREY_CHROMA = 0.03

        /** Oklab chroma a color needs for its hue to mean anything. */
        const val HUED = 0.02

        /** Greys at least this far apart in Oklab lightness must stay strictly ordered. */
        const val DISTINCT_GREYS = 0.05

        /** Lightness two nearly equal greys may swap by when rounded to 8 bits. */
        const val ROUNDING = 0.005

        /** Oklab lightness of the darkest greys the consoles' bodies are drawn in (#4d4d4d is 0.42). */
        const val DARK_BODY = 0.45

        /** Each console's halo is white at half opacity (60% in arcade.svg). */
        const val HALO_OPACITY = 0.5
        const val HALO_CONTRAST = 2.0

        val NAMED = mapOf("gray" to 0x808080, "grey" to 0x808080, "red" to 0xFF0000)
    }
}
