package com.phobos.emulator.ui.theme

import android.graphics.Bitmap
import androidx.compose.foundation.Canvas
import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import com.phobos.emulator.data.UiEffects
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * How the Library draws its consoles: a look for each style, every one of them from the Systematic art
 * (BAXY Square's, MIT, and Phobos's PSP in its style; see LICENSE), in the theme's colors.
 */
enum class ConsoleTreatment {
    /** The art recolored to the theme ([ConsoleArtPalette]), as drawn. */
    SYSTEMATIC,

    /** Recolored, with a glass bevel: light along the top and left edges, a gloss across it, a soft shadow below. */
    GLASS,

    /** Neon tubes along the outline and the parts' edges, glowing, over a faint fill. */
    NEON,

    /** Pixel art ([ConsolePixels]): the theme's four tones and accents, flat, in a dark outline. */
    PIXEL,

    /** 16-bit pixel art from the same cells: six shades, cool in the shadows and warm in the light, a black outline, a highlight and a drop shadow. */
    RPG,

    /** Clean and glossy: one color (light on dark themes, the text's on light ones) shaded by the art, a gloss and a soft glow. */
    XMB,

    /** Ink: a line around it and along its parts' edges, screentone in the mid tones and solid ink in the darkest. */
    MANGA,

    /** Phosphor: the art's light and its outline in the CRT style's phosphor, scanlines and a bloom; a dark ink on light themes. */
    CRT,
    ;

    /** Drawn from [ConsolePixels]' cells rather than from the SVG. */
    val pixelated: Boolean get() = this == PIXEL || this == RPG
}

/** The look Match style gives the consoles under these effects. */
val UiEffects.consoleTreatment: ConsoleTreatment
    get() = when (this) {
        UiEffects.NONE -> ConsoleTreatment.GLASS
        UiEffects.RETROWAVE -> ConsoleTreatment.NEON
        UiEffects.PIXEL_ART -> ConsoleTreatment.PIXEL
        UiEffects.RPG -> ConsoleTreatment.RPG
        UiEffects.XMB -> ConsoleTreatment.XMB
        UiEffects.MANGA -> ConsoleTreatment.MANGA
        UiEffects.CRT -> ConsoleTreatment.CRT
    }

/**
 * The theme's colors as the treatments take them: [palette] recolors the art as Systematic does; [tile] is
 * the card the console sits on; [ink] the text on it; the neon is the primary and secondary at a neon's
 * lightness; [phosphor] the CRT style's.
 */
@Immutable
class ConsoleColors(val palette: ConsoleArtPalette, scheme: ColorScheme, val isDark: Boolean, tile: Color, phosphor: Color) {
    internal val tile: Int = tile.toArgb()
    internal val ink: Int = scheme.onSurface.toArgb()
    internal val accents: IntArray = intArrayOf(scheme.primary.toArgb(), scheme.secondary.toArgb(), scheme.tertiary.toArgb(), scheme.error.toArgb())
    internal val neon: IntArray = intArrayOf(neonOf(scheme.primary), neonOf(scheme.secondary))
    internal val phosphor: Int = phosphor.toArgb()

    /** Identifies these colors, for caching what's drawn in them. */
    val key: String = palette.key + ";" + listOf(this.tile, ink, this.phosphor).joinToString(",") { Integer.toHexString(it) } + if (isDark) "d" else "l"

    override fun equals(other: Any?) = other is ConsoleColors && other.key == key

    override fun hashCode() = key.hashCode()
}

/** A small ARGB picture, [width] by [height], not premultiplied. */
internal class Sprite(val width: Int, val height: Int, val pixels: IntArray)

/**
 * Which of the tones whose Oklab lightness is [levels] (darkest first) each grey of [picture] takes, by palette
 * index; colored parts aren't in it. The grey covering most of the console takes the tone nearest its own
 * lightness, and the others that cover a good share of it (a twentieth of its grey cells) the tones either side
 * in their order, those past the end sharing the end tone. The whole run moves a tone up or down if that sets
 * more of them apart, taking the console's lightness up to [SHIFT_DRIFT] from its own for greys that differ by
 * [APART] or more, [NEAR_SHIFT_DRIFT] for closer ones. So a dark console stays dark, its parts apart as far as the
 * tones allow. Each other grey takes the tone of the nearest of those, a step darker or lighter when it stands out.
 */
