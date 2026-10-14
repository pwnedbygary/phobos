package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.MangaBackdropScene
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.ceil
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
 * - Magazine cover: a weekly shonen magazine's cover announcing Phobos ([cover]).
 * - Truck-kun: the isekai truck charging out of focus lines, headlights blazing ([truck]).
 *
 * Plain Kotlin, drawn once per size and theme; the same size, colors and seed always give the same picture.
 * The two lettered scenes letter with [lettering] (without it, they leave the words out).
 */
internal object MangaPage {
    fun render(width: Int, height: Int, paper: Int, ink: Int, scene: MangaBackdropScene, lettering: Lettering? = null, seed: Int = 1952): IntArray {
        val w = width.coerceAtLeast(1)
        val h = height.coerceAtLeast(1)
        val coverage = FloatArray(w * h)
        val page = Page(w, h, Random(seed), luma(ink) > luma(paper))
        when (scene) {
            MangaBackdropScene.TONE -> page.screentone(coverage)
            MangaBackdropScene.SPEED_LINES -> page.focusLines(coverage)
            MangaBackdropScene.COVER -> page.cover(coverage, lettering)
            MangaBackdropScene.TRUCK -> page.truck(coverage, lettering)
        }
        return IntArray(w * h) { PixelKit.mix(paper, ink, coverage[it]) }
    }

    private fun luma(color: Int) = 0.299f * (color shr 16 and 0xFF) + 0.587f * (color shr 8 and 0xFF) + 0.114f * (color and 0xFF)
}

/**
 * A page being inked: [w] by [h] pixels of coverage, 0 paper to 1 ink. [inkIsLight] on dark themes, where
 * the ink is the lighter of the two, so a light (a headlight, a glint, light caught on a rim) and form
 * shading ([shade]) are drawn the right way round; lines, lettering and panels stay ink on paper.
 */
internal class Page(val w: Int, val h: Int, val random: Random, val inkIsLight: Boolean) {
    /** About a dp on a phone, from the page's shorter side. */
    val unit = max(1f, min(w, h) / 420f)

    /** The screentone's dot pitch along its grid. */
    val pitch = max(4f, unit * 7f)

    /** A finer screentone's pitch, for shading drawn things. */
    val fine = max(3f, unit * 4.4f)

    /** The coverage that reads as light: ink on a dark theme, paper on a light one. */
    val light: Float get() = if (inkIsLight) 1f else 0f

    /**
     * A tone at ([x], [y]) that looks [darkness] dark, 0 lit to 1 in shadow, whichever way round the theme
     * is: on a light theme the shadows get the dots, on a dark one (light ink) the lit parts, up to [LIT].
     */
    fun shade(x: Int, y: Int, darkness: Float): Float = if (inkIsLight) dot(x, y, (1f - darkness) * LIT, fine) else dot(x, y, darkness, fine)

