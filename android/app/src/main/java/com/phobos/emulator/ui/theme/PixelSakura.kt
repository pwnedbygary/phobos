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

/**
 * The Sakura cycle's clock: a day and a night, [LENGTH] milliseconds long, in the order of [Phase]. The sun
 * is up from its rising to its setting and the moon from its rising to its setting, never both, each
 * climbing out of the horizon top first and sinking below it again. Between them the sky turns: from the
 * sunset to full night before the moon rises, and from night to dawn before the sun does. Plain math, so
 * the timing can be tested.
 */
internal object SakuraCycle {
    private const val RISE_MS = 24_000L
    private const val UP_MS = 132_000L
    private const val TURN_MS = 60_000L

    /** A body's run: its rising, its time up and its setting. */
    private const val RUN_MS = RISE_MS + UP_MS + RISE_MS

    enum class Phase(val length: Long) {
        /** The sun climbing out of the horizon, from its glow's top touching it to its bottom clearing it. */
        SUNRISE(RISE_MS),
        DAY(UP_MS),
        /** The sun sinking, from its bottom touching the horizon to its glow's top going below it. */
        SUNSET(RISE_MS),
        /** Neither sun nor moon, while the sky darkens from the sunset to full night. */
        DUSK(TURN_MS),
        MOONRISE(RISE_MS),
        NIGHT(UP_MS),
        MOONSET(RISE_MS),
        /** Neither sun nor moon, while the sky lightens from night to dawn. */
        DAWN(TURN_MS),
    }

    /** The skies the cycle passes through. */
    enum class Sky { DAWN, DAY, SUNSET, NIGHT }

    val LENGTH: Long = Phase.entries.sumOf { it.length }

    /** The share of a body's run it spends rising, and again setting. */
    private const val RISING = RISE_MS.toFloat() / RUN_MS

    /** The arc's height in radii: its climb starts at the rising's speed, one diameter in [RISING] of the run. */
    val ARC: Float = 2f * (1f - 2f * RISING) / (PI.toFloat() * RISING)

    fun start(phase: Phase): Long = Phase.entries.takeWhile { it != phase }.sumOf { it.length }

    fun phase(time: Long): Phase {
        var t = Math.floorMod(time, LENGTH)
        for (phase in Phase.entries) {
            if (t < phase.length) return phase
            t -= phase.length
        }
        return Phase.DAWN
    }

    /** How far the sun is from its rising (0) to its setting (1) at [time], or null while it's down. */
    fun sunRun(time: Long): Float? = run(time, Phase.SUNRISE)

    /** How far the moon is from its rising (0) to its setting (1) at [time], or null while it's down. */
    fun moonRun(time: Long): Float? = run(time, Phase.MOONRISE)

    private fun run(time: Long, rising: Phase): Float? {
        val t = Math.floorMod(time, LENGTH) - start(rising)
        return if (t in 0..RUN_MS) t.toFloat() / RUN_MS else null
    }

    /**
     * How high a body's middle is above the horizon, in its radii, [run] of the way from its rising to its
     * setting: from -1, its top on the horizon, up a diameter at a steady speed, then along an arc that
     * climbs on at that speed, peaks halfway at 1 + [ARC] and comes down as it went up.
     */
    fun height(run: Float): Float {
        val u = run.coerceIn(0f, 1f)
        val edge = min(u, 1f - u)
        return if (edge <= RISING) -1f + 2f * edge / RISING
        else 1f + ARC * sin(PI * (u - RISING) / (1f - 2f * RISING)).toFloat()
    }

    /**
     * The row a body of [radius], its glow reaching [reach] beyond it, is drawn around (a pixel corner) [run] of
     * the way through, over a horizon whose first hidden row is [horizon]: lower by up to [reach] while it rises
     * and sets, so its glow comes up out of the horizon with it and goes back down with it.
     */
    fun middleRow(run: Float, radius: Float, reach: Float, horizon: Int): Int {
        val h = height(run)
        val up = ((h + 1f) / 2f).coerceIn(0f, 1f)
        return (horizon - h * radius + reach * (1f - up * up * (3f - 2f * up))).roundToInt()
    }

