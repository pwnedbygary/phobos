package com.phobos.emulator.ui.theme

import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Lettering in a [TrueType] font, rasterized to coverage for the manga backdrops: glyphs set along a
 * baseline, turned and slanted like hand lettering, then filled by their signed area per pixel ([Raster]).
 */
internal class Lettering(val font: TrueType) {
    /** [glyph] on the page: font units (y up) to page pixels by x' = a x + c y + e, y' = b x + d y + f. */
    class Placed(val glyph: Int, val a: Float, val b: Float, val c: Float, val d: Float, val e: Float, val f: Float)

    private fun glyphs(text: String): IntArray = IntArray(text.length) { font.glyph(text[it].code) }

    /** Each glyph's pen position along the line, in ems, and the line's width last; [tracking] ems added between letters. */
    private fun pens(glyphs: IntArray, tracking: Float): FloatArray {
        val pens = FloatArray(glyphs.size + 1)
        var pen = 0f
        for (i in glyphs.indices) {
            pens[i] = pen
            var advance = font.advance(glyphs[i]).toFloat()
            if (i + 1 < glyphs.size) advance += font.kerning(glyphs[i], glyphs[i + 1]) + tracking * font.unitsPerEm
            pen += advance / font.unitsPerEm
        }
        pens[glyphs.size] = pen
        return pens
    }

    /** How tall the capitals stand, in ems. */
    val capHeight: Float = (font.contours(font.glyph('H'.code)).maxOfOrNull { it.y.max() } ?: (font.unitsPerEm * 0.7f)) / font.unitsPerEm

    /** How wide [text] sets at [size] pixels to the em. */
    fun width(text: String, size: Float, tracking: Float = 0f): Float {
        val glyphs = glyphs(text)
        return pens(glyphs, tracking)[glyphs.size] * size
    }

    /** The size [text] sets [width] pixels wide at. */
    fun fit(text: String, width: Float, tracking: Float = 0f): Float = width / max(1e-3f, width(text, 1f, tracking))

    /**
     * [lines] one under another at [sizes] pixels to the em, [gap] of a capital's height apart, the block's
     * capitals centered on ([cx], [cy]); each line centered, or from the block's left side when [left] is
     * set (the block [left] wide); turned by [angle] and slanted by [slant] as in [line].
     */
    fun block(lines: List<String>, sizes: List<Float>, cx: Float, cy: Float, angle: Float = 0f, slant: Float = 0f, gap: Float = 0.3f, tracking: Float = 0f, left: Float? = null): List<Placed> {
        val caps = sizes.map { it * capHeight }
        val total = caps.sum() + gap * caps.max() * (lines.size - 1)
        val cos = cos(angle)
        val sin = sin(angle)
        val out = ArrayList<Placed>()
        var top = -total / 2f
        for ((k, text) in lines.withIndex()) {
            val down = top + caps[k]
            val width = width(text, sizes[k], tracking)
            val along = if (left != null) -left / 2f else -width / 2f
            out += line(text, sizes[k], cx + cos * along - sin * down, cy + sin * along + cos * down, angle, slant, tracking)
            top = down + gap * caps.max()
        }
        return out
    }

    /**
     * [text] at [size] pixels to the em on a baseline starting at ([x], [y]), turned by [angle] radians
     * (clockwise on the page), its letters slanted by [slant] (sideways per unit up); [grow] scales each
     * letter from 1 at the first to [grow] at the last, along the baseline, like lettering rushing closer.
     */
    fun line(text: String, size: Float, x: Float, y: Float, angle: Float = 0f, slant: Float = 0f, tracking: Float = 0f, grow: Float = 1f): List<Placed> {
        val glyphs = glyphs(text)
        val pens = pens(glyphs, tracking)
        val cos = cos(angle)
        val sin = sin(angle)
        val unit = 1f / font.unitsPerEm
        val out = ArrayList<Placed>(glyphs.size)
        // With letters growing, each one's pen moves by its own size.
        var along = 0f
        for (i in glyphs.indices) {
            val t = if (glyphs.size > 1) i / (glyphs.size - 1f) else 0f
            val s = size * (1f + (grow - 1f) * t) * unit
            val px = x + cos * along
            val py = y + sin * along
            // Font x along the baseline, font y up (against the page's y), slanted.
            out += Placed(glyphs[i], cos * s, sin * s, (slant * cos + sin) * s, (slant * sin - cos) * s, px, py)
            along += (pens[i + 1] - pens[i]) * s * font.unitsPerEm
        }
        return out
    }