internal fun toneLevels(picture: ConsolePixels.Picture, levels: FloatArray): Map<Int, Int> {
    val count = levels.size
    val cells = IntArray(picture.palette.size)
    for (y in 0 until picture.height) for (x in 0 until picture.width) picture.at(x, y).let { if (it >= 0) cells[it]++ }
    val light = picture.palette.map { Oklab.of(Color(it)) }.map { if (it.chroma < GREY) it.l else Float.NaN }
    val greys = picture.palette.indices.filter { !light[it].isNaN() && cells[it] > 0 }
    if (greys.isEmpty()) return emptyMap()
    val total = greys.sumOf { cells[it] }
    val main = greys.filter { cells[it] * 20 >= total }.ifEmpty { listOf(greys.maxBy { cells[it] }) }.sortedBy { light[it] }
    val top = main.indexOf(main.maxBy { cells[it] })
    val home = levels.indices.minBy { abs(levels[it] - light[main[top]]) }
    val weight = main.sumOf { cells[it] }
    val own = main.sumOf { light[it].toDouble() * cells[it] } / weight
    fun run(anchor: Int) = main.indices.map { (anchor + it - top).coerceIn(0, count - 1) }
    fun drift(run: List<Int>) = abs(main.indices.sumOf { levels[run[it]].toDouble() * cells[main[it]] } / weight - own)
    fun apart(run: List<Int>, by: Float) = (1 until main.size).count { run[it] != run[it - 1] && light[main[it]] - light[main[it - 1]] >= by }
    var best = run(home)
    for (anchor in listOf(home - 1, home + 1)) {
        if (anchor !in 0 until count) continue
        val moved = run(anchor)
        val drifts = drift(moved)
        if (drifts > SHIFT_DRIFT) continue
        val clearly = apart(moved, APART) - apart(best, APART)
        val barely = apart(moved, 0f) - apart(best, 0f)
        val better = clearly > 0 || clearly == 0 && when {
            barely > 0 -> drifts <= NEAR_SHIFT_DRIFT
            barely == 0 -> drifts < drift(best)
            else -> false
        }
        if (better) best = moved
    }
    val tones = HashMap<Int, Int>()
    main.forEachIndexed { i, grey -> tones[grey] = best[i] }
    for (grey in greys - main.toSet()) {
        val nearest = main.minBy { abs(light[it] - light[grey]) }
        val step = when {
            light[grey] - light[nearest] >= STANDS_OUT -> 1
            light[nearest] - light[grey] >= STANDS_OUT -> -1
            else -> 0
        }
        tones[grey] = (tones.getValue(nearest) + step).coerceIn(0, count - 1)
    }
    return tones
}

/** Below this Oklab chroma an art color is grey. */
private const val GREY = 0.05f

/** How much lighter or darker than its nearest main grey a minor one must be to take a tone of its own. */
private const val STANDS_OUT = 0.06f

/** How far from its own lightness [toneLevels] may move a console to set more of its parts apart. */
private const val SHIFT_DRIFT = 0.2

/** How far [toneLevels] may move it to set apart greys closer than [APART], which the art itself barely tells apart. */
private const val NEAR_SHIFT_DRIFT = 0.12

/** How much lighter than the next darker main grey one must be to count as clearly apart from it. */
private const val APART = 0.05f

/** The pixelated treatments, from [ConsolePixels]' cells. */
internal object ConsolePixelArt {
    /**
     * [picture] in [treatment]'s palette, one pixel a cell: PIXEL with a cell of outline around it, RPG with its
     * outline, a highlight along its top and left edges and its drop shadow a cell further down and right.
     */
    fun sprite(picture: ConsolePixels.Picture, treatment: ConsoleTreatment, colors: ConsoleColors): Sprite {
        val rpg = treatment == ConsoleTreatment.RPG
        val w = picture.width
        val h = picture.height
        val pad = 1
        val width = w + 2 * pad + if (rpg) 1 else 0
        val height = h + 2 * pad + if (rpg) 1 else 0
        val tones = toneLevels(picture, if (rpg) RPG_TONES else PIXEL_TONES)
        val cells = IntArray(picture.palette.size) { cellColor(picture.palette[it], rpg, tones[it], colors) }
        fun filled(x: Int, y: Int) = x in 0 until w && y in 0 until h && picture.at(x, y) >= 0
        val out = IntArray(width * height)
        val outline = if (rpg) RPG_OUTLINE else pixelOutline(colors)
        if (rpg) {
            for (y in -1..h) for (x in -1..w) {
                if (filled(x, y) || filled(x - 1, y) || filled(x + 1, y) || filled(x, y - 1) || filled(x, y + 1)) out[(y + pad + 1) * width + x + pad + 1] = SHADOW
            }
        }
        for (y in -1..h) for (x in -1..w) {
            val i = (y + pad) * width + x + pad
            if (filled(x, y)) {
                var color = cells[picture.at(x, y)]
                if (rpg && (!filled(x - 1, y) || !filled(x, y - 1))) color = PixelKit.mix(color, WHITE, 0.28f)
                out[i] = color
            } else if (filled(x - 1, y) || filled(x + 1, y) || filled(x, y - 1) || filled(x, y + 1)) {
                out[i] = outline
            }
        }
        return Sprite(width, height, out)
    }

