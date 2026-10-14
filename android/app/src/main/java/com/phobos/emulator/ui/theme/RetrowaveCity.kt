package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import kotlin.math.ceil
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.random.Random

/**
 * Night city's colors, from the theme: the sky from the background down to a glow over the city, the
 * haze the far towers fade into, the towers, their windows, the neon in the theme's three accents and a
 * violet from its background, and the water. Light themes get the same city washed toward the background
 * ([lift]), so text over it stays crisp. Shared with the glass panels' contrast search ([GlassStyle]).
 */
internal class CityColors(scheme: ColorScheme, val isDark: Boolean) {
    val background: Int = scheme.background.toArgb()
    val neon: IntArray = intArrayOf(neonOf(scheme.primary), neonOf(scheme.secondary), violet(scheme), neonOf(scheme.tertiary))
    val skyTop: Int = lerp(scheme.background, Color.Black, 0.25f).toArgb()
    val skyMiddle: Int = lerp(scheme.background, Color(neon[2]), 0.3f).toArgb()
    val skyLow: Int = lerp(lerp(scheme.background, Color(neon[0]), 0.62f), Color(neon[2]), 0.2f).toArgb()
    val haze: Int = lerp(Color(skyLow), Color(skyMiddle), 0.4f).toArgb()
    val tower: Int = lerp(lerp(scheme.background, Color.Black, 0.6f), Color(neon[2]), 0.1f).toArgb()
    val windows: IntArray = intArrayOf(
        lerp(Color(0xFFFFD9A0), scheme.tertiary, 0.25f).toArgb(),
        lerp(Color(0xFFBDF4FF), scheme.secondary, 0.3f).toArgb(),
        lerp(Color(0xFFFFE9F6), scheme.primary, 0.2f).toArgb(),
    )
    val water: Int = lerp(scheme.background, Color.Black, 0.45f).toArgb()

    /** How far every color is washed toward the background: none in dark themes. */
    val lift: Float = if (isDark) 0f else 0.55f

    /** [color] as the screen shows it, after the [lift]. */
    fun shown(color: Int): Int = if (lift == 0f) color else PixelKit.mix(color, background, lift)

    /** A neon tube's bright core: its color run toward white. */
    fun core(neon: Int): Int = PixelKit.mix(neon, WHITE, 0.55f)

    private val signature: List<Int> get() = listOf(background, skyTop, skyMiddle, skyLow, haze, tower, water, if (isDark) 1 else 0) + neon.toList() + windows.toList()

    /** Equal when every color is, so a city drawn before can be shown again ([RenderedBackdrop]). */
    override fun equals(other: Any?) = other is CityColors && other.signature == signature

    override fun hashCode() = signature.hashCode()

    private companion object {
        const val WHITE = 0xFFFFFFFF.toInt()

        /** A violet from the background's hue where it has one (the retrowave themes' purples), else between the primary and secondary. */
        fun violet(scheme: ColorScheme): Int {
            val background = Oklab.of(scheme.background)
            val hue = if (background.chroma > 0.02f) background.hue else Oklab.of(lerp(scheme.primary, scheme.secondary, 0.5f)).hue
            val l = 0.62f
            val color = Oklab.lch(l, Oklab.maxChroma(l, hue) * 0.85f, hue).toColor()
            return if (color.luminance() < 0.02f) lerp(scheme.primary, scheme.secondary, 0.5f).toArgb() else color.toArgb()
        }
    }
}

/** [color]'s hue as bright and colorful as sRGB allows at a neon's lightness: night city's neon, and the consoles'. */
internal fun neonOf(color: Color): Int {
    val tone = Oklab.of(color)
    val l = 0.74f
    return Oklab.lch(l, Oklab.maxChroma(l, tone.hue) * 0.92f, tone.hue).toColor().toArgb()
}