    /** The coverage of [placed] on the page, with [margin] pixels of room around it; null when nothing's inked. */
    fun mask(placed: List<Placed>, margin: Int = 0): Mask? {
        var minX = Float.MAX_VALUE
        var minY = Float.MAX_VALUE
        var maxX = -Float.MAX_VALUE
        var maxY = -Float.MAX_VALUE
        val outlines = placed.map { it to font.contours(it.glyph) }
        for ((p, contours) in outlines) for (contour in contours) for (k in contour.x.indices) {
            val x = p.a * contour.x[k] + p.c * contour.y[k] + p.e
            val y = p.b * contour.x[k] + p.d * contour.y[k] + p.f
            minX = min(minX, x)
            maxX = max(maxX, x)
            minY = min(minY, y)
            maxY = max(maxY, y)
        }
        if (minX > maxX || !(maxX - minX < MAX_SIDE && maxY - minY < MAX_SIDE)) return null
        val left = floor(minX).toInt() - margin - 2
        val top = floor(minY).toInt() - margin - 2
        val raster = Raster(ceil(maxX).toInt() + margin + 3 - left, ceil(maxY).toInt() + margin + 3 - top)
        for ((p, contours) in outlines) for (contour in contours) {
            raster.contour(contour) { x, y, out ->
                out[0] = p.a * x + p.c * y + p.e - left
                out[1] = p.b * x + p.d * y + p.f - top
            }
        }
        return Mask(left, top, raster.width, raster.height, raster.coverage())
    }

    private companion object {
        /** No lettering is set larger than this many pixels across. */
        const val MAX_SIDE = 16384f
    }
}

/** Lettering in the font file [bytes], or null when they can't be read as a TrueType font. */
internal fun letteringOf(bytes: ByteArray): Lettering? =
    try {
        Lettering(TrueType(bytes))
    } catch (e: RuntimeException) {
        null
    }

/** Coverage on part of the page: [alpha] for the [width] by [height] pixels from ([left], [top]). */
internal class Mask(val left: Int, val top: Int, val width: Int, val height: Int, val alpha: FloatArray) {
    /**
     * How far each of the mask's pixels is from the inked shape, in pixels: 0 at and inside its edge. An
     * exact Euclidean distance transform between pixel centers, seeded by the edge pixels' coverage, so it's
     * within about half a pixel of the true distance: an outline drawn at a distance follows the shape, but
     * along a shallow slope its width steps by a pixel.
     */
    fun distances(): FloatArray {
        val far = 1e12
        val field = DoubleArray(width * height) {
            val c = alpha[it]
            when {
                c >= 0.5f -> 0.0
                c > 0f -> (0.5 - c) * (0.5 - c)
                else -> far
            }
        }
        val n = max(width, height)
        val line = DoubleArray(n)
        val result = DoubleArray(n)
        val hull = IntArray(n)
        val bounds = DoubleArray(n + 1)
        for (x in 0 until width) {
            for (y in 0 until height) line[y] = field[y * width + x]
            transform(line, height, result, hull, bounds)
            for (y in 0 until height) field[y * width + x] = result[y]
        }
        for (y in 0 until height) {
            for (x in 0 until width) line[x] = field[y * width + x]
            transform(line, width, result, hull, bounds)
            for (x in 0 until width) field[y * width + x] = result[x]
        }
        return FloatArray(width * height) { sqrt(field[it]).toFloat() }
    }

    /** One row or column of the squared distance transform: the lower envelope of parabolas over [f] (Felzenszwalb and Huttenlocher). */
    private fun transform(f: DoubleArray, n: Int, out: DoubleArray, hull: IntArray, bounds: DoubleArray) {
        if (n == 0) return
        var k = 0
        hull[0] = 0
        bounds[0] = Double.NEGATIVE_INFINITY
        bounds[1] = Double.POSITIVE_INFINITY
        for (q in 1 until n) {
            var s = meet(f, hull[k], q)
            while (s <= bounds[k]) {
                k--
                s = meet(f, hull[k], q)
            }
            k++
            hull[k] = q
            bounds[k] = s
            bounds[k + 1] = Double.POSITIVE_INFINITY
        }
        k = 0
        for (q in 0 until n) {
            while (bounds[k + 1] < q) k++
            val v = hull[k]
            out[q] = (q - v).toDouble() * (q - v) + f[v]
        }
    }

    /** Where the parabolas rooted at [v] and [q] (v < q) cross. */
    private fun meet(f: DoubleArray, v: Int, q: Int): Double =
        ((f[q] + q.toDouble() * q) - (f[v] + v.toDouble() * v)) / (2.0 * (q - v))
}

/**
 * Coverage by signed area: each edge adds, to the cells of every row it crosses, the share of the row it
 * winds past, and a row's running sum is then how much of each pixel is inside (overlaps capped at full).
 *
 * A modified Kotlin translation of font-rs's rasterizer (Copyright 2015 Google Inc., Apache License 2.0;
 * see LICENSE): [line] is `draw_line` in its `src/raster.rs` and [coverage] is `accumulate` in its
 * `src/accumulate.rs`, changed to start each row's sum afresh, clamp edges to the raster and take the
 * sum's rounding residue as no ink; [quad] flattens a curve by the same estimate as its `draw_quad`.
 */