    /** The rows of a disc of [radius] around a pixel corner on row [middle] that show above a horizon whose first hidden row is [horizon]. */
    fun shownRows(middle: Int, radius: Float, horizon: Int): IntRange {
        val reach = sqrt(radius * radius - 0.25f)
        val top = floor(middle - 0.5f - reach).toInt() + 1
        return top until min(horizon, 2 * middle - top)
    }

    /** How light it is at [time], from 0 (full night) to 1 (full day). */
    fun daylight(time: Long): Float = between(time) { a, b, x -> a.daylight + (b.daylight - a.daylight) * x }

    /** The skies either side of [time] and how far it is from the first to the second, eased. */
    fun sky(time: Long): Triple<Sky, Sky, Float> = between(time) { a, b, x -> Triple(a.sky, b.sky, x) }

    private class Key(val time: Long, val sky: Sky, val daylight: Float)

    /** The sky and the light along the cycle, each eased into the next. */
    private val KEYS = listOf(
        Key(0L, Sky.DAWN, 0.7f),
        Key(start(Phase.DAY) + 12_000L, Sky.DAY, 1f),
        Key(start(Phase.SUNSET) - 24_000L, Sky.DAY, 1f),
        Key(start(Phase.DUSK), Sky.SUNSET, 0.6f),
        Key(start(Phase.MOONRISE), Sky.NIGHT, 0f),
        Key(start(Phase.DAWN), Sky.NIGHT, 0f),
        Key(LENGTH, Sky.DAWN, 0.7f),
    )