/**
 * Night city, drawn into an ARGB array: a dense skyline of futuristic towers in five layers fading into
 * the haze over a glowing horizon, banded and speckled with lit windows, edged and ringed in neon, hung
 * with signs and screens whose light spills into the air, and the whole city mirrored in the water below,
 * its lights drawn down into streaks. Plain Kotlin, drawn once per size and theme; the same size, colors
 * and seed always give the same picture.
 */
internal object NightCity {
    /** Where the water starts, as a fraction of the height. */
    const val WATERLINE = 0.72f

    /** How far each layer of towers, farthest first, fades into the haze. */
    val HAZE = floatArrayOf(0.72f, 0.5f, 0.3f, 0.13f, 0.02f)

    /** The most of the primary neon the city's bloom lays over the sky. */
    const val BLOOM = 0.32f

    /** How far the water darkens what it mirrors: from this at the waterline to [DEEP] at the bottom. */
    const val SHALLOW = 0.38f
    const val DEEP = 0.8f

    fun render(width: Int, height: Int, colors: CityColors, seed: Int = 2077): IntArray =
        CityPainter(width.coerceAtLeast(1), height.coerceAtLeast(1), colors, seed).paint()
}

private class CityPainter(val w: Int, val h: Int, val colors: CityColors, seed: Int) {
    private val px = IntArray(w * h)
    private val random = Random(seed)
    private val water = (h * NightCity.WATERLINE).roundToInt().coerceIn(1, h)

    /** The drawing unit: about a dp on a phone, from the picture's shorter side. */
    private val unit = max(1f, min(w, h) / 420f)
    private fun u(n: Float) = max(1, (n * unit).roundToInt())

    /** Light spilling from the neon, at a quarter of the resolution, per channel. */
    private val gw = ceil(w / GLOW_SCALE.toFloat()).toInt()
    private val gh = ceil(h / GLOW_SCALE.toFloat()).toInt()
    private val glow = Array(3) { FloatArray(gw * gh) }

    private class Layer(
        val fog: Float,
        val widths: ClosedFloatingPointRange<Float>,
        val heights: ClosedFloatingPointRange<Float>,
        /** How far along each tower the next starts, in its widths. */
        val step: ClosedFloatingPointRange<Float>,
        val window: Float,
        val lit: Float,
        val neon: Float,
        val edges: Boolean = false,
    )

    private val layers = listOf(
        Layer(fog = NightCity.HAZE[0], widths = 0.006f..0.016f, heights = 0.16f..0.46f, step = 0.45f..0.9f, window = 0.8f, lit = 0.28f, neon = 0.12f),
        Layer(fog = NightCity.HAZE[1], widths = 0.012f..0.028f, heights = 0.14f..0.44f, step = 0.5f..1f, window = 1f, lit = 0.32f, neon = 0.3f),
        Layer(fog = NightCity.HAZE[2], widths = 0.02f..0.045f, heights = 0.12f..0.36f, step = 0.6f..1.15f, window = 1.2f, lit = 0.34f, neon = 0.5f),
        Layer(fog = NightCity.HAZE[3], widths = 0.035f..0.07f, heights = 0.1f..0.28f, step = 0.9f..1.6f, window = 1.5f, lit = 0.36f, neon = 0.65f),
        Layer(fog = NightCity.HAZE[4], widths = 0.07f..0.12f, heights = 0.1f..0.24f, step = 0.8f..1.1f, window = 2f, lit = 0.34f, neon = 0.95f, edges = true),
    )

    fun paint(): IntArray {
        paintSky()
        layers.forEachIndexed { index, layer ->
            paintLayer(layer, index)
            if (index == 1) paintLandmark()
            if (index == 3) paintLowFog()
        }
        applyGlow()
        paintWater()
        if (colors.lift > 0f) for (i in px.indices) px[i] = colors.shown(px[i])
        return px
    }

    private fun ClosedFloatingPointRange<Float>.pick() = start + (endInclusive - start) * random.nextFloat()

    private fun anyNeon() = colors.neon[random.nextInt(colors.neon.size)]

