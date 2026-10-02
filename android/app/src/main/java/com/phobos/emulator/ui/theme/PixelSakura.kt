package com.phobos.emulator.ui.theme

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/**
 * The sakura backdrop: a moonlit, sunlit, or slowly cycling sky over layered mountains, a pagoda, and
 * a cherry branch in blossom. Petals drift down on the wind at eight frames a second.
 */
internal enum class SakuraMode { NIGHT, DAY, CYCLE }

internal class SakuraArt(
    cols: Int,
    rows: Int,
    private val palette: PixelPalette,
    private val mode: SakuraMode = SakuraMode.NIGHT,
    private val seed: Int = 1990,
) : PixelArt(cols, rows) {
    private val still = IntArray(cols * rows)
    private val starColor = palette.tint(palette.onBackground, 0.75f)
    private val stars = StarField(cols, rows, (rows * 0.5f).toInt(), cols * rows / 240, seed)
    private val blossom = PixelKit.mix(palette.tint(palette.primary, 0.8f), PINK, if (palette.isDark) 0.5f else 0.3f)
    private val petalColors = intArrayOf(blossom, PixelKit.mix(blossom, WHITE, 0.35f))

    /** Each petal: start column and row, fall and drift in pixels a second, sway in pixels, sway period in ms, phase. */
    private val petals: Array<DoubleArray>

    init {
        paintScene(if (mode == SakuraMode.DAY) 1f else if (mode == SakuraMode.NIGHT) 0f else EVENING, 0L)

        val random = Random(seed + 5)
        petals = Array((cols * rows / 2600).coerceIn(6, 90)) {
            doubleArrayOf(
                random.nextDouble() * cols,
                random.nextDouble() * rows,
                6.0 + 8.0 * random.nextDouble(),
                -(3.0 + 5.0 * random.nextDouble()),
                1.5 + 3.0 * random.nextDouble(),
                2000.0 + 2000.0 * random.nextDouble(),
                random.nextDouble() * 2 * PI,
            )
        }
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        val daylight = when (mode) {
            SakuraMode.NIGHT -> 0f
            SakuraMode.DAY -> 1f
            SakuraMode.CYCLE -> if (live) cycleDaylight(time) else EVENING
        }
        paintScene(daylight, if (live) time else 0L)
        System.arraycopy(still, 0, out, 0, still.size)
        if (!live) return
        if (daylight < 0.6f) stars.twinkle(time, out, starColor)
        val seconds = time / 1000.0
        petals.forEachIndexed { i, p ->
            val angle = 2 * PI * time / p[5] + p[6]
            val x = wrap(p[0] + p[3] * seconds + p[4] * sin(angle), cols + 6.0) - 3.0
            val y = wrap(p[1] + p[2] * seconds, rows + 6.0) - 3.0
            val px = floor(x).toInt()
            val py = floor(y).toInt()
            val color = petalColors[i % 2]
            // Turning as it falls: flat, then edge on.
            PixelKit.put(out, cols, rows, px, py, color)
            if (sin(2 * angle) > 0) PixelKit.put(out, cols, rows, px + 1, py, color)
            else PixelKit.put(out, cols, rows, px, py + 1, color)
        }
    }

    override fun hold(time: Long): Long = PETAL_FRAME_MS - Math.floorMod(time, PETAL_FRAME_MS)

    /** Rebuilds the static landscape with the current day-to-night lighting. */
    private fun paintScene(daylight: Float, time: Long) {
        val nightTop = palette.background
        val dayTop = PixelKit.mix(palette.tint(palette.secondary, 0.72f), 0xFF58A9E8.toInt(), 0.5f)
        val nightBottom = palette.tint(palette.primary, 0.3f)
        val dayBottom = PixelKit.mix(palette.tint(palette.onBackground, 0.45f), 0xFFBFE8FF.toInt(), 0.6f)
        PixelKit.sky(still, cols, 0, rows, PixelKit.mix(nightTop, dayTop, daylight), PixelKit.mix(nightBottom, dayBottom, daylight), bands = 7)
        if (daylight < 0.75f) stars.paint(still, starColor)
        val sunArc = cycleArc(if (mode == SakuraMode.CYCLE) time else if (mode == SakuraMode.DAY) CYCLE_MS / 4 else 3 * CYCLE_MS / 4)
        val moonArc = cycleArc(if (mode == SakuraMode.CYCLE) time + CYCLE_MS / 2 else 3 * CYCLE_MS / 4)
        if (daylight > 0.08f) paintSun(sunArc.first, sunArc.second, palette.tint(palette.tertiary, 0.95f), palette.tint(palette.onBackground, 0.8f))
        if (daylight < 0.92f) paintMoon(moonArc.first, moonArc.second, palette.tint(palette.onBackground, 0.9f), palette.tint(palette.onBackground, 0.7f), palette.tint(palette.onBackground, 0.3f), seed)
        paintRidge(PixelKit.ridge(cols, seed + 1, 0.3f), rows * 0.8f, rows * 0.2f, PixelKit.mix(palette.tint(palette.secondary, 0.28f), palette.tint(palette.secondary, 0.62f), daylight))
        paintRidge(PixelKit.ridge(cols, seed + 2, 0.45f), rows * 0.86f, rows * 0.12f, PixelKit.mix(palette.tint(palette.secondary, 0.18f), palette.tint(palette.secondary, 0.46f), daylight))
        val silhouette = PixelKit.mix(PixelKit.mix(palette.background, BLACK, if (palette.isDark) 0.45f else 0.35f), palette.tint(palette.secondary, 0.25f), daylight * 0.45f)
        paintHill(silhouette)
        paintPagoda(silhouette, PixelKit.mix(palette.tint(palette.tertiary, 0.85f), palette.tint(palette.tertiary, 0.35f), daylight))
        paintBranch(silhouette, seed)
    }

    private fun cycleDaylight(time: Long): Float {
        val phase = Math.floorMod(time, CYCLE_MS).toFloat() / CYCLE_MS
        return ((sin(phase * 2f * PI - PI / 2).toFloat() + 1f) / 2f).coerceIn(0f, 1f)
    }

    /** Sun and moon travel opposite halves of the same horizon-to-horizon arc. */
    private fun cycleArc(time: Long): Pair<Float, Float> {
        val phase = Math.floorMod(time, CYCLE_MS).toFloat() / CYCLE_MS
        val x = cols * (0.1f + 0.8f * phase)
        val y = rows * (0.54f - 0.35f * sin(phase * PI).toFloat())
        return x to y
    }

    private fun paintMoon(cx: Float, cy: Float, moon: Int, shade: Int, glow: Int, seed: Int) {
        val r = min(cols, rows) * 0.12f
        if (r < 2f) return
        val random = Random(seed + 3)
        val maria = List(5) {
            Triple(cx + (random.nextFloat() - 0.5f) * r * 1.1f, cy + (random.nextFloat() - 0.5f) * r * 1.1f, r * (0.12f + 0.18f * random.nextFloat()))
        }
        for (y in max(0, (cy - r - 6).toInt()) until min(rows, ceil(cy + r + 6).toInt())) {
            for (x in max(0, (cx - r - 6).toInt()) until min(cols, ceil(cx + r + 6).toInt())) {
                val dx = x + 0.5f - cx
                val dy = y + 0.5f - cy
                val d = sqrt(dx * dx + dy * dy)
                val i = y * cols + x
                if (d < r) {
                    val dark = maria.any { (mx, my, mr) -> (x + 0.5f - mx).let { it * it } + (y + 0.5f - my).let { it * it } < mr * mr }
                    still[i] = if (dark && PixelKit.threshold(x, y) < 0.6f) shade else moon
                } else if (d < r + 5f) {
                    val strength = 1f - (d - r) / 5f
                    if (strength * 0.7f > PixelKit.threshold(x, y)) still[i] = PixelKit.mix(still[i], glow, 0.5f)
                }
            }
        }
    }

    private fun paintSun(cx: Float, cy: Float, sun: Int, glow: Int) {
        val r = min(cols, rows) * 0.1f
        for (y in floor(cy - r - 3).toInt()..ceil(cy + r + 3).toInt()) for (x in floor(cx - r - 3).toInt()..ceil(cx + r + 3).toInt()) {
            val d = sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy))
            if (d < r) PixelKit.put(still, cols, rows, x, y, sun)
            else if (d < r + 3 && PixelKit.threshold(x, y) < 0.4f) PixelKit.blend(still, cols, rows, x, y, glow, 0.35f)
        }
    }

    /** Mountains standing on row [base], up to [height] rows tall, filled down to the bottom in [color]. */
    private fun paintRidge(ridge: FloatArray, base: Float, height: Float, color: Int) {
        for (x in 0 until cols) {
            val top = (base - height * (0.3f + 0.7f * ridge[x])).toInt().coerceIn(0, rows)
            for (y in top until rows) still[y * cols + x] = color
        }
    }

    /** Where the hill's top is at column [x]: a rise on the right, over a strip of ground. */
    private fun hillTop(x: Int): Int {
        val u = ((x + 0.5f) / cols - 0.48f) / 0.62f
        val rise = if (u in 0f..1f) sin(PI * u).toFloat() else 0f
        return (rows * (0.92f - 0.12f * rise)).toInt().coerceIn(0, rows)
    }

    private fun paintHill(color: Int) {
        for (x in 0 until cols) for (y in hillTop(x) until rows) still[y * cols + x] = color
    }

    /** A three-tier pagoda on the hill, with a spire and a lit window on each tier. */
    private fun paintPagoda(color: Int, window: Int) {
        val center = (cols * 0.8f).toInt()
        var width = max(9f, rows * 0.13f)
        val tier = max(3f, rows * 0.05f)
        if (width > cols * 0.4f) return
        var y = hillTop(center)
        repeat(3) {
            val bodyHeight = max(2, (tier * 0.6f).roundToInt())
            val bodyHalf = (width * 0.35f).roundToInt()
            for (dy in 1..bodyHeight) for (dx in -bodyHalf..bodyHalf) PixelKit.put(still, cols, rows, center + dx, y - dy, color)
            PixelKit.put(still, cols, rows, center, y - bodyHeight / 2 - 1, window)
            PixelKit.put(still, cols, rows, center - 1, y - bodyHeight / 2 - 1, window)
            y -= bodyHeight
            // The roof, its eaves sweeping up at the tips.
            val roofHeight = max(1, (tier * 0.4f).roundToInt())
            val roofHalf = (width * 0.5f).roundToInt()
            for (dy in 1..roofHeight) {
                val inset = (dy - 1) * roofHalf / (roofHeight + 2)
                for (dx in -roofHalf + inset..roofHalf - inset) PixelKit.put(still, cols, rows, center + dx, y - dy, color)
            }
            PixelKit.put(still, cols, rows, center - roofHalf - 1, y - 2, color)
            PixelKit.put(still, cols, rows, center + roofHalf + 1, y - 2, color)
            y -= roofHeight
            width *= 0.78f
        }
        val spire = max(3, (tier * 1.2f).roundToInt())
        for (dy in 1..spire) {
            PixelKit.put(still, cols, rows, center, y - dy, color)
            if (dy % 2 == 0 && dy < spire) {
                PixelKit.put(still, cols, rows, center - 1, y - dy, color)
                PixelKit.put(still, cols, rows, center + 1, y - dy, color)
            }
        }
    }

    /** A cherry branch reaching in from the top left, its twigs heavy with blossom. */
    private fun paintBranch(color: Int, seed: Int) {
        val strokes = listOf(
            floatArrayOf(-0.02f, 0.06f, 0.34f, 0.16f, 4f, 2f),
            floatArrayOf(0.12f, 0.1f, 0.2f, 0.03f, 2f, 1f),
            floatArrayOf(0.22f, 0.135f, 0.3f, 0.25f, 2f, 1f),
            floatArrayOf(0.28f, 0.15f, 0.4f, 0.12f, 1.5f, 1f),
        )
        val scale = min(1f, rows / 270f).coerceAtLeast(0.5f)
        for (s in strokes) stroke(cols * s[0], rows * s[1], cols * s[2], rows * s[3], s[4] * scale, s[5] * scale, color)
        val random = Random(seed + 6)
        val light = PixelKit.mix(blossom, WHITE, 0.35f)
        val dark = PixelKit.mix(blossom, color, 0.35f)
        for (s in strokes) {
            val clusters = 4 + random.nextInt(3)
            repeat(clusters) {
                val t = 0.25f + 0.75f * random.nextFloat()
                val cx = cols * (s[0] + (s[2] - s[0]) * t) + (random.nextFloat() - 0.5f) * 6f
                val cy = rows * (s[1] + (s[3] - s[1]) * t) + (random.nextFloat() - 0.5f) * 6f
                val r = (2f + random.nextFloat() * 3.5f) * scale.coerceAtLeast(0.7f)
                for (y in floor(cy - r).toInt()..ceil(cy + r).toInt()) {
                    for (x in floor(cx - r).toInt()..ceil(cx + r).toInt()) {
                        val dx = x + 0.5f - cx
                        val dy = y + 0.5f - cy
                        if (dx * dx + dy * dy >= r * r) continue
                        val t2 = PixelKit.threshold(x, y)
                        // Lit from above: lighter on top, darker underneath, dithered between.
                        val shade = (dy / r + 1f) / 2f
                        PixelKit.put(still, cols, rows, x, y, when {
                            shade < 0.35f && t2 > shade -> light
                            shade > 0.7f && t2 < shade - 0.3f -> dark
                            else -> blossom
                        })
                    }
                }
            }
        }
    }

    /** A line from ([x0], [y0]) to ([x1], [y1]) tapering from [r0] to [r1] pixels across. */
    private fun stroke(x0: Float, y0: Float, x1: Float, y1: Float, r0: Float, r1: Float, color: Int) {
        val steps = max(1, max(abs(x1 - x0), abs(y1 - y0)).roundToInt())
        for (k in 0..steps) {
            val t = k / steps.toFloat()
            val x = x0 + (x1 - x0) * t
            val y = y0 + (y1 - y0) * t
            val r = (r0 + (r1 - r0) * t) / 2f
            for (py in floor(y - r).toInt()..ceil(y + r).toInt()) {
                for (px in floor(x - r).toInt()..ceil(x + r).toInt()) {
                    val dx = px + 0.5f - x
                    val dy = py + 0.5f - y
                    if (dx * dx + dy * dy <= r * r + 0.25f) PixelKit.put(still, cols, rows, px, py, color)
                }
            }
        }
    }

    private companion object {
        const val PETAL_FRAME_MS = 125L
        const val CYCLE_MS = 108_000L
        const val EVENING = 0.35f
        const val BLACK = 0xFF000000.toInt()
        const val WHITE = 0xFFFFFFFF.toInt()
        const val PINK = 0xFFFFB7D5.toInt()

        fun wrap(value: Double, span: Double): Double = ((value % span) + span) % span
    }
}
