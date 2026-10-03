package com.phobos.emulator.ui.theme

import kotlin.math.PI
import kotlin.math.asin
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/**
 * The space backdrop: Mars' limb along the bottom, turning craters, and Phobos orbiting through
 * the upper sky as a small D-pad moon. Stars twinkle and occasional shooting stars cross the scene.
 *
 * Across the face, craters are squashed as much as the still picture always drew them; only the last
 * few pixels below the limb squash them further, as a sphere would, so they stay visible right up to it.
 */
internal class SpaceArt(cols: Int, rows: Int, private val colors: PixelSceneColors, seed: Int = 1977) : PixelArt(cols, rows) {
    private val horizonRow = (rows * PixelScene.HORIZON).toInt()
    private val radius = cols * 1.1f
    private val cx = cols * 0.56f
    private val cy = horizonRow + radius

    /** The first row the planet and its glow reach. */
    private val top = (horizonRow - 4).coerceIn(0, rows)
    private val still = IntArray(cols * rows)
    private val stars = StarField(cols, rows, (horizonRow * 0.85f).toInt(), cols * rows / 170, seed)
    private val meteors = Meteors(cols, rows, (horizonRow * 0.8f).toInt(), seed)

    // The planet's rows at rest and as last drawn, and for each pixel on its face, where it is on the
    // sphere: the angle around the turning axis (NaN off the face) and the angle along it.
    private val rest = IntArray(cols * (rows - top))
    private val planet = IntArray(rest.size)
    private val theta = FloatArray(rest.size) { Float.NaN }
    private val lambda = FloatArray(rest.size)
    private var thetaLow = HALF_PI
    private var drawnStep = Long.MIN_VALUE

    private val craterTheta: FloatArray
    private val craterLambda: FloatArray
    private val craterRadius: FloatArray

    init {
        val horizon = horizonRow.coerceIn(0, rows)
        PixelKit.sky(still, cols, 0, horizon, colors.skyTop, colors.skyBottom)
        for (i in horizon * cols until still.size) still[i] = colors.skyBottom
        stars.paint(still, colors.star)

        var lambdaLow = Float.MAX_VALUE
        var lambdaHigh = -Float.MAX_VALUE
        for (y in top until rows) {
            for (x in 0 until cols) {
                val i = (y - top) * cols + x
                val dx = x + 0.5f - cx
                val dy = y + 0.5f - cy
                val d = sqrt(dx * dx + dy * dy)
                var color = still[y * cols + x]
                when {
                    d < radius - 1.2f -> {
                        // Lit from the upper left: the shaded side grows toward the right, dithered at the terminator.
                        val shadeT = ((x - cx * 0.95f) / (cols * 0.55f)).coerceIn(0f, 1f)
                        color = if (shadeT > PixelKit.threshold(x, y)) colors.planetShade else colors.planetLit
                        val depth = (y + 0.5f - limb(dx)).coerceAtLeast(0f)
                        theta[i] = thetaAt(depth)
                        lambda[i] = asin((dx / radius).coerceIn(-1f, 1f))
                        thetaLow = min(thetaLow, theta[i])
                        lambdaLow = min(lambdaLow, lambda[i])
                        lambdaHigh = max(lambdaHigh, lambda[i])
                    }
                    d < radius -> color = colors.rim
                    d < radius + 3.5f -> {
                        val strength = 1f - (d - radius) / 3.5f
                        if (strength * 0.8f > PixelKit.threshold(x, y)) color = PixelKit.mix(color, colors.glow, 0.45f)
                    }
                }
                rest[i] = color
            }
        }
        if (lambdaLow > lambdaHigh) {
            lambdaLow = 0f
            lambdaHigh = 0f
        }

        // Craters all the way around, across the band of the sphere the face shows.
        val random = Random(seed * 31 + 7)
        craterTheta = FloatArray(CRATERS) { random.nextFloat() * TAU - PI_F }
        craterLambda = FloatArray(CRATERS) { lambdaLow + random.nextFloat() * (lambdaHigh - lambdaLow) }
        craterRadius = FloatArray(CRATERS) { (1.5f + random.nextFloat() * cols * 0.045f) / radius }
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        val step = if (live) Math.floorDiv(time, PLANET_STEP_MS) else 0L
        if (step != drawnStep) {
            drawPlanet(step)
            drawnStep = step
        }
        System.arraycopy(still, 0, out, 0, top * cols)
        System.arraycopy(planet, 0, out, top * cols, planet.size)
        if (live) {
            stars.twinkle(time, out, colors.star)
            meteors.paint(time, out, colors.star)
        }
        paintPhobos(if (live) time else 0L, out)
    }

    override fun hold(time: Long): Long {
        if (meteors.flying(time)) return PixelArt.FRAME_MS
        val twinkle = TWINKLE_STEP_MS - Math.floorMod(time, TWINKLE_STEP_MS)
        val orbit = PHOBOS_STEP_MS - Math.floorMod(time, PHOBOS_STEP_MS)
        return min(min(twinkle, meteors.untilNext(time)), orbit).coerceAtLeast(1L)
    }