    /**
     * An art color as the treatment shows it, from its own limited palette: a grey takes its [tone] ([toneLevels])
     * of four (Pixel) or six (RPG) greys recolored to the theme; a colored part takes the theme accent it maps to.
     */
    private fun cellColor(original: Int, rpg: Boolean, tone: Int?, colors: ConsoleColors): Int {
        if (tone == null) {
            val mapped = Oklab.of(colors.palette.map(Color(original)))
            val accent = colors.accents.minBy { Oklab.of(Color(it)).let { a -> hypot(a.a - mapped.a, a.b - mapped.b) } }
            val color = if (mapped.l < Oklab.of(Color(accent)).l - 0.12f) PixelKit.mix(accent, BLACK, 0.35f) else accent
            return if (rpg) shade(color, mapped.l) else color
        }
        if (!rpg) return colors.palette.map(Color(PIXEL_GREYS[tone])).toArgb()
        return shade(colors.palette.map(Color(RPG_GREYS[tone])).toArgb(), tone / (RPG_GREYS.size - 1f))
    }

    /** RPG's shading: the dark shades run toward a cool violet and the light ones toward a warm white. */
    private fun shade(color: Int, level: Float): Int = when {
        level < 0.5f -> PixelKit.mix(color, COOL, (0.5f - level) * 0.45f)
        level > 0.6f -> PixelKit.mix(color, WARM, (level - 0.6f) * 0.35f)
        else -> color
    }

    private fun pixelTones(colors: ConsoleColors): List<Int> = PIXEL_GREYS.map { colors.palette.map(Color(it)).toArgb() }

    private fun pixelOutline(colors: ConsoleColors): Int =
        if (colors.isDark) PixelKit.mix(pixelTones(colors).first(), BLACK, 0.55f) else PixelKit.mix(colors.ink, BLACK, 0.2f)

    private val PIXEL_GREYS = intArrayOf(0xFF262626.toInt(), 0xFF666666.toInt(), 0xFFA6A6A6.toInt(), 0xFFE6E6E6.toInt())
    private val RPG_GREYS = intArrayOf(0xFF1A1A1A.toInt(), 0xFF404040.toInt(), 0xFF666666.toInt(), 0xFF8C8C8C.toInt(), 0xFFB3B3B3.toInt(), 0xFFE0E0E0.toInt())

    /** The Oklab lightness of Pixel's and RPG's greys, before the theme recolors them. */
    val PIXEL_TONES = FloatArray(PIXEL_GREYS.size) { Oklab.of(Color(PIXEL_GREYS[it])).l }
    val RPG_TONES = FloatArray(RPG_GREYS.size) { Oklab.of(Color(RPG_GREYS[it])).l }
    private const val RPG_OUTLINE = 0xFF120F1C.toInt()
    private const val SHADOW = 0x66000000
    private const val COOL = 0xFF2E2A6E.toInt()
    private const val WARM = 0xFFFFF0C8.toInt()
    private const val WHITE = 0xFFFFFFFF.toInt()
    private const val BLACK = 0xFF000000.toInt()
}

/**
 * The treatments drawn from the SVG: [apply] takes the art rendered without its halo and gives it finished,
 * with a [margin] around it for the glow, line or shadow, so art rendered at [inside] a box still fits it.
 */
internal object ConsoleFinish {
    /** The margin each side, as a share of the art's longer side. */
    const val MARGIN = 0.07f

    fun margin(side: Int): Int = (MARGIN * side).roundToInt()

