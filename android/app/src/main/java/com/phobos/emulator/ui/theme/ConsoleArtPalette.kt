package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.graphics.toArgb
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min

/**
 * Recolors the Library's console illustrations (the SVGs in `assets/platforms`) in a theme's colors.
 *
 * Greys map by lightness onto the theme's own tones: its surface, outline and text roles, sorted
 * dark to light, which flips on its own for light themes. Greys darker than mid-grey land below
 * the card surface, so dark consoles stay darker than their tile; where the theme has little room
 * below its cards, the darkest tone is extended toward black so their detail stays visible. White
 * (the halo behind each console) becomes the lightest tone. Colored parts take the accent closest
 * in hue (weighed by how colorful it is), blended with the grey tone for their lightness so the
 * shading survives.
 */
@Immutable
class ConsoleArtPalette(scheme: ColorScheme, success: Color, warning: Color) {
    private val tile = Oklab.of(scheme.surfaceContainer)

    /** The theme's tones sorted dark to light, then the same with the shadow extension. */
    private val themeTones: List<Oklab>
    private val tones: List<Oklab>
    private val accents: List<Oklab>

    /** Original lightness that lands on the tile. */
    private val split: Float

    /** Identifies the mapping, for caching recolored art. */
    val key: String

    init {
        val toneRoles = listOf(
            scheme.background, scheme.surface, scheme.surfaceDim, scheme.surfaceBright,
            scheme.surfaceContainerLowest, scheme.surfaceContainerLow, scheme.surfaceContainer,
            scheme.surfaceContainerHigh, scheme.surfaceContainerHighest, scheme.surfaceVariant,
            scheme.outline, scheme.outlineVariant, scheme.onSurfaceVariant, scheme.onSurface,
        )
        val accentRoles = listOf(scheme.primary, scheme.secondary, scheme.tertiary, scheme.error, success, warning)
        key = (toneRoles + accentRoles).joinToString(",") { Integer.toHexString(it.toArgb()) }

        themeTones = toneRoles.map(Oklab::of).sortedBy { it.l }
        val darkest = themeTones.first()
        val floor = max(0f, tile.l - SHADOW_RANGE)
        tones = if (darkest.l - floor > TONE_GAP) {
            val k = floor / darkest.l
            listOf(Oklab(floor, darkest.a * k, darkest.b * k)) + themeTones
        } else {
            themeTones
        }
        val lo = tones.first().l
        val hi = tones.last().l
        split = if (hi - lo > TONE_GAP) ((tile.l - lo) / (hi - lo)).coerceIn(MID_GREY, MAX_SPLIT) else MID_GREY

        val accentTones = accentRoles.map(Oklab::of)
        accents = accentTones.filter { it.chroma >= MIN_ACCENT_CHROMA }.ifEmpty { accentTones }
    }

    /** Maps the Phobos logo's brightness from the theme's darkest tone (black) to its lightest (white). */
    val logoMatrix: ColorMatrix = run {
        val dark = themeTones.first().toColor()
        val light = themeTones.last().toColor()
        fun row(from: Float, to: Float) =
            floatArrayOf((to - from) * LUMA_RED, (to - from) * LUMA_GREEN, (to - from) * LUMA_BLUE, 0f, from * 255f)
        ColorMatrix(row(dark.red, light.red) + row(dark.green, light.green) + row(dark.blue, light.blue) + floatArrayOf(0f, 0f, 0f, 1f, 0f))
    }

    /** [logoMatrix] as a filter, created on first use since it wraps a framework object. */
    val logoFilter: ColorFilter by lazy { ColorFilter.colorMatrix(logoMatrix) }

    /** The theme color an illustration color becomes. */
    fun map(color: Color): Color {
        val original = Oklab.of(color)
        val grey = greyTone(original.l)
        val weight = ACCENT_WEIGHT * smoothstep((original.chroma - GREY_CHROMA) / (FULL_CHROMA - GREY_CHROMA))
        if (weight <= 0f) return grey.toColor()
        return grey.mix(accentFor(original.hue), weight).toColor()
    }

    /**
     * Rewrites every hex (`#rgb`, `#rrggbb`) and named color given to `fill`, `stroke` or
     * `stop-color`, as an attribute or a style declaration (inline or in a `<style>` rule). `none`,
     * references, opacities and shapes are left as they are.
     */
    fun recolorSvg(svg: String): String {
        val recolored = HashMap<String, String>()
        return COLOR_PROPERTY.replace(svg) { match ->
            val (property, separator, value) = match.destructured
            val color = parseColor(value) ?: return@replace match.value
            property + separator + recolored.getOrPut(value.lowercase()) { hex(map(color)) }
        }
    }