    /** How much of a halftone cell's area is ink at ([x], [y]) for a tone of [density] (0 to 1), antialiased. */
    fun dot(x: Int, y: Int, density: Float, pitch: Float = this.pitch): Float {
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

    /** [value] (0 paper to 1 ink) laid over [out] at [i], [alpha] of the way. */
    fun paint(out: FloatArray, i: Int, alpha: Float, value: Float) {
        if (alpha > 0f) out[i] += (value - out[i]) * min(alpha, 1f)
    }

    /** Every pixel from ([left], [top]) to ([right], [bottom]) that's on the page: its x, y and index. */
    inline fun box(left: Float, top: Float, right: Float, bottom: Float, body: (x: Int, y: Int, i: Int) -> Unit) {
        if (!(left <= right && top <= bottom)) return
        val x0 = max(0f, floor(left)).toInt()
        val x1 = min(w - 1f, ceil(right)).toInt()
        val y0 = max(0f, floor(top)).toInt()
        val y1 = min(h - 1f, ceil(bottom)).toInt()
        for (y in y0..y1) for (x in x0..x1) body(x, y, y * w + x)
    }

    /** Every page pixel the part of [frame] from ([left], [top]) to ([right], [bottom]) covers, with its point in the frame (x, y and pixels per unit there). */
    inline fun box(frame: Frame, left: Float, top: Float, right: Float, bottom: Float, body: (x: Int, y: Int, i: Int, local: FloatArray) -> Unit) {
        val corners = FloatArray(3)
        var minX = Float.MAX_VALUE
        var minY = Float.MAX_VALUE
        var maxX = -Float.MAX_VALUE
        var maxY = -Float.MAX_VALUE
        for ((lx, ly) in listOf(left to top, right to top, left to bottom, right to bottom)) {
            frame.toPage(lx, ly, corners)
            minX = min(minX, corners[0])
            maxX = max(maxX, corners[0])
            minY = min(minY, corners[1])
            maxY = max(maxY, corners[1])
        }
        val local = FloatArray(3)
        box(minX - 2f, minY - 2f, maxX + 2f, maxY + 2f) { x, y, i ->
            frame.toLocal(x + 0.5f, y + 0.5f, local)
            body(x, y, i, local)
        }
    }

    /**
     * Lettering as a logo: [mask]'s letters filled by [fill], ringed with paper [ring] wide and then an ink
     * line [line] wide, over a drop shadow of the whole [shadowX], [shadowY] pixels away in a tone of [shadow].
     */
    fun logo(out: FloatArray, mask: Mask, ring: Float, line: Float, shadowX: Int, shadowY: Int, shadow: Float, fill: (x: Int, y: Int) -> Float) {
        val distance = mask.distances()
        val outer = ring + line
        for (my in 0 until mask.height) for (mx in 0 until mask.width) {
            val x = mask.left + mx
            val y = mask.top + my
            if (x !in 0 until w || y !in 0 until h) continue
            val i = y * w + x
            val k = my * mask.width + mx
            val sx = mx - shadowX
            val sy = my - shadowY
            if (shadow > 0f && sx in 0 until mask.width && sy in 0 until mask.height) {
                val behind = (outer - distance[sy * mask.width + sx] + 0.5f).coerceIn(0f, 1f)
                if (behind > 0f) paint(out, i, behind, dot(x, y, shadow))
            }
            paint(out, i, (outer - distance[k] + 0.5f).coerceIn(0f, 1f), 1f)
            paint(out, i, (ring - distance[k] + 0.5f).coerceIn(0f, 1f), 0f)
            if (mask.alpha[k] > 0f) paint(out, i, mask.alpha[k], fill(x, y))
        }
    }

    /** [mask]'s letters in [value]. */
    fun letters(out: FloatArray, mask: Mask, value: Float) {
        for (my in 0 until mask.height) for (mx in 0 until mask.width) {
            val x = mask.left + mx
            val y = mask.top + my
            if (x !in 0 until w || y !in 0 until h) continue
            paint(out, y * w + x, mask.alpha[my * mask.width + mx], value)
        }
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
    class Ray(val angle: Float, val width: Float, val inner: Float)

    /** [count] rays, sorted by angle, up to [thickest] wide (most much thinner), their inner ends ragged around [clear]. */
    fun rays(count: Int, clear: Float, thickest: Float): List<Ray> =
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
    fun rayInk(x: Int, y: Int, cx: Float, cy: Float, rays: List<Ray>, angles: FloatArray, reach: Float, thickest: Float): Float {
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

    /**
     * An impact burst's edge around a circle of [radius]: [points] spike tips and the valleys between them,
     * as angles and radii, the tips out to [spike] radii (unevenly), the valleys in to [valley].
     */
    fun burstEdge(points: Int, radius: Float, spike: Float, valley: Float): Pair<FloatArray, FloatArray> {
        val n = 2 * points
        val angles = FloatArray(n) { (2 * PI * (it + 0.3f * random.nextFloat()) / n).toFloat() }
        val radii = FloatArray(n) {
            if (it % 2 == 0) radius * (1f + (spike - 1f) * (0.45f + 0.55f * random.nextFloat() * random.nextFloat()))
            else radius * (valley + 0.08f * random.nextFloat())
        }
        return angles to radii
    }

    /** The burst's edge at [theta]: its radius there, and how square to the radius it runs (the cosine of its slant). */
    fun edgeAt(theta: Float, angles: FloatArray, radii: FloatArray): Pair<Float, Float> {
        val n = angles.size
        // The last edge point at or before theta (-1: before the first, so between the last and the first).
        var lo = 0
        var hi = n
        while (lo < hi) {
            val mid = (lo + hi) / 2
            if (angles[mid] <= theta) lo = mid + 1 else hi = mid
        }
        val k = lo - 1
        val a0 = if (k < 0) angles[n - 1] - TURN else angles[k]
        val r0 = radii[(k + n) % n]
        val a1 = if (k + 1 >= n) angles[0] + TURN else angles[k + 1]
        val r1 = radii[(k + 1) % n]
        val t = ((theta - a0) / (a1 - a0)).coerceIn(0f, 1f)
        val r = r0 + (r1 - r0) * t
        // Along the edge, the radius changes by (r1 - r0) over an arc of r * (a1 - a0).
        val slope = (r1 - r0) / (r * (a1 - a0)).coerceAtLeast(1e-3f)
        return r to (1f / sqrt(1f + slope * slope))
    }

    /** How far ([x], [y]) is outside the burst around ([cx], [cy]) with the edge [angles] and [radii], square to its edge (negative inside). */
    fun burstDistance(x: Int, y: Int, cx: Float, cy: Float, angles: FloatArray, radii: FloatArray): Float {
        val dx = x + 0.5f - cx
        val dy = y + 0.5f - cy
        var theta = atan2(dy, dx)
        if (theta < 0) theta += TURN
        val (edge, square) = edgeAt(theta, angles, radii)
        return (sqrt(dx * dx + dy * dy) - edge) * square
    }

    companion object {
        val SQRT2 = sqrt(2f)
        const val TURN = (2 * PI).toFloat()

        /** The densest tone [shade] gives the lit parts of a dark theme's drawings. */
        const val LIT = 0.6f
    }
}

/** A box on the page, in pixels. */
internal class PageBox(val left: Float, val top: Float, val right: Float, val bottom: Float) {
    val width get() = right - left
    val height get() = bottom - top
    val cx get() = (left + right) / 2f
    val cy get() = (top + bottom) / 2f
}

/** A paper box with an ink line [line] wide round it, its corners square. */
internal fun Page.panel(out: FloatArray, box: PageBox, line: Float) {
    box(box.left - line, box.top - line, box.right + line, box.bottom + line) { x, y, i ->
        val d = roundBox(x + 0.5f - box.cx, y + 0.5f - box.cy, box.width / 2f, box.height / 2f, 0f)
        paint(out, i, inside(d), 0f)
        paint(out, i, onLine(d, line), 1f)
    }
}

/** A caption box: [lines] in ink, centered, as large as they fit in [box], on a [panel]. */
internal fun Page.caption(out: FloatArray, l: Lettering, box: PageBox, line: Float, lines: List<String>) {
    panel(out, box, line)
    val pad = min(box.width, box.height) * 0.14f
    val size = min(lines.minOf { l.fit(it, box.width - pad * 2, 0.02f) }, (box.height - pad * 2) / (l.capHeight * (lines.size + 0.3f * (lines.size - 1))))
    l.mask(l.block(lines, lines.map { size }, box.cx, box.cy, 0f, 0.05f, gap = 0.3f, tracking = 0.02f))?.let { letters(out, it, 1f) }
}

/**
 * A drawing's own coordinates: its unit is [scale] pixels at its origin, page point ([cx], [cy]); it's
 * turned [angle] radians clockwise, and seen in perspective, its lower half leaning toward the viewer, a
 * point at y = 1 nearer by [tilt] (1 / (1 - tilt) as large).
 */
internal class Frame(val cx: Float, val cy: Float, val scale: Float, val angle: Float, val tilt: Float = 0f) {
    private val cos = cos(angle)
    private val sin = sin(angle)

    /** How many pixels a unit spans across, at the frame's height [y]. */
    fun pixels(y: Float): Float = scale / max(0.05f, 1f - tilt * y)

    /** Page point ([x], [y]) in the frame: into [out], its x, y, and how many pixels a unit spans there. */
    fun toLocal(x: Float, y: Float, out: FloatArray) {
        val dx = x - cx
        val dy = y - cy
        val rx = (dx * cos + dy * sin) / scale
        val ry = (-dx * sin + dy * cos) / scale
        val ly = ry / max(0.05f, 1f + tilt * ry)
        val near = 1f / max(0.05f, 1f - tilt * ly)
        out[0] = rx / near
        out[1] = ly
        out[2] = scale * near * sqrt(near)
    }

    /** The frame's point ([x], [y]) on the page, into [out]: x, y. */
    fun toPage(x: Float, y: Float, out: FloatArray) {
        val near = 1f / max(0.05f, 1f - tilt * y)
        val rx = x * near * scale
        val ry = y * near * scale
        out[0] = cx + rx * cos - ry * sin
        out[1] = cy + rx * sin + ry * cos
    }
}

/** How much of a pixel a shape covers, from its signed distance to the shape's edge in pixels (negative inside). */
internal fun inside(distance: Float): Float = (0.5f - distance).coerceIn(0f, 1f)

/** How much of a pixel at [s] a band from [from] to [to] covers (all in pixels, across the band). */
internal fun band(s: Float, from: Float, to: Float): Float = (s - from + 0.5f).coerceIn(0f, 1f) * (to - s + 0.5f).coerceIn(0f, 1f)

/** How much of a pixel a line [width] pixels wide covers, [distance] pixels from its middle. */
internal fun onLine(distance: Float, width: Float): Float = (width / 2f - abs(distance) + 0.5f).coerceIn(0f, 1f)

/** Signed distance from ([x], [y]) to a box around the origin, [hx] and [hy] its half sides, its corners rounded by [r]. */
internal fun roundBox(x: Float, y: Float, hx: Float, hy: Float, r: Float): Float {
    val qx = abs(x) - hx + r
    val qy = abs(y) - hy + r
    return min(max(qx, qy), 0f) + hypot(max(qx, 0f), max(qy, 0f)) - r
}

/** Distance from ([x], [y]) to the segment from ([ax], [ay]) to ([bx], [by]). */
internal fun segment(x: Float, y: Float, ax: Float, ay: Float, bx: Float, by: Float): Float {
    val px = x - ax
    val py = y - ay
    val ex = bx - ax
    val ey = by - ay
    val t = ((px * ex + py * ey) / max(1e-6f, ex * ex + ey * ey)).coerceIn(0f, 1f)
    return hypot(px - ex * t, py - ey * t)
}

/** Signed distance from ([x], [y]) to the polygon through [xs], [ys] (negative inside, either winding). */
internal fun polygon(x: Float, y: Float, xs: FloatArray, ys: FloatArray): Float {
    var best = Float.MAX_VALUE
    var sign = 1f
    var j = xs.size - 1
    for (i in xs.indices) {
        val ex = xs[j] - xs[i]
        val ey = ys[j] - ys[i]
        val px = x - xs[i]
        val py = y - ys[i]
        val t = ((px * ex + py * ey) / max(1e-6f, ex * ex + ey * ey)).coerceIn(0f, 1f)
        val dx = px - ex * t
        val dy = py - ey * t
        best = min(best, dx * dx + dy * dy)
        val above = y >= ys[i]
        val below = y < ys[j]
        val left = ex * py > ey * px
        if ((above && below && left) || (!above && !below && !left)) sign = -sign
        j = i
    }
    return sign * sqrt(best)
}

/** 0 at or below [from], 1 at or past [to], eased between (either way round). */
internal fun smooth(from: Float, to: Float, x: Float): Float {
    val t = ((x - from) / (to - from)).coerceIn(0f, 1f)
    return t * t * (3f - 2f * t)
}