    /** The largest size to render the art at that, finished, still fits [box] pixels. */
    fun inside(box: Int): Int {
        var side = (box / (1f + 2f * MARGIN)).toInt() + 1
        while (side > 1 && side + 2 * margin(side) > box) side--
        return side
    }

    fun apply(treatment: ConsoleTreatment, src: IntArray, width: Int, height: Int, colors: ConsoleColors): Sprite {
        val art = Art(src, width, height, colors)
        val out = when (treatment) {
            ConsoleTreatment.GLASS -> art.glass()
            ConsoleTreatment.NEON -> art.neon()
            ConsoleTreatment.XMB -> art.xmb()
            ConsoleTreatment.MANGA -> art.manga()
            ConsoleTreatment.CRT -> art.crt()
            else -> error("$treatment isn't drawn from the SVG")
        }
        return Sprite(art.w, art.h, out.toArgb())
    }
}

/** Premultiplied RGBA, a float per channel per pixel. */
private class Layer(size: Int) {
    val r = FloatArray(size)
    val g = FloatArray(size)
    val b = FloatArray(size)
    val a = FloatArray(size)

    /** [color] at [alpha] over what's at [i]. */
    fun over(i: Int, color: Int, alpha: Float) {
        if (alpha <= 0f) return
        val k = alpha.coerceAtMost(1f)
        r[i] = (color shr 16 and 0xFF) / 255f * k + r[i] * (1 - k)
        g[i] = (color shr 8 and 0xFF) / 255f * k + g[i] * (1 - k)
        b[i] = (color and 0xFF) / 255f * k + b[i] * (1 - k)
        a[i] = k + a[i] * (1 - k)
    }

    /** [color]'s light at [amount] added at [i], as a glow adds to what's under it. */
    fun add(i: Int, color: Int, amount: Float) {
        if (amount <= 0f) return
        r[i] = min(1f, r[i] + (color shr 16 and 0xFF) / 255f * amount)
        g[i] = min(1f, g[i] + (color shr 8 and 0xFF) / 255f * amount)
        b[i] = min(1f, b[i] + (color and 0xFF) / 255f * amount)
        a[i] = min(1f, a[i] + amount)
    }

    fun toArgb(): IntArray = IntArray(a.size) { i ->
        val alpha = a[i]
        if (alpha <= 0.002f) 0 else {
            fun channel(v: Float) = (v / alpha * 255f).roundToInt().coerceIn(0, 255)
            ((alpha * 255f).roundToInt().coerceIn(0, 255) shl 24) or (channel(r[i]) shl 16) or (channel(g[i]) shl 8) or channel(b[i])
        }
    }
}

/**
 * The art laid out with its margin: its coverage, its lightness spread over the console's own range, its
 * recolored pixels, and the edges between its parts and around it.
 */
private class Art(src: IntArray, width: Int, height: Int, val colors: ConsoleColors) {
    val margin = ConsoleFinish.margin(max(width, height))
    val w = width + 2 * margin
    val h = height + 2 * margin

    /** The art's longer side, which the strokes and glows scale with. */
    val size = max(width, height).toFloat()

    val alpha = FloatArray(w * h)
    val light = FloatArray(w * h)
    val mapped = IntArray(w * h)
    private val grey = BooleanArray(w * h)
    private val raw: FloatArray
    val top: Int
    val bottom: Int
    val left: Int
    val right: Int

    init {
        val lightOf = HashMap<Int, Float>()
        val mappedOf = HashMap<Int, Int>()
        val greyOf = HashMap<Int, Boolean>()
        var t = h
        var bo = 0
        var l = w
        var ri = 0
        for (y in 0 until height) for (x in 0 until width) {
            val p = src[y * width + x]
            val a = (p ushr 24) / 255f
            if (a <= 0f) continue
            val rgb = p or (0xFF shl 24)
            val i = (y + margin) * w + x + margin
            alpha[i] = a
            grey[i] = greyOf.getOrPut(rgb) { Oklab.of(Color(rgb)).chroma < GREY }
            light[i] = lightOf.getOrPut(rgb) { Oklab.of(Color(rgb)).l }
            mapped[i] = mappedOf.getOrPut(rgb) { colors.palette.map(Color(rgb)).toArgb() }
            if (a >= 0.5f) {
                t = min(t, y + margin)
                bo = max(bo, y + margin)
                l = min(l, x + margin)
                ri = max(ri, x + margin)
            }
        }
        top = t
        bottom = max(bo, t)
        left = l
        right = max(ri, l)
        raw = light.copyOf()
        // The console's own range of lightness, so a dark console's parts stand apart as well as a light one's.
        val lights = FloatArray(alpha.count { it >= 0.5f })
        var n = 0
        for (i in alpha.indices) if (alpha[i] >= 0.5f) lights[n++] = light[i]
        lights.sort()
        if (lights.isNotEmpty()) {
            val lo = lights[(lights.size * 0.02f).toInt()]
            val hi = lights[(lights.size * 0.98f).toInt().coerceAtMost(lights.size - 1)]
            for (i in light.indices) light[i] = ((light[i] - lo) / max(0.05f, hi - lo)).coerceIn(0f, 1f)
        }
    }