    // ---- The sky ----

    private fun paintSky() {
        for (y in 0 until water) {
            val t = y / water.toFloat()
            val base = if (t < 0.5f) PixelKit.mix(colors.skyTop, colors.skyMiddle, smooth(t / 0.5f))
            else PixelKit.mix(colors.skyMiddle, colors.skyLow, smooth((t - 0.5f) / 0.5f))
            for (x in 0 until w) {
                // A wide bloom of the city's light over the skyline's middle, and low streaks of cloud it lights.
                val dx = (x - w * 0.45f) / (w * 0.45f)
                val dy = (water - y) / (h * 0.42f)
                val bloom = NightCity.BLOOM * exp(-(dx * dx + dy * dy))
                val streak = cloud(x, y) * t * t * 0.22f
                px[y * w + x] = dither(PixelKit.mix(PixelKit.mix(base, colors.neon[0], bloom), colors.skyLow, streak), x, y)
            }
        }
        if (colors.isDark) {
            repeat(w * h / 6000) {
                val x = random.nextInt(w)
                val y = (random.nextFloat() * random.nextFloat() * water * 0.55f).toInt()
                val i = y * w + x
                px[i] = PixelKit.mix(px[i], WHITE, 0.2f + 0.5f * random.nextFloat())
            }
        }
    }

    /** Long, thin streaks of cloud, from 0 to 1. */
    private fun cloud(x: Int, y: Int): Float {
        val fx = x / w.toFloat()
        val fy = y / h.toFloat()
        val v = sin(fy * 70f + sin(fx * 5.3f) * 1.6f) * 0.5f + sin(fy * 31f - fx * 3.1f + 1.7f) * 0.5f
        return ((v - 0.35f) / 0.65f).coerceIn(0f, 1f)
    }

    // ---- The towers ----

    private fun paintLayer(layer: Layer, index: Int) {
        val face = PixelKit.mix(colors.tower, colors.haze, layer.fog)
        var x = -w * 0.03f
        while (x < w * 1.03f) {
            val width = w * layer.widths.pick()
            val center = (x + width / 2) / w
            if (layer.edges && center in 0.17f..0.83f) {
                x = w * 0.83f
                continue
            }
            // Taller toward the skyline's middle, a little left of center.
            val envelope = 0.72f + 0.42f * exp(-((center - 0.44f) / 0.26f).let { it * it })
            val height = h * layer.heights.pick() * envelope
            paintTower(x.roundToInt(), width.roundToInt().coerceAtLeast(2), height.roundToInt().coerceAtLeast(2), face, layer, index)
            x += width * layer.step.pick()
        }
    }

    private fun paintTower(left: Int, width: Int, height: Int, face: Int, layer: Layer, index: Int) {
        val top = water - height
        val taper = if (random.nextFloat() < 0.25f) 0.12f + 0.2f * random.nextFloat() else 0f
        // Lighter toward its foot, where the haze and the horizon's glow are; narrowing toward its top when tapered.
        for (y in max(0, top) until water) {
            val along = (y - top).toFloat() / height
            val inset = width * taper * (1f - along) / 2f
            val color = PixelKit.mix(face, colors.haze, 0.3f * along * (1f - layer.fog))
            span(left + inset, left + width - inset, y, color)
        }
        // A rim of the city's light down one edge.
        fill(left + width - u(0.7f), top, u(0.7f), height) { _, _ -> PixelKit.mix(face, colors.skyLow, 0.32f) }
        paintCrown(left, top, width, face, layer)
        paintWindows(left + (width * taper / 2).roundToInt(), top, width - (width * taper).roundToInt(), height, layer)
        if (random.nextFloat() < layer.neon) paintNeon(left, top, width, height, layer, index)
    }