    override fun equals(other: Any?) = other is ConsoleArtPalette && other.key == key

    override fun hashCode() = key.hashCode()

    /** The theme tone a grey of Oklab lightness [l] becomes. */
    private fun greyTone(l: Float): Oklab {
        val lo = tones.first().l
        val hi = tones.last().l
        val target = if (l <= split) lo + (tile.l - lo) * (l / split) else tile.l + (hi - tile.l) * ((l - split) / (1f - split))
        return toneAt(target)
    }

    /**
     * The accent closest to [hue], weighed by how colorful each accent is, so red takes a theme's red
     * rather than a pale pink of nearly the same hue. Falls back to the nearest hue when no accent is
     * within a quarter turn.
     */
    private fun accentFor(hue: Float): Oklab {
        val best = accents.maxBy { accentScore(it, hue) }
        return if (accentScore(best, hue) > 0f) best else accents.minBy { hueDistance(it.hue, hue) }
    }

    /** The color at lightness [l] along [tones], blending the two tones either side. */
    private fun toneAt(l: Float): Oklab {
        if (l <= tones.first().l) return tones.first()
        for (i in 1 until tones.size) {
            val upper = tones[i]
            if (l <= upper.l) {
                val lower = tones[i - 1]
                return lower.mix(upper, (l - lower.l) / (upper.l - lower.l))
            }
        }
        return tones.last()
    }

    private companion object {
        /** Greys darker than this original lightness land below the tile. */
        const val MID_GREY = 0.5f
        const val MAX_SPLIT = 0.95f

        /** Oklab lightness kept between the tile and the darkest tone for dark console detail. */
        const val SHADOW_RANGE = 0.2f
        const val TONE_GAP = 0.002f

        /** Below this Oklab chroma a color is grey; accents blend in gradually up to [FULL_CHROMA]. */
        const val GREY_CHROMA = 0.03f
        const val FULL_CHROMA = 0.12f
        const val ACCENT_WEIGHT = 0.7f

        /** Accents greyer than this have no reliable hue and are skipped when others exist. */
        const val MIN_ACCENT_CHROMA = 0.02f

        const val LUMA_RED = 0.2126f
        const val LUMA_GREEN = 0.7152f
        const val LUMA_BLUE = 0.0722f

        val COLOR_PROPERTY = Regex(
            """(?<![\w-])(fill|stroke|stop-color)(\s*:\s*|\s*=\s*["'])""" +
                """(#[0-9a-fA-F]{6}(?![0-9a-fA-F])|#[0-9a-fA-F]{3}(?![0-9a-fA-F])|[a-zA-Z]+(?![\w(-]))""",
        )

        /** CSS named colors that SVG art commonly uses; others (`none`, `currentColor`) are kept. */
        val NAMED_COLORS = mapOf(
            "black" to 0xFF000000, "white" to 0xFFFFFFFF, "gray" to 0xFF808080, "grey" to 0xFF808080,
            "silver" to 0xFFC0C0C0, "red" to 0xFFFF0000, "maroon" to 0xFF800000, "orange" to 0xFFFFA500,
            "yellow" to 0xFFFFFF00, "olive" to 0xFF808000, "lime" to 0xFF00FF00, "green" to 0xFF008000,
            "aqua" to 0xFF00FFFF, "cyan" to 0xFF00FFFF, "teal" to 0xFF008080, "blue" to 0xFF0000FF,
            "navy" to 0xFF000080, "fuchsia" to 0xFFFF00FF, "magenta" to 0xFFFF00FF, "purple" to 0xFF800080,
        )

        fun parseColor(value: String): Color? = when {
            value.length == 4 && value[0] == '#' -> Color(0xFF000000 or value.drop(1).map { "$it$it" }.joinToString("").toLong(16))
            value.length == 7 && value[0] == '#' -> Color(0xFF000000 or value.drop(1).toLong(16))
            else -> NAMED_COLORS[value.lowercase()]?.let(::Color)
        }

        fun hex(color: Color) = "#" + (color.toArgb() and 0xFFFFFF or 0x1000000).toString(16).substring(1)

        fun smoothstep(t: Float): Float {
            val x = t.coerceIn(0f, 1f)
            return x * x * (3f - 2f * x)
        }

        fun hueDistance(a: Float, b: Float): Float {
            val d = abs(a - b) % (2f * PI.toFloat())
            return min(d, 2f * PI.toFloat() - d)
        }

        fun accentScore(accent: Oklab, hue: Float): Float {
            val closeness = max(0f, cos(hueDistance(accent.hue, hue)))
            return accent.chroma * closeness * closeness * closeness
        }
    }
}