    private fun at(x: Int, y: Int, field: FloatArray) = field[y.coerceIn(0, h - 1) * w + x.coerceIn(0, w - 1)]

    /**
     * The art recolored as Systematic does, but with each grey moved first toward its place in the console's own
     * range of lightness (two thirds of the way), so a dark console's parts stay apart under the glass's light.
     */
    val spreadMapped: IntArray by lazy {
        val byLevel = HashMap<Int, Int>()
        IntArray(w * h) { i ->
            if (alpha[i] <= 0f) return@IntArray 0
            if (!grey[i]) return@IntArray mapped[i]
            val level = ((0.35f * raw[i].coerceIn(0f, 1f) + 0.65f * light[i]) * 255f).roundToInt()
            byLevel.getOrPut(level) { colors.palette.map(Oklab(level / 255f, 0f, 0f).toColor()).toArgb() }
        }
    }

    /** How sharply the lightness changes at each pixel inside the art, against its four neighbors inside it too. */
    val edges: FloatArray by lazy {
        FloatArray(w * h) { i ->
            if (alpha[i] < 0.5f) return@FloatArray 0f
            val x = i % w
            val y = i / w
            var most = 0f
            for ((dx, dy) in NEIGHBORS) {
                val j = (y + dy).coerceIn(0, h - 1) * w + (x + dx).coerceIn(0, w - 1)
                if (alpha[j] >= 0.5f) most = max(most, abs(light[i] - light[j]))
            }
            most
        }
    }

    /** The outline: how steeply the coverage changes, a soft line two or three pixels wide around the art. */
    val outline: FloatArray by lazy {
        FloatArray(w * h) { i ->
            val x = i % w
            val y = i / w
            val gx = at(x + 1, y, alpha) - at(x - 1, y, alpha)
            val gy = at(x, y + 1, alpha) - at(x, y - 1, alpha)
            (hypot(gx, gy) * 1.1f).coerceIn(0f, 1f)
        }
    }

    /** A soft copy of [field], blurred [radius] pixels (three box blurs). */
    fun blur(field: FloatArray, radius: Int): FloatArray {
        val out = field.copyOf()
        val line = FloatArray(max(w, h))
        val span = 2 * radius + 1
        repeat(3) {
            for (y in 0 until h) {
                var sum = 0f
                for (k in -radius..radius) sum += out[y * w + k.coerceIn(0, w - 1)]
                for (x in 0 until w) {
                    line[x] = sum / span
                    sum += out[y * w + (x + radius + 1).coerceAtMost(w - 1)] - out[y * w + (x - radius).coerceAtLeast(0)]
                }
                System.arraycopy(line, 0, out, y * w, w)
            }
            for (x in 0 until w) {
                var sum = 0f
                for (k in -radius..radius) sum += out[k.coerceIn(0, h - 1) * w + x]
                for (y in 0 until h) {
                    line[y] = sum / span
                    sum += out[(y + radius + 1).coerceAtMost(h - 1) * w + x] - out[(y - radius).coerceAtLeast(0) * w + x]
                }
                for (y in 0 until h) out[y * w + x] = line[y]
            }
        }
        return out
    }

    private fun radius(share: Float) = max(1, (size * share).roundToInt())