    /** The tower's top: flat, stepped back, needled, slanted or crowned. */
    private fun paintCrown(left: Int, top: Int, width: Int, face: Int, layer: Layer) {
        val strength = 1f - layer.fog * 0.85f
        when (random.nextInt(6)) {
            1, 2 -> {
                // Setbacks: a narrower block, and sometimes another on it.
                var y = top
                var tier = width
                repeat(1 + random.nextInt(3)) {
                    tier = (tier * (0.6f + 0.2f * random.nextFloat())).roundToInt().coerceAtLeast(2)
                    val tierHeight = (tier * (0.3f + 0.6f * random.nextFloat())).roundToInt().coerceAtLeast(2)
                    fill(left + (width - tier) / 2, y - tierHeight, tier, tierHeight) { _, _ -> face }
                    y -= tierHeight
                }
                if (random.nextFloat() < 0.5f) needle(left + width / 2, y, (width * (0.5f + random.nextFloat())).roundToInt() + u(4f), face, strength)
            }
            3 -> needle(left + width / 2, top, (width * (0.8f + 1.6f * random.nextFloat())).roundToInt() + u(6f), face, strength)
            4 -> {
                // A slanted roof.
                val rise = (width * (0.3f + 0.6f * random.nextFloat())).roundToInt().coerceAtLeast(2)
                val leftHigh = random.nextBoolean()
                for (dy in 0 until rise) {
                    val span = width * (dy + 1f) / rise
                    val from = if (leftHigh) left.toFloat() else left + width - span
                    span(from, from + span, top - rise + dy, face)
                }
            }
            5 -> {
                // A lantern with a ring of light.
                val lantern = (width * 0.55f).roundToInt().coerceAtLeast(2)
                val lanternHeight = (width * 0.4f).roundToInt() + u(3f)
                val x = left + (width - lantern) / 2
                fill(x, top - lanternHeight, lantern, lanternHeight) { _, _ -> face }
                neonRect(x - u(1f), top - (lanternHeight * 0.55f).roundToInt(), lantern + u(2f), u(0.8f), anyNeon(), 0.85f * strength)
                needle(left + width / 2, top - lanternHeight, u(5f) + width / 3, face, strength)
            }
            else -> {}
        }
    }

    private fun needle(x: Int, base: Int, length: Int, face: Int, strength: Float) {
        fill(x, base - length, u(0.6f), length) { _, _ -> face }
        if (random.nextFloat() < 0.4f) light(x, base - length, WARNING, 0.7f * strength)
    }

    private fun paintWindows(left: Int, top: Int, width: Int, height: Int, layer: Layer) {
        val size = u(layer.window * 0.9f)
        val pitchX = size + u(layer.window * 0.9f)
        val pitchY = size + u(layer.window * 1.3f)
        val color = PixelKit.mix(colors.windows[random.nextInt(colors.windows.size)], colors.haze, layer.fog * 0.7f)
        val margin = u(layer.window)
        when (random.nextInt(4)) {
            0 -> {
                // Floors lit in bands, like offices at night.
                var y = top + margin
                var floor = 0
                val every = 2 + random.nextInt(3)
                while (y + size <= water - margin) {
                    if (floor % every != 0 && random.nextFloat() < 0.85f) {
                        val brightness = 0.35f + 0.35f * random.nextFloat()
                        fill(left + margin, y, width - 2 * margin, size) { i, _ -> PixelKit.mix(px[i], color, brightness) }
                    }
                    y += pitchY
                    floor++
                }
            }
            else -> {
                val lit = layer.lit * (0.5f + random.nextFloat())
                var y = top + margin
                while (y + size <= water - margin) {
                    var x = left + margin
                    while (x + size <= left + width - margin) {
                        if (random.nextFloat() < lit) {
                            val brightness = 0.5f + 0.5f * random.nextFloat()
                            fill(x, y, size, size) { i, _ -> PixelKit.mix(px[i], color, brightness) }
                        }
                        x += pitchX
                    }
                    y += pitchY
                }
            }
        }
    }