    private inline fun <T> between(time: Long, value: (Key, Key, Float) -> T): T {
        val t = Math.floorMod(time, LENGTH)
        val i = KEYS.indexOfLast { it.time <= t }
        val a = KEYS[i]
        val b = KEYS[i + 1]
        val x = (t - a.time).toFloat() / (b.time - a.time)
        return value(a, b, x * x * (3f - 2f * x))
    }
}

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

    private val nightTop = palette.background
    private val dayTop = PixelKit.mix(palette.tint(palette.secondary, 0.72f), 0xFF58A9E8.toInt(), 0.5f)
    private val nightBottom = palette.tint(palette.primary, 0.3f)
    private val dayBottom = PixelKit.mix(palette.tint(palette.onBackground, 0.45f), 0xFFBFE8FF.toInt(), 0.6f)

    /** The first row the far ridge covers in every column: the cycle's horizon, below which the sun and moon don't show. */
    private val horizon = ridgeTop(FAR_BASE, FAR_HEIGHT, 0f)

    /** What the cycle's landscape was last painted for, so it's painted again only when that changes. */
    private var painted: Moment? = null

    /** Each petal: start column and row, fall and drift in pixels a second, sway in pixels, sway period in ms, phase. */
    private val petals: Array<DoubleArray>

    init {
        when (mode) {
            SakuraMode.NIGHT -> paintStill(0f)
            SakuraMode.DAY -> paintStill(1f)
            SakuraMode.CYCLE -> paintCycle(STILL_MOMENT)
        }

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
        // Night and day keep the landscape init painted; only the cycle relights it.
        if (mode == SakuraMode.CYCLE) paintCycle(if (live) time else STILL_MOMENT)
        System.arraycopy(still, 0, out, 0, still.size)
        if (!live) return
        when (mode) {
            SakuraMode.NIGHT -> stars.twinkle(time, out, starColor)
            SakuraMode.DAY -> {}
            SakuraMode.CYCLE -> painted?.stars?.takeIf { it > 0 }?.let { stars.twinkle(time, out, starColor, it / 255f) }
        }
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

    /** The landscape at [time]: the sky, its stars, the sun or the moon, the land; no petals and no twinkling. */
    internal fun landscape(time: Long): IntArray {
        if (mode == SakuraMode.CYCLE) paintCycle(time)
        return still.copyOf()
    }

    /** Night's or day's still landscape, with the moon or the sun high in the sky. */
    private fun paintStill(daylight: Float) {
        PixelKit.sky(still, cols, 0, rows, PixelKit.mix(nightTop, dayTop, daylight), PixelKit.mix(nightBottom, dayBottom, daylight), bands = 7)
        if (daylight < 0.75f) stars.paint(still, starColor)
        if (mode == SakuraMode.DAY) paintSun(arcX(0.25f), arcY(0.25f), palette.tint(palette.tertiary, 0.95f), palette.tint(palette.onBackground, 0.8f))
        if (mode == SakuraMode.NIGHT) paintMoon(arcX(0.75f), arcY(0.75f))
        paintLand(daylight)
    }

    private fun arcX(phase: Float) = cols * (0.1f + 0.8f * phase)
    private fun arcY(phase: Float) = rows * (0.54f - 0.35f * sin(phase * PI).toFloat())

    /** What the cycle's landscape shows at a moment: [paintCycle] paints it again only when this changes. */
    private data class Moment(
        val skyTop: Int,
        val skyMiddle: Int,
        val skyBottom: Int,
        val light: Int,
        val stars: Int,
        val sunX: Int,
        val sunY: Int,
        val sun: Int,
        val moonX: Int,
        val moonY: Int,
    )

    /** The cycle's landscape at [time]: its sky, its stars as they come out, the sun or the moon on its way, lit to match. */
    private fun paintCycle(time: Long) {
        val (from, to, along) = SakuraCycle.sky(time)
        val (fromTop, fromMiddle, fromBottom) = skyColors(from)
        val (toTop, toMiddle, toBottom) = skyColors(to)
        val daylight = SakuraCycle.daylight(time)
        val sunRadius = min(cols, rows) * 0.1f
        val moonRadius = min(cols, rows) * 0.12f
        val sunRun = SakuraCycle.sunRun(time)?.takeIf { sunRadius >= 2f }
        val moonRun = SakuraCycle.moonRun(time)?.takeIf { moonRadius >= 2f }
        val warmth = sunRun?.let { 1f - ((SakuraCycle.height(it) - 1f) / (SakuraCycle.ARC * 0.5f)).coerceIn(0f, 1f) } ?: 0f
        val moment = Moment(
            PixelKit.mix(fromTop, toTop, along),
            PixelKit.mix(fromMiddle, toMiddle, along),
            PixelKit.mix(fromBottom, toBottom, along),
            (daylight * 255).roundToInt(),
            (((STARS_FROM - daylight) / STARS_FROM).coerceIn(0f, 1f) * 255).roundToInt(),
            sunRun?.let { (cols * (0.12f + 0.76f * it)).roundToInt() } ?: 0,
            sunRun?.let { SakuraCycle.middleRow(it, sunRadius, SUN_GLOW, horizon) } ?: 0,
            PixelKit.mix(palette.tint(palette.tertiary, 0.95f), SETTING_SUN, 0.6f * warmth * warmth),
            moonRun?.let { (cols * (0.12f + 0.76f * it)).roundToInt() } ?: 0,
            moonRun?.let { SakuraCycle.middleRow(it, moonRadius, MOON_GLOW, horizon) } ?: 0,
        )
        if (moment == painted) return
        painted = moment
        paintSky(moment.skyTop, moment.skyMiddle, moment.skyBottom)
        if (moment.stars > 0) stars.paint(still, starColor, moment.stars / 255f)
        if (sunRun != null) {
            val glow = PixelKit.mix(palette.tint(palette.onBackground, 0.8f), SETTING_SUN, 0.5f * warmth)
            paintSun(moment.sunX.toFloat(), moment.sunY.toFloat(), moment.sun, glow, clip = horizon, anchored = true)
        }
        if (moonRun != null) paintMoon(moment.moonX.toFloat(), moment.moonY.toFloat(), clip = horizon, anchored = true)
        paintLand(moment.light / 255f)
    }

    /** The top, the middle and the horizon's color of the cycle's [sky]. */
    private fun skyColors(sky: SakuraCycle.Sky): Triple<Int, Int, Int> = when (sky) {
        SakuraCycle.Sky.NIGHT -> PixelKit.mix(nightTop, nightBottom, 0.7f).let { Triple(nightTop, PixelKit.mix(nightTop, it, 0.5f), it) }
        SakuraCycle.Sky.DAY -> PixelKit.mix(dayTop, dayBottom, 0.7f).let { Triple(dayTop, PixelKit.mix(dayTop, it, 0.5f), it) }
        SakuraCycle.Sky.SUNSET -> Triple(
            PixelKit.mix(palette.tint(palette.primary, 0.35f), DUSK_VIOLET, 0.55f),
            PixelKit.mix(palette.tint(palette.primary, 0.75f), ROSE, 0.5f),
            PixelKit.mix(palette.tint(palette.tertiary, 0.9f), AMBER, 0.6f),
        )
        SakuraCycle.Sky.DAWN -> Triple(
            PixelKit.mix(dayTop, PERIWINKLE, 0.45f),
            PixelKit.mix(palette.tint(palette.secondary, 0.6f), PINK, 0.5f),
            PixelKit.mix(palette.tint(palette.tertiary, 0.75f), PEACH, 0.6f),
        )
    }

    /** A sky from [top] through [middle], halfway down to the horizon, to [bottom] at it, in hard bands dithered into each other. */
    private fun paintSky(top: Int, middle: Int, bottom: Int) {
        val half = horizon / 2
        PixelKit.sky(still, cols, 0, half, top, middle, bands = 4)
        PixelKit.sky(still, cols, half, horizon, middle, bottom, bands = 4)
        still.fill(bottom, horizon.coerceIn(0, rows) * cols, still.size)
    }

    /** The mountains, the hill, the pagoda and the branch, lit for [daylight]. */
    private fun paintLand(daylight: Float) {
        paintRidge(PixelKit.ridge(cols, seed + 1, 0.3f), FAR_BASE, FAR_HEIGHT, PixelKit.mix(palette.tint(palette.secondary, 0.28f), palette.tint(palette.secondary, 0.62f), daylight))
        paintRidge(PixelKit.ridge(cols, seed + 2, 0.45f), 0.86f, 0.12f, PixelKit.mix(palette.tint(palette.secondary, 0.18f), palette.tint(palette.secondary, 0.46f), daylight))
        val silhouette = PixelKit.mix(PixelKit.mix(palette.background, BLACK, if (palette.isDark) 0.45f else 0.35f), palette.tint(palette.secondary, 0.25f), daylight * 0.45f)
        paintHill(silhouette)
        paintPagoda(silhouette, PixelKit.mix(palette.tint(palette.tertiary, 0.85f), palette.tint(palette.tertiary, 0.35f), daylight))
        paintBranch(silhouette, seed)
    }

    /**
     * A full moon around ([cx], [cy]) with darker seas and a dithered glow, above row [clip]. [anchored] lays
     * the dithering out from the moon's middle rather than the picture's corner, so it keeps its look as it moves.
     */
    private fun paintMoon(cx: Float, cy: Float, clip: Int = rows, anchored: Boolean = false) {
        val moon = palette.tint(palette.onBackground, 0.9f)
        val shade = palette.tint(palette.onBackground, 0.7f)
        val glow = palette.tint(palette.onBackground, 0.3f)
        val r = min(cols, rows) * 0.12f
        if (r < 2f) return
        val ax = if (anchored) cx.toInt() else 0
        val ay = if (anchored) cy.toInt() else 0
        val random = Random(seed + 3)
        val maria = List(5) {
            Triple(cx + (random.nextFloat() - 0.5f) * r * 1.1f, cy + (random.nextFloat() - 0.5f) * r * 1.1f, r * (0.12f + 0.18f * random.nextFloat()))
        }
        for (y in max(0, (cy - r - MOON_GLOW - 1).toInt()) until min(clip, ceil(cy + r + MOON_GLOW + 1).toInt())) {
            for (x in max(0, (cx - r - MOON_GLOW - 1).toInt()) until min(cols, ceil(cx + r + MOON_GLOW + 1).toInt())) {
                val dx = x + 0.5f - cx
                val dy = y + 0.5f - cy
                val d = sqrt(dx * dx + dy * dy)
                val i = y * cols + x
                if (d < r) {
                    val dark = maria.any { (mx, my, mr) -> (x + 0.5f - mx).let { it * it } + (y + 0.5f - my).let { it * it } < mr * mr }
                    still[i] = if (dark && PixelKit.threshold(x - ax, y - ay) < 0.6f) shade else moon
                } else if (d < r + MOON_GLOW) {
                    val strength = 1f - (d - r) / MOON_GLOW
                    if (strength * 0.7f > PixelKit.threshold(x - ax, y - ay)) still[i] = PixelKit.mix(still[i], glow, 0.5f)
                }
            }
        }
    }

    /** The sun around ([cx], [cy]) with a dithered glow, above row [clip]; [anchored] as for [paintMoon]. */
    private fun paintSun(cx: Float, cy: Float, sun: Int, glow: Int, clip: Int = rows, anchored: Boolean = false) {
        val r = min(cols, rows) * 0.1f
        val ax = if (anchored) cx.toInt() else 0
        val ay = if (anchored) cy.toInt() else 0
        for (y in floor(cy - r - SUN_GLOW).toInt()..min(clip - 1, ceil(cy + r + SUN_GLOW).toInt())) for (x in floor(cx - r - SUN_GLOW).toInt()..ceil(cx + r + SUN_GLOW).toInt()) {
            val d = sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy))
            if (d < r) PixelKit.put(still, cols, rows, x, y, sun)
            else if (d < r + SUN_GLOW && PixelKit.threshold(x - ax, y - ay) < 0.4f) PixelKit.blend(still, cols, rows, x, y, glow, 0.35f)
        }
    }

    /** The top row of mountains standing on [base] (of the height), up to [height] (of the height) tall, at [level] of that. */
    private fun ridgeTop(base: Float, height: Float, level: Float): Int = (rows * base - rows * height * (0.3f + 0.7f * level)).toInt().coerceIn(0, rows)

    /** Mountains standing on [base] (of the height), up to [height] (of the height) tall, filled down to the bottom in [color]. */
    private fun paintRidge(ridge: FloatArray, base: Float, height: Float, color: Int) {
        for (x in 0 until cols) {
            val top = ridgeTop(base, height, ridge[x])
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
        const val FAR_BASE = 0.8f
        const val FAR_HEIGHT = 0.2f

        /** How far the sun's and the moon's dithered glows reach beyond their discs, in pixels. */
        const val SUN_GLOW = 3f
        const val MOON_GLOW = 5f

        /** The cycle's picture with Android's animations off: the sun touching the horizon as it sets. */
        val STILL_MOMENT = SakuraCycle.start(SakuraCycle.Phase.SUNSET) + 4_000L

        /** The light below which stars come out, the brightest first, all of them by full night. */
        const val STARS_FROM = 0.55f

        const val BLACK = 0xFF000000.toInt()
        const val WHITE = 0xFFFFFFFF.toInt()
        const val PINK = 0xFFFFB7D5.toInt()
        const val SETTING_SUN = 0xFFFF7B3A.toInt()
        const val DUSK_VIOLET = 0xFF3E2C6E.toInt()
        const val ROSE = 0xFFC2577E.toInt()
        const val AMBER = 0xFFFF9A4D.toInt()
        const val PERIWINKLE = 0xFF7C83C9.toInt()
        const val PEACH = 0xFFFFCB9A.toInt()

        fun wrap(value: Double, span: Double): Double = ((value % span) + span) % span
    }
}