    fun glass(): Layer {
        val layer = Layer(w * h)
        val shadow = blur(FloatArray(w * h) { i -> at(i % w, i / w - radius(0.03f), alpha) }, radius(0.025f))
        val spanX = max(1, right - left).toFloat()
        val spanY = max(1, bottom - top).toFloat()
        for (i in alpha.indices) {
            layer.over(i, BLACK, shadow[i] * 0.28f)
            val a = alpha[i]
            if (a <= 0f) continue
            val x = i % w
            val y = i / w
            val facing = (at(x + 1, y, alpha) - at(x - 1, y, alpha)) + (at(x, y + 1, alpha) - at(x, y - 1, alpha))
            var color = spreadMapped[i]
            if (facing > 0f) color = PixelKit.mix(color, WHITE, (facing * 0.7f).coerceAtMost(0.6f))
            if (facing < 0f) color = PixelKit.mix(color, BLACK, (-facing * 0.35f).coerceAtMost(0.3f))
            val d = (x - left) / spanX + (y - top) / spanY
            color = PixelKit.mix(color, WHITE, 0.16f * exp(-((d - 0.5f) / 0.2f).let { it * it }) * (if (y < top + spanY * 0.6f) 1f else 0.4f))
            layer.over(i, color, a)
        }
        return layer
    }

    fun neon(): Layer {
        val pink = colors.neon[0]
        val cyan = colors.neon[1]
        val inner = FloatArray(w * h) { i -> ((edges[i] - 0.07f) * 5f).coerceIn(0f, 1f) * (1f - outline[i]) }
        val glowOuter = blur(outline, radius(0.012f))
        val wideOuter = blur(outline, radius(0.04f))
        val glowInner = blur(inner, radius(0.01f))
        val layer = Layer(w * h)
        if (colors.isDark) {
            val pinkTube = readable(PixelKit.mix(pink, WHITE, 0.5f), colors.tile)
            val cyanTube = readable(PixelKit.mix(cyan, WHITE, 0.45f), colors.tile)
            for (i in alpha.indices) {
                layer.over(i, pink, alpha[i] * 0.07f)
                layer.add(i, pink, wideOuter[i] * 0.55f + glowOuter[i] * 0.9f)
                layer.add(i, cyan, glowInner[i] * 0.8f)
                layer.over(i, cyanTube, inner[i] * 0.95f)
                layer.over(i, pinkTube, outline[i])
            }
        } else {
            // Light added to a light tile would wash out, so the tubes are darkened and their glow is a tint.
            val pinkTube = readable(pink, colors.tile, INK)
            val cyanTube = readable(cyan, colors.tile, INK)
            for (i in alpha.indices) {
                layer.over(i, pink, alpha[i] * 0.06f)
                layer.over(i, pink, (wideOuter[i] * 0.2f + glowOuter[i] * 0.35f).coerceAtMost(0.5f))
                layer.over(i, cyan, (glowInner[i] * 0.3f).coerceAtMost(0.4f))
                layer.over(i, cyanTube, inner[i] * 0.95f)
                layer.over(i, pinkTube, outline[i])
            }
        }
        return layer
    }

    fun xmb(): Layer {
        val base = if (colors.isDark) PixelKit.mix(WHITE, colors.accents[0], 0.1f) else PixelKit.mix(colors.ink, colors.accents[0], 0.2f)
        val glow = blur(alpha, radius(0.035f))
        val spanY = max(1, bottom - top).toFloat()
        val layer = Layer(w * h)
        for (i in alpha.indices) {
            layer.over(i, base, glow[i] * 0.32f)
            val a = alpha[i]
            if (a <= 0f) continue
            val y = i / w
            var color = PixelKit.mix(base, colors.tile, (1f - light[i]) * 0.42f + (edges[i] * 0.9f).coerceAtMost(0.35f))
            val t = (y - top) / spanY
            if (t < 0.5f) color = PixelKit.mix(color, WHITE, 0.3f * (1f - t / 0.5f))
            layer.over(i, color, a)
        }
        return layer
    }

    fun manga(): Layer {
        val ink = colors.ink
        val pitch = max(3f, size / 34f)
        val layer = Layer(w * h)
        val line = blur(outline, 1)
        for (i in alpha.indices) {
            val x = i % w
            val y = i / w
            val a = alpha[i]
            val l = light[i]
            val density = when {
                a < 0.5f -> 0f
                l > 0.68f -> 0f
                l > 0.42f -> 0.26f
                l > 0.2f -> 0.52f
                else -> 1f
            }
            val tone = if (density >= 1f) 1f else dot(x, y, density, pitch)
            val parts = ((edges[i] - 0.06f) * 6f).coerceIn(0f, 1f)
            val coverage = max(max(tone * a, parts), (line[i] * 1.6f).coerceAtMost(1f))
            layer.over(i, ink, coverage)
        }
        return layer
    }