    private fun paintNeon(left: Int, top: Int, width: Int, height: Int, layer: Layer, index: Int) {
        val strength = 1f - layer.fog * 0.8f
        val color = anyNeon()
        val tube = u(0.9f)
        val kinds = if (index == 0) 2 else 6
        repeat(if (index >= 3) 2 else 1) {
            when (random.nextInt(kinds)) {
                0 -> {
                    // Light down an edge, or both.
                    val length = (height * (0.5f + 0.5f * random.nextFloat())).roundToInt()
                    if (random.nextBoolean()) neonRect(left, top, tube, length, color, strength)
                    neonRect(left + width - tube, top, tube, length, color, strength)
                }
                1 -> {
                    // Rings of light around the tower.
                    val rings = 1 + random.nextInt(3)
                    repeat(rings) { k -> neonRect(left, top + (height * (0.08f + 0.22f * k)).roundToInt(), width, tube, color, strength) }
                }
                2, 3 -> paintSign(left, top, width, height, color, strength)
                4 -> paintBillboard(left, top, width, height, strength)
                else -> {
                    // The roofline traced in light.
                    neonRect(left, top, width, tube, color, strength)
                    neonRect(left, top, tube, (height * 0.2f).roundToInt(), color, strength)
                    neonRect(left + width - tube, top, tube, (height * 0.2f).roundToInt(), color, strength)
                }
            }
        }
    }

    /** A tall sign of glyph-like strokes, on the tower's face or hung out from its edge. */
    private fun paintSign(left: Int, top: Int, width: Int, height: Int, color: Int, strength: Float) {
        val signW = u(4f + random.nextInt(4)).coerceAtMost(max(3, width))
        val cells = 3 + random.nextInt(5)
        val cell = (signW * 1.15f).roundToInt().coerceAtLeast(3)
        val signH = cell * cells
        if (signH > height * 0.9f) return
        val x0 = when (random.nextInt(3)) {
            0 -> left - signW / 2
            1 -> left + width - signW / 2
            else -> left + (width - signW) / 2
        }
        val y0 = top + (height * (0.05f + 0.35f * random.nextFloat())).roundToInt()
        val core = colors.core(color)
        fill(x0, y0, signW, signH) { _, _ -> PixelKit.mix(color, colors.tower, 0.45f) }
        val stroke = u(0.8f)
        val inset = max(1, signW / 5)
        for (c in 0 until cells) {
            val cy = y0 + c * cell
            repeat(2 + random.nextInt(2)) {
                if (random.nextBoolean()) fill(x0 + inset, cy + inset + random.nextInt(max(1, cell - 2 * inset)), signW - 2 * inset, stroke) { _, _ -> core }
                else fill(x0 + inset + random.nextInt(max(1, signW - 2 * inset)), cy + inset, stroke, cell - 2 * inset) { _, _ -> core }
            }
        }
        addGlow(x0, y0, signW, signH, color, 1.1f * strength)
    }

    /** A wide screen across the tower, a gradient between two neons with scan lines. */
    private fun paintBillboard(left: Int, top: Int, width: Int, height: Int, strength: Float) {
        val bw = (width * (0.6f + 0.35f * random.nextFloat())).roundToInt().coerceAtLeast(4)
        val bh = (bw * (0.4f + 0.3f * random.nextFloat())).roundToInt().coerceAtLeast(3)
        if (bh > height * 0.4f) return
        val x0 = left + (width - bw) / 2
        val y0 = top + (height * (0.05f + 0.25f * random.nextFloat())).roundToInt()
        val a = anyNeon()
        val b = anyNeon()
        val line = u(1.6f).coerceAtLeast(2)
        fill(x0, y0, bw, bh) { _, y ->
            val color = PixelKit.mix(a, b, (y - y0).toFloat() / bh)
            if ((y - y0) % line == 0) PixelKit.mix(color, colors.tower, 0.4f) else PixelKit.mix(color, WHITE, 0.2f)
        }
        addGlow(x0, y0, bw, bh, PixelKit.mix(a, b, 0.5f), 0.85f * strength)
    }