private class Raster(val width: Int, val height: Int) {
    private val cells = FloatArray(width * height)

    /** Fills [contour], each of its points mapped to the raster by [map] (into its out array: x, y). */
    inline fun contour(contour: TrueType.Contour, map: (Float, Float, FloatArray) -> Unit) {
        val n = contour.x.size
        if (n < 2) return
        // Implied points between two off-curve ones, so on- and off-curve points alternate at most.
        val xs = ArrayList<Float>(n * 2)
        val ys = ArrayList<Float>(n * 2)
        val on = ArrayList<Boolean>(n * 2)
        val point = FloatArray(2)
        for (i in 0 until n) {
            map(contour.x[i], contour.y[i], point)
            xs += point[0]
            ys += point[1]
            on += contour.on[i]
            val next = (i + 1) % n
            if (!contour.on[i] && !contour.on[next]) {
                map((contour.x[i] + contour.x[next]) / 2f, (contour.y[i] + contour.y[next]) / 2f, point)
                xs += point[0]
                ys += point[1]
                on += true
            }
        }
        val m = xs.size
        val start = on.indexOf(true)
        if (start < 0) return
        var x = xs[start]
        var y = ys[start]
        var j = 1
        while (j <= m) {
            val k = (start + j) % m
            if (on[k]) {
                line(x, y, xs[k], ys[k])
                x = xs[k]
                y = ys[k]
                j++
            } else {
                val e = (start + j + 1) % m
                quad(x, y, xs[k], ys[k], xs[e], ys[e])
                x = xs[e]
                y = ys[e]
                j += 2
            }
        }
    }

    /** A quadratic curve, flattened to within a tenth of a pixel. */
    fun quad(x0: Float, y0: Float, cx: Float, cy: Float, x1: Float, y1: Float) {
        val ddx = x0 - 2 * cx + x1
        val ddy = y0 - 2 * cy + y1
        val steps = max(1, ceil(sqrt(sqrt(ddx * ddx + ddy * ddy) * 2.5f)).toInt()).coerceAtMost(64)
        var px = x0
        var py = y0
        for (s in 1..steps) {
            val t = s / steps.toFloat()
            val u = 1 - t
            val x = u * u * x0 + 2 * u * t * cx + t * t * x1
            val y = u * u * y0 + 2 * u * t * cy + t * t * y1
            line(px, py, x, y)
            px = x
            py = y
        }
    }

    fun line(x0: Float, y0: Float, x1: Float, y1: Float) {
        if (y0 == y1) return
        val dir = if (y0 < y1) 1f else -1f
        val ax = if (y0 < y1) x0 else x1
        val ay = min(y0, y1)
        val bx = if (y0 < y1) x1 else x0
        val by = max(y0, y1)
        val dxdy = (bx - ax) / (by - ay)
        var row = max(0, floor(ay).toInt())
        var x = ax + (row - ay).coerceAtLeast(0f) * dxdy
        val last = min(height, ceil(by).toInt())
        // Every point is at least a pixel inside the raster's sides, so a row's cells never spill over.
        val limit = width - 2f
        while (row < last) {
            val start = row * width
            val dy = min(row + 1f, by) - max(row.toFloat(), ay)
            val next = x + dxdy * dy
            val d = dy * dir
            val left = min(x, next).coerceIn(0f, limit)
            val right = max(x, next).coerceIn(0f, limit)
            val l = left.toInt()
            val r = ceil(right).toInt()
            if (r <= l + 1) {
                val middle = (left + right) / 2f - l
                cells[start + l] += d * (1f - middle)
                cells[start + l + 1] += d * middle
            } else {
                // The edge crosses several pixels: the area to its right in each, as a ramp across them.
                val s = 1f / (right - left)
                val lf = left - l
                val first = 0.5f * s * (1f - lf) * (1f - lf)
                val rf = right - r + 1f
                val lastShare = 0.5f * s * rf * rf
                cells[start + l] += d * first
                if (r == l + 2) {
                    cells[start + l + 1] += d * (1f - first - lastShare)
                } else {
                    val second = s * (1.5f - lf)
                    cells[start + l + 1] += d * (second - first)
                    for (xi in l + 2 until r - 1) cells[start + xi] += d * s
                    val before = second + (r - l - 3) * s
                    cells[start + r - 1] += d * (1f - before - lastShare)
                }
                cells[start + r] += d * lastShare
            }
            x = next
            row++
        }
    }

    fun coverage(): FloatArray {
        val out = FloatArray(width * height)
        for (y in 0 until height) {
            var sum = 0f
            for (x in 0 until width) {
                val i = y * width + x
                sum += cells[i]
                // A row's crossings cancel out only to within rounding: what's left past them isn't ink.
                val area = kotlin.math.abs(sum)
                out[i] = if (area < RESIDUE) 0f else min(1f, area)
            }
        }
        return out
    }

    private companion object {
        const val RESIDUE = 1f / 512
    }
}