    fun crt(): Layer {
        val dark = colors.isDark
        val glowing = FloatArray(w * h) { i ->
            val shade = if (dark) light[i] else 1f - light[i]
            val lit = max(
                alpha[i] * (0.22f + 0.78f * shade) + ((edges[i] - 0.05f) * 3f).coerceIn(0f, 1f) * 0.4f * alpha[i],
                outline[i] * 0.9f,
            )
            (if ((i / w) % 3 == 2) lit * 0.5f else lit).coerceIn(0f, 1f)
        }
        val bloom = blur(glowing, radius(0.03f))
        val layer = Layer(w * h)
        if (dark) {
            val bright = readable(colors.phosphor, colors.tile)
            for (i in alpha.indices) {
                layer.add(i, colors.phosphor, bloom[i] * 0.6f)
                layer.over(i, PixelKit.mix(bright, WHITE, glowing[i] * 0.25f), glowing[i])
            }
        } else {
            // On a light screen the phosphor prints as ink, its glow a faint tint.
            val ink = readable(colors.phosphor, colors.tile, INK)
            for (i in alpha.indices) {
                layer.over(i, colors.phosphor, (bloom[i] * 0.3f).coerceAtMost(0.35f))
                layer.over(i, ink, glowing[i])
            }
        }
        return layer
    }

    /** [color] taken toward black on a light tile, or white on a dark one, until it stands out [ratio]:1 against [tile]. */
    private fun readable(color: Int, tile: Int, ratio: Float = 3f): Int {
        val toward = if (Color(tile).luminance() > 0.18f) BLACK else WHITE
        var t = 0f
        var result = color
        while (contrastRatio(Color(result), Color(tile)) < ratio && t < 1f) {
            t += 0.05f
            result = PixelKit.mix(color, toward, t)
        }
        return result
    }

    /** How much of a screentone cell at ([x], [y]) is ink for a tone of [density], dots [pitch] apart on a 45-degree grid. */
    private fun dot(x: Int, y: Int, density: Float, pitch: Float): Float {
        if (density <= 0f) return 0f
        val u = (x + y + 1) / SQRT2 / pitch
        val v = (x - y) / SQRT2 / pitch
        val radius = pitch * sqrt(density / Math.PI.toFloat()) * 1.02f
        var best = 0f
        for (cu in floor(u).toInt()..floor(u).toInt() + 1) for (cv in floor(v).toInt()..floor(v).toInt() + 1) {
            best = max(best, (radius - hypot(u - cu, v - cv) * pitch + 0.5f).coerceIn(0f, 1f))
        }
        return best
    }

    private companion object {
        val NEIGHBORS = listOf(1 to 0, -1 to 0, 0 to 1, 0 to -1)
        val SQRT2 = sqrt(2f)

        /** The contrast a light theme's ink is darkened to, so its antialiased edges still stand out 3:1. */
        const val INK = 4.5f
        const val WHITE = 0xFFFFFFFF.toInt()
        const val BLACK = 0xFF000000.toInt()
    }
}

/**
 * The console [asset] as pixel art in [treatment] (PIXEL or RPG), centered in its box at the largest whole
 * number of screen pixels to the cell, so every cell is the same size and stays crisp.
 */
@Composable
fun PixelConsole(
    asset: String,
    treatment: ConsoleTreatment,
    colors: ConsoleColors,
    contentDescription: String,
    modifier: Modifier = Modifier,
) {
    val picture = ConsolePixels.pictures.getValue(asset)
    val image = remember(picture, treatment, colors) {
        val sprite = ConsolePixelArt.sprite(picture, treatment, colors)
        Bitmap.createBitmap(sprite.pixels, sprite.width, sprite.height, Bitmap.Config.ARGB_8888).asImageBitmap()
    }
    Canvas(
        modifier.semantics {
            this.contentDescription = contentDescription
            role = Role.Image
        },
    ) {
        val scale = max(1, floor(min(size.width / image.width, size.height / image.height)).toInt())
        val width = image.width * scale
        val height = image.height * scale
        drawImage(
            image,
            dstOffset = IntOffset(((size.width - width) / 2f).roundToInt(), ((size.height - height) / 2f).roundToInt()),
            dstSize = IntSize(width, height),
            filterQuality = FilterQuality.None,
        )
    }
}