    /** The tallest tower, a landmark left of center: tiered, ringed and edged in light, with a mast. */
    private fun paintLandmark() {
        val face = PixelKit.mix(colors.tower, colors.haze, 0.42f)
        val width = (w * 0.046f).roundToInt().coerceAtLeast(4)
        val left = (w * 0.4f).roundToInt()
        val height = (h * 0.52f).roundToInt()
        val top = water - height
        fill(left, top, width, height) { _, y -> PixelKit.mix(face, colors.haze, 0.22f * (y - top) / height) }
        var y = top
        for (tier in listOf(0.82f, 0.62f, 0.44f, 0.28f)) {
            val tw = (width * tier).roundToInt().coerceAtLeast(2)
            val th = (height * 0.06f).roundToInt().coerceAtLeast(2)
            fill(left + (width - tw) / 2, y - th, tw, th) { _, _ -> face }
            neonRect(left + (width - tw) / 2, y - th, tw, u(0.9f), colors.neon[1], 0.9f)
            y -= th
        }
        needle(left + width / 2, y, (height * 0.2f).roundToInt(), face, 1f)
        paintWindows(left, top, width, height, layers[1])
        neonRect(left, top, u(1f), height, colors.neon[0], 0.8f)
        neonRect(left + width - u(1f), top, u(1f), height, colors.neon[0], 0.8f)
    }

    /** Haze lying low over the water, in front of all but the nearest towers. */
    private fun paintLowFog() {
        val from = water - (h * 0.16f).roundToInt()
        for (y in max(0, from) until water) {
            val t = (y - from).toFloat() / (water - from)
            val amount = 0.42f * t * t
            for (x in 0 until w) px[y * w + x] = PixelKit.mix(px[y * w + x], colors.haze, amount)
        }
    }

    // ---- Light ----

    private fun neonRect(x: Int, y: Int, width: Int, height: Int, color: Int, strength: Float) {
        val core = PixelKit.mix(colors.haze, colors.core(color), strength.coerceIn(0f, 1f))
        fill(x, y, width, height) { _, _ -> core }
        addGlow(x, y, width, height, color, strength)
    }

    /** A point of light, such as a mast's warning light. */
    private fun light(x: Int, y: Int, color: Int, strength: Float) {
        val size = u(1.3f)
        fill(x - size / 2, y - size / 2, size, size) { _, _ -> PixelKit.mix(color, WHITE, 0.3f) }
        addGlow(x - size, y - size, size * 2, size * 2, color, strength * 2f)
    }

    /** Adds [color] at [strength] over the area to the light the neon spills, at the glow's resolution. */
    private fun addGlow(x: Int, y: Int, width: Int, height: Int, color: Int, strength: Float) {
        if (strength <= 0f || width <= 0 || height <= 0) return
        val r = (color shr 16 and 0xFF) / 255f * strength
        val g = (color shr 8 and 0xFF) / 255f * strength
        val b = (color and 0xFF) / 255f * strength
        val gx0 = max(0, Math.floorDiv(x, GLOW_SCALE))
        val gy0 = max(0, Math.floorDiv(y, GLOW_SCALE))
        val gx1 = min(gw - 1, Math.floorDiv(x + width - 1, GLOW_SCALE))
        val gy1 = min(gh - 1, Math.floorDiv(y + height - 1, GLOW_SCALE))
        val cellArea = (GLOW_SCALE * GLOW_SCALE).toFloat()
        for (gy in gy0..gy1) for (gx in gx0..gx1) {
            // Each cell takes the share of it the area covers.
            val cover = overlap(x, width, gx * GLOW_SCALE) * overlap(y, height, gy * GLOW_SCALE) / cellArea
            val i = gy * gw + gx
            glow[0][i] += r * cover
            glow[1][i] += g * cover
            glow[2][i] += b * cover
        }
    }

