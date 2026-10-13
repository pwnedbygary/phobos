package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.MangaBackdropScene
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/**
 * The manga ink backdrops, drawn into an ARGB array: [ink] marks on [paper], never denser than [ink]
 * itself ([mangaTone]), so the panels' ink lines keep their contrast over every one of them.
 *
 * - Screentone: a halftone of dots on a 45-degree grid, swelling from nothing a third of the way down to
 *   dense at the bottom along a gently wavy edge, cut by two clusters of scratched highlights.
 * - Speed lines: focus lines, hundreds of tapering wedges converging on a clear space just above the
 *   middle, their inner ends ragged, a few of them thick.
 * - Splash page: an impact burst, a spiky flash of paper with an inked edge and short focus lines inside
 *   it, cut out of a field of screentone that darkens toward the edges.
 *
 * Plain Kotlin, drawn once per size and theme; the same size, colors and seed always give the same picture.
 */
internal object MangaPage {
    fun render(width: Int, height: Int, paper: Int, ink: Int, scene: MangaBackdropScene, seed: Int = 1952): IntArray {
        val w = width.coerceAtLeast(1)
        val h = height.coerceAtLeast(1)
        val coverage = FloatArray(w * h)
        val page = Page(w, h, Random(seed))
        when (scene) {
            MangaBackdropScene.TONE -> page.screentone(coverage)
            MangaBackdropScene.SPEED_LINES -> page.focusLines(coverage)
            MangaBackdropScene.SPLASH -> page.impact(coverage)
        }
        return IntArray(w * h) { PixelKit.mix(paper, ink, coverage[it]) }
    }
}

private class Page(val w: Int, val h: Int, val random: Random) {
    /** About a dp on a phone, from the page's shorter side. */
    private val unit = max(1f, min(w, h) / 420f)

    /** The screentone's dot pitch along its grid. */
    private val pitch = max(4f, unit * 7f)

    /** How much of a halftone cell's area is ink at ([x], [y]) for a tone of [density] (0 to 1), antialiased. */
    private fun dot(x: Int, y: Int, density: Float): Float {
        if (density <= 0f) return 0f
        // The grid turned 45 degrees: each dot's area grows with the density, until they touch and merge.
        val u = (x + y + 1) / SQRT2 / pitch
        val v = (x - y) / SQRT2 / pitch
        val radius = pitch * sqrt(density / PI.toFloat()) * 1.02f
        var best = 0f
        for (cu in floor(u).toInt()..floor(u).toInt() + 1) for (cv in floor(v).toInt()..floor(v).toInt() + 1) {
            val d = sqrt((u - cu) * (u - cu) + (v - cv) * (v - cv)) * pitch
            best = max(best, (radius - d + 0.5f).coerceIn(0f, 1f))
        }
        return best
    }

    fun screentone(out: FloatArray) {
        val scratches = scratches()
        for (y in 0 until h) for (x in 0 until w) {
            val fx = x / w.toFloat()
            val edge = 0.36f + 0.05f * sin(fx * 2f * PI.toFloat() * 1.3f + 0.7f) + 0.025f * sin(fx * 2f * PI.toFloat() * 3.1f + 2.1f)
            val t = ((y / h.toFloat() - edge) / (1f - edge)).coerceIn(0f, 1f)
            val density = 0.82f * t * t * (3f - 2f * t)
            out[y * w + x] = dot(x, y, density) * (1f - scratched(x, y, scratches))
        }
    }

    /** Scratched highlights: lines cut into the tone, each from ([0], [1]) to ([2], [3]), [4] wide and [5] long. */
    private fun scratches(): List<FloatArray> {
        val lines = mutableListOf<FloatArray>()
        repeat(2) { cluster ->
            val baseX = w * (0.18f + 0.48f * cluster + 0.12f * random.nextFloat())
            val baseY = h * (0.86f + 0.1f * random.nextFloat())
            val angle = -(0.55f + 0.2f * random.nextFloat())
            repeat(3 + random.nextInt(2)) { k ->
                val length = h * (0.25f + 0.35f * random.nextFloat())
                val x0 = baseX + k * unit * 9f
                val y0 = baseY + k * unit * 3f
                lines += floatArrayOf(x0, y0, x0 + cos(angle) * length, y0 + sin(angle) * length, unit * (1.2f + 1.4f * random.nextFloat()), length)
            }
        }
        return lines
    }

    /** How much of ([x], [y]) the scratches cut away, tapering toward each scratch's ends. */
    private fun scratched(x: Int, y: Int, lines: List<FloatArray>): Float {
        var cut = 0f
        for (line in lines) {
            val dx = line[2] - line[0]
            val dy = line[3] - line[1]
            val along = (((x + 0.5f - line[0]) * dx + (y + 0.5f - line[1]) * dy) / (dx * dx + dy * dy))
            if (along !in 0f..1f) continue
            val distance = abs((x + 0.5f - line[0]) * dy - (y + 0.5f - line[1]) * dx) / line[5]
            val half = line[4] / 2f * sin(PI.toFloat() * along)
            cut = max(cut, (half - distance + 0.5f).coerceIn(0f, 1f))
        }
        return cut
    }

    /** A focus line: its angle, its width at the page's farthest corner, and where its inner end is. */
    private class Ray(val angle: Float, val width: Float, val inner: Float)

    /** [count] rays, sorted by angle, up to [thickest] wide (most much thinner), their inner ends ragged around [clear]. */
    private fun rays(count: Int, clear: Float, thickest: Float): List<Ray> =
        List(count) {
            val angle = (2 * PI * (it + random.nextFloat() * 0.8f) / count).toFloat()
            val thick = random.nextFloat()
            Ray(angle, unit * 0.8f + thickest * thick * thick * thick, clear * (1f + 0.55f * random.nextFloat()))
        }.sortedBy { it.angle }