    /** Phobos follows an elliptical orbit while the D-pad glyph on its face slowly turns. */
    private fun paintPhobos(time: Long, out: IntArray) {
        val phase = Math.floorMod(time, PHOBOS_ORBIT_MS).toFloat() / PHOBOS_ORBIT_MS * TAU
        val cx = cols * 0.53f + cos(phase) * cols * 0.36f
        val cy = rows * 0.39f + sin(phase) * rows * 0.23f
        val r = min(cols, rows) * 0.055f
        if (r < 1f) return
        val body = PixelKit.mix(colors.planetLit, colors.star, 0.32f)
        val shade = PixelKit.mix(colors.planetShade, colors.crater, 0.35f)
        val dpad = PixelKit.mix(colors.crater, colors.skyTop, 0.4f)
        for (y in floor(cy - r).toInt()..ceil(cy + r).toInt()) for (x in floor(cx - r).toInt()..ceil(cx + r).toInt()) {
            val dx = x + 0.5f - cx
            val dy = y + 0.5f - cy
            if (dx * dx + dy * dy < r * r) PixelKit.put(out, cols, rows, x, y, if (dx > r * 0.25f || dy > r * 0.4f) shade else body)
        }
        val turn = Math.floorMod(time, PHOBOS_SPIN_MS).toFloat() / PHOBOS_SPIN_MS * TAU
        val arm = max(1, (r * 0.55f).toInt())
        val width = max(1, (r * 0.22f).toInt())
        for (v in -arm..arm) for (u in -width..width) {
            val rx = (u * cos(turn) - v * sin(turn)).roundToInt()
            val ry = (u * sin(turn) + v * cos(turn)).roundToInt()
            PixelKit.put(out, cols, rows, cx.roundToInt() + rx, cy.roundToInt() + ry, dpad)
            PixelKit.put(out, cols, rows, cx.roundToInt() - ry, cy.roundToInt() + rx, dpad)
        }
    }

    /** The row of the limb above the column [dx] from the planet's center. */
    private fun limb(dx: Float): Float = cy - sqrt(max(0f, radius * radius - dx * dx))

    /**
     * The angle around the turning axis [depth] pixels below the limb: [FLAT] times the angle per pixel
     * across, and up to four times that in the last few pixels, where the surface turns away.
     */
    private fun thetaAt(depth: Float): Float = HALF_PI - FLAT / radius * (depth + NEAR_PX * 3f * (1f - exp(-depth / NEAR_PX)))

    /** How far below the limb [theta] is: the inverse of [thetaAt], found by halving. */
    private fun depthAt(theta: Float): Float {
        var low = 0f
        var high = rows.toFloat() * 2f + 1f
        repeat(20) {
            val mid = (low + high) / 2f
            if (thetaAt(mid) > theta) low = mid else high = mid
        }
        return (low + high) / 2f
    }

    /** The planet turned to [step]: its face at rest, with the craters on the face at that turn. */
    private fun drawPlanet(step: Long) {
        System.arraycopy(rest, 0, planet, 0, rest.size)
        val turn = Math.floorMod(step * PLANET_STEP_MS, PLANET_TURN_MS) / PLANET_TURN_MS.toFloat() * TAU
        val rim = FLAT / radius
        for (c in craterTheta.indices) {
            val at = wrap(craterTheta[c] + turn)
            val r = craterRadius[c]
            if (at < thetaLow - r - rim || at > HALF_PI + r) continue
            val l = craterLambda[c]
            val cosL = cos(l)
            val sx = cx + sin(l) * radius
            val sy = limb(sx - cx) + depthAt(at.coerceAtMost(HALF_PI))
            val reach = radius * (r + rim) + 2f
            val x0 = floor(sx - reach).toInt().coerceAtLeast(0)
            val x1 = ceil(sx + reach).toInt().coerceAtMost(cols - 1)
            val y0 = floor(sy - reach).toInt().coerceAtLeast(top)
            val y1 = ceil(sy + reach).toInt().coerceAtMost(rows - 1)
            for (y in y0..y1) {
                for (x in x0..x1) {
                    val i = (y - top) * cols + x
                    val t = theta[i]
                    if (t.isNaN()) continue
                    val dt = wrap(t - at)
                    val dl = lambda[i] - l
                    val d2 = dt * dt * cosL * cosL + dl * dl
                    if (d2 < r * r) {
                        planet[i] = colors.crater
                    } else if (dt < 0f && d2 < (r + rim) * (r + rim)) {
                        // The rim's near side catches the light.
                        planet[i] = PixelKit.mix(planet[i], colors.rim, 0.35f)
                    }
                }
            }
        }
    }

    private companion object {
        val PI_F = PI.toFloat()
        val TAU = (2 * PI).toFloat()
        val HALF_PI = (PI / 2).toFloat()
        /** About 14 on the face at a time, as the still picture always had. */
        const val CRATERS = 300

        /** How much the face squashes craters, as the still picture drew them, and how near the limb it squashes them more. */
        const val FLAT = 1.6f
        const val NEAR_PX = 8f
        const val PLANET_STEP_MS = 500L

        /** A whole turn takes 18 minutes: a crater crosses the face in about a minute and a half. */
        const val PLANET_TURN_MS = 1_080_000L
        const val TWINKLE_STEP_MS = 250L
        const val PHOBOS_STEP_MS = 125L
        const val PHOBOS_ORBIT_MS = 38_000L
        const val PHOBOS_SPIN_MS = 12_000L

        fun wrap(angle: Float): Float {
            var a = angle % TAU
            if (a > PI_F) a -= TAU else if (a <= -PI_F) a += TAU
            return a
        }
    }
}