    private fun overlap(start: Int, length: Int, cell: Int): Int = (min(start + length, cell + GLOW_SCALE) - max(start, cell)).coerceAtLeast(0)

    /** Blurs the spilled light, near and far, adds the horizon's glow, and lays it over the picture. */
    private fun applyGlow() {
        val near = Array(3) { glow[it].copyOf() }
        val far = Array(3) { glow[it].copyOf() }
        val nearRadius = max(1, (unit * 1.6f).roundToInt())
        val farRadius = max(2, (unit * 7f).roundToInt())
        for (c in 0 until 3) {
            repeat(3) { boxBlur(near[c], nearRadius) }
            repeat(3) { boxBlur(far[c], farRadius) }
        }
        val horizon = PixelKit.mix(colors.neon[0], colors.neon[2], 0.3f)
        val reach = h * 0.035f
        for (y in 0 until h) {
            val gy = (y + 0.5f) / GLOW_SCALE - 0.5f
            val y0 = floor(gy).toInt().coerceIn(0, gh - 1)
            val y1 = (y0 + 1).coerceAtMost(gh - 1)
            val fy = (gy - floor(gy)).coerceIn(0f, 1f)
            val band = HORIZON_GLOW * exp(-((y - water) / reach).let { it * it })
            for (x in 0 until w) {
                val gx = (x + 0.5f) / GLOW_SCALE - 0.5f
                val x0 = floor(gx).toInt().coerceIn(0, gw - 1)
                val x1 = (x0 + 1).coerceAtMost(gw - 1)
                val fx = (gx - floor(gx)).coerceIn(0f, 1f)
                val i = y * w + x
                var argb = px[i]
                var shift = 16
                for (c in 0 until 3) {
                    val a = y0 * gw + x0
                    val b = y0 * gw + x1
                    val d = y1 * gw + x0
                    val e = y1 * gw + x1
                    val nearLight = (near[c][a] * (1 - fx) + near[c][b] * fx) * (1 - fy) + (near[c][d] * (1 - fx) + near[c][e] * fx) * fy
                    val farLight = (far[c][a] * (1 - fx) + far[c][b] * fx) * (1 - fy) + (far[c][d] * (1 - fx) + far[c][e] * fx) * fy
                    val light = (nearLight * NEAR_GLOW + farLight * FAR_GLOW) * 255f + band * (horizon shr shift and 0xFF)
                    val channel = (argb shr shift and 0xFF) + light
                    argb = (argb and (0xFF shl shift).inv()) or (min(255f, channel).toInt() shl shift)
                    shift -= 8
                }
                px[i] = argb
            }
        }
    }

    /** A box blur of [radius] cells across, then down, the glow buffer. */
    private fun boxBlur(buffer: FloatArray, radius: Int) {
        val line = FloatArray(max(gw, gh))
        val span = 2 * radius + 1
        for (y in 0 until gh) {
            var sum = 0f
            for (k in -radius..radius) sum += buffer[y * gw + k.coerceIn(0, gw - 1)]
            for (x in 0 until gw) {
                line[x] = sum / span
                sum += buffer[y * gw + (x + radius + 1).coerceAtMost(gw - 1)] - buffer[y * gw + (x - radius).coerceAtLeast(0)]
            }
            System.arraycopy(line, 0, buffer, y * gw, gw)
        }
        for (x in 0 until gw) {
            var sum = 0f
            for (k in -radius..radius) sum += buffer[k.coerceIn(0, gh - 1) * gw + x]
            for (y in 0 until gh) {
                line[y] = sum / span
                sum += buffer[(y + radius + 1).coerceAtMost(gh - 1) * gw + x] - buffer[(y - radius).coerceAtLeast(0) * gw + x]
            }
            for (y in 0 until gh) buffer[y * gw + x] = line[y]
        }
    }

    // ---- The water ----