    /**
     * How much ink rays around ([cx], [cy]) lay on ([x], [y]): each a wedge from its inner end, where it's
     * a point, out to [reach], where it's its full width; only rays near enough to reach the pixel, at up to
     * [thickest] (as given to [rays]), are looked at.
     */
    private fun rayInk(x: Int, y: Int, cx: Float, cy: Float, rays: List<Ray>, angles: FloatArray, reach: Float, thickest: Float): Float {
        val dx = x + 0.5f - cx
        val dy = y + 0.5f - cy
        val rho = sqrt(dx * dx + dy * dy)
        if (rho < 1f) return 0f
        val window = ((unit * 0.8f + thickest) / 2f + 1f).div(rho).coerceAtMost(PI.toFloat())
        val from = atan2(dy, dx) - window
        var i = lowerBound(angles, from)
        var ink = 0f
        repeat(rays.size) {
            val ray = rays[i % rays.size]
            val turned = ((ray.angle - from) % TURN + TURN) % TURN
            if (turned > 2 * window) return ink
            if (rho > ray.inner) {
                val half = ray.width / 2f * ((rho - ray.inner) / (reach - ray.inner)).coerceIn(0f, 1f)
                ink = max(ink, (half - rho * sin(abs(turned - window)) + 0.5f).coerceIn(0f, 1f))
            }
            i++
        }
        return ink
    }

    /** The first index in sorted [angles] at or after [angle] (wrapped into 0 until a turn). */
    private fun lowerBound(angles: FloatArray, angle: Float): Int {
        val a = (angle % TURN + TURN) % TURN
        var lo = 0
        var hi = angles.size
        while (lo < hi) {
            val mid = (lo + hi) / 2
            if (angles[mid] < a) lo = mid + 1 else hi = mid
        }
        return lo
    }

    fun focusLines(out: FloatArray) {
        val cx = w * 0.5f
        val cy = h * 0.46f
        val reach = hypot(max(cx, w - cx), max(cy, h - cy))
        val clear = min(w, h) * 0.3f
        val thickest = unit * 7f
        val rays = rays(520, clear, thickest)
        val angles = FloatArray(rays.size) { rays[it].angle }
        for (y in 0 until h) for (x in 0 until w) out[y * w + x] = rayInk(x, y, cx, cy, rays, angles, reach, thickest)
    }

    fun impact(out: FloatArray) {
        val cx = w * 0.56f
        val cy = h * 0.46f
        val radius = min(w, h) * 0.3f
        // The burst's edge: spike tips and the valleys between them, around the circle.
        val points = 2 * (30 + random.nextInt(8))
        val edgeAngles = FloatArray(points) { (2 * PI * (it + 0.3f * random.nextFloat()) / points).toFloat() }
        val edgeRadii = FloatArray(points) { if (it % 2 == 0) radius * (1.2f + 0.75f * random.nextFloat() * random.nextFloat()) else radius * (0.82f + 0.12f * random.nextFloat()) }
        val line = unit * 2.2f
        val rays = rays(160, radius * 0.5f, unit * 3f)
        val angles = FloatArray(rays.size) { rays[it].angle }
        val far = hypot(max(cx, w - cx), max(cy, h - cy))
        for (y in 0 until h) for (x in 0 until w) {
            val dx = x + 0.5f - cx
            val dy = y + 0.5f - cy
            val rho = sqrt(dx * dx + dy * dy)
            var theta = atan2(dy, dx)
            if (theta < 0) theta += (2 * PI).toFloat()
            val edge = edgeAt(theta, edgeAngles, edgeRadii)
            // Distance across the edge, measured square to it where it slants.
            val across = (rho - edge.first) * edge.second
            val i = y * w + x
            out[i] = if (across > 0f) {
                // Outside the burst: screentone, denser toward the page's edges.
                val density = 0.24f + 0.34f * ((rho - radius) / (far - radius)).coerceIn(0f, 1f)
                max(dot(x, y, density) * (across / (line * 3f)).coerceIn(0f, 1f), (line / 2f - abs(across) + 0.5f).coerceIn(0f, 1f))
            } else {
                max((line / 2f - abs(across) + 0.5f).coerceIn(0f, 1f), rayInk(x, y, cx, cy, rays, angles, edge.first, unit * 3f) * 0.85f)
            }
        }
    }

    /** The burst's edge at [theta]: its radius there, and how square to the radius it runs (the cosine of its slant). */
    private fun edgeAt(theta: Float, angles: FloatArray, radii: FloatArray): Pair<Float, Float> {
        val n = angles.size
        // The last edge point at or before theta (-1: before the first, so between the last and the first).
        var lo = 0
        var hi = n
        while (lo < hi) {
            val mid = (lo + hi) / 2
            if (angles[mid] <= theta) lo = mid + 1 else hi = mid
        }
        val k = lo - 1
        val a0 = if (k < 0) angles[n - 1] - (2 * PI).toFloat() else angles[k]
        val r0 = radii[(k + n) % n]
        val a1 = if (k + 1 >= n) angles[0] + (2 * PI).toFloat() else angles[k + 1]
        val r1 = radii[(k + 1) % n]
        val t = ((theta - a0) / (a1 - a0)).coerceIn(0f, 1f)
        val r = r0 + (r1 - r0) * t
        // Along the edge, the radius changes by (r1 - r0) over an arc of r * (a1 - a0).
        val slope = (r1 - r0) / (r * (a1 - a0)).coerceAtLeast(1e-3f)
        return r to (1f / sqrt(1f + slope * slope))
    }

    private companion object {
        val SQRT2 = sqrt(2f)
        const val TURN = (2 * PI).toFloat()
    }
}