    /**
     * The city mirrored in the water: each row from the row as far above the waterline, swayed by slow
     * ripples, its lights drawn down into streaks (an average down a run of rows that lengthens with
     * depth), and darkening toward the bottom.
     */
    private fun paintWater() {
        val depth = (h - water).coerceAtLeast(1)
        val mirror = IntArray(water * w)
        System.arraycopy(px, 0, mirror, 0, water * w)
        val r = IntArray(w)
        val g = IntArray(w)
        val b = IntArray(w)
        for (y in water until h) {
            val d = y - water
            val t = d / depth.toFloat()
            val center = water - 1 - (d * 0.9f).roundToInt()
            val run = 1 + (t * 9f * unit).roundToInt()
            r.fill(0)
            g.fill(0)
            b.fill(0)
            var rows = 0
            for (k in 0 until run) {
                val source = (center - k).coerceIn(0, water - 1)
                val sway = ((sin(y * 0.21f / unit + k * 0.5f) + 0.6f * sin(y * 0.047f / unit + 1.3f)) * (0.6f + 4f * t) * unit).roundToInt()
                for (x in 0 until w) {
                    val c = mirror[source * w + (x + sway).coerceIn(0, w - 1)]
                    r[x] += c shr 16 and 0xFF
                    g[x] += c shr 8 and 0xFF
                    b[x] += c and 0xFF
                }
                rows++
            }
            val dim = NightCity.SHALLOW + (NightCity.DEEP - NightCity.SHALLOW) * t
            for (x in 0 until w) {
                val from = (0xFF shl 24) or ((r[x] / rows) shl 16) or ((g[x] / rows) shl 8) or (b[x] / rows)
                px[y * w + x] = dither(PixelKit.mix(from, colors.water, dim), x, y)
            }
        }
        val line = u(1f)
        val horizon = PixelKit.mix(colors.neon[0], WHITE, 0.45f)
        for (y in water until min(h, water + line)) for (x in 0 until w) px[y * w + x] = PixelKit.mix(px[y * w + x], horizon, 0.85f)
    }

    // ---- Helpers ----

    /** Fills a rectangle, clipped to the picture, with what [color] gives for each pixel's index and row. */
    private inline fun fill(x: Int, y: Int, width: Int, height: Int, color: (Int, Int) -> Int) {
        val x0 = max(0, x)
        val x1 = min(w, x + width)
        val y0 = max(0, y)
        val y1 = min(water, y + height)
        for (row in y0 until y1) for (col in x0 until x1) {
            val i = row * w + col
            px[i] = color(i, row)
        }
    }

    /** A row of [color] from [from] to [to], its ends blended by how much of their pixel they cover. */
    private fun span(from: Float, to: Float, y: Int, color: Int) {
        if (y !in 0 until water) return
        val start = floor(from).toInt()
        val end = ceil(to).toInt() - 1
        for (x in max(0, start)..min(w - 1, end)) {
            val cover = min(to, x + 1f) - max(from, x.toFloat())
            val i = y * w + x
            px[i] = if (cover >= 1f) color else PixelKit.mix(px[i], color, cover.coerceIn(0f, 1f))
        }
    }

    /** Half an 8-bit step of ordered dither, so smooth gradients don't band. */
    private fun dither(color: Int, x: Int, y: Int): Int {
        val d = ((PixelKit.threshold(x, y) - 0.47f) * 2f).roundToInt()
        if (d == 0) return color
        fun channel(shift: Int) = ((color shr shift and 0xFF) + d).coerceIn(0, 255) shl shift
        return (color and 0xFF000000.toInt()) or channel(16) or channel(8) or channel(0)
    }

    private fun smooth(t: Float): Float = t.coerceIn(0f, 1f).let { it * it * (3 - 2 * it) }

    private companion object {
        const val GLOW_SCALE = 4
        const val NEAR_GLOW = 1.1f
        const val FAR_GLOW = 1.9f
        const val HORIZON_GLOW = 0.55f
        const val WHITE = 0xFFFFFFFF.toInt()
        const val WARNING = 0xFFFF4D5A.toInt()
    }
}
