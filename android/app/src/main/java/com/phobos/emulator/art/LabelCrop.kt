package com.phobos.emulator.art

import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Cuts the sticker out of a flat scan of a real cartridge (or takes the whole printed face of a disc).
 *
 * Port of Cartridge's `electron/labelcrop.js` - same fractions, aspect gates, region lists, background test,
 * edge snapping and final insets. Input is an RGBA pixel buffer (index i = ((y * width) + x) * 4, R G B A),
 * exactly what the JS Buffer holds; outputs are pixel rectangles.
 *
 * Fixtures: `tools/cart-fixtures/make-label-fixtures.mjs` (synthetic bitmaps) -> `labelcrop-fixtures.json`.
 */
object LabelCrop {

    data class Rect(val x: Int, val y: Int, val w: Int, val h: Int)

    /** [left, top, right, bottom] as fractions of the cart's bounding box. */
    val SPECS: Map<String, DoubleArray> = mapOf(
        "gb" to doubleArrayOf(0.12, 0.27, 0.885, 0.875),
        "gbc" to doubleArrayOf(0.12, 0.27, 0.885, 0.875),
        "gba" to doubleArrayOf(0.135, 0.225, 0.868, 0.875),
        "ds" to doubleArrayOf(0.09, 0.05, 0.91, 0.89),
        "3ds" to doubleArrayOf(0.08, 0.035, 0.878, 0.888),
        "switch" to doubleArrayOf(0.07, 0.06, 0.93, 0.78),
        "n64" to doubleArrayOf(0.26, 0.085, 0.735, 0.905),
        "snes" to doubleArrayOf(0.19, 0.0, 0.815, 0.448),
        "nes" to doubleArrayOf(0.4, 0.0, 0.862, 0.68),
        "umd" to doubleArrayOf(0.0, 0.0, 1.0, 1.0),
    )

    /** Width / height of each cart's outline, to reject photos, odd editions and scans with packaging. */
    val ASPECT: Map<String, Double> = mapOf(
        "gb" to 0.87, "gbc" to 0.87, "gba" to 1.734, "ds" to 0.943, "3ds" to 1.0, "switch" to 0.684,
        "n64" to 1.526, "snes" to 1.579, "nes" to 0.896, "disc" to 1.0, "umd" to 0.99,
    )

    /** Which scan regions have the same shell as our model (Famicom/SFC carts are shaped differently). */
    val REGIONS: Map<String, Set<String>> = mapOf(
        "snes" to setOf("US", "W", ""),
        "nes" to setOf("US", "EU", "AU", "W", ""),
    )

    private fun luminance(d: ByteArray, i: Int): Double =
        (d[i].toInt() and 0xff) * 0.11 + (d[i + 1].toInt() and 0xff) * 0.59 +
            (d[i + 2].toInt() and 0xff) * 0.3

    /** Bounding box of the object: alpha when the scan has it, otherwise anything differing from the corner colour. */
    fun objectBox(bmp: ByteArray, w: Int, h: Int): Rect {
        var transparent = false
        for ((x, y) in listOf(1 to 1, (w - 2) to 1, 1 to (h - 2), (w - 2) to (h - 2))) {
            if (bmp[(y * w + x) * 4 + 3].toInt() and 0xff < 200) transparent = true
        }
        val c0 = (2 * w + 2) * 4
        fun inside(i: Int): Boolean = if (transparent) {
            (bmp[i + 3].toInt() and 0xff) > 128
        } else {
            abs((bmp[i].toInt() and 0xff) - (bmp[c0].toInt() and 0xff)) +
                abs((bmp[i + 1].toInt() and 0xff) - (bmp[c0 + 1].toInt() and 0xff)) +
                abs((bmp[i + 2].toInt() and 0xff) - (bmp[c0 + 2].toInt() and 0xff)) > 48
        }
        var x0 = w
        var y0 = h
        var x1 = -1
        var y1 = -1
        val rowHits = IntArray(h)
        val colHits = IntArray(w)
        for (y in 0 until h) {
            for (x in 0 until w) {
                if (inside((y * w + x) * 4)) {
                    rowHits[y]++
                    colHits[x]++
                }
            }
        }
        // Ignore stray specks: a row/column must be at least 2% covered.
        for (y in 0 until h) if (rowHits[y] > w * 0.02) { y0 = min(y0, y); y1 = y }
        for (x in 0 until w) if (colHits[x] > h * 0.02) { x0 = min(x0, x); x1 = x }
        if (x1 < 0 || y1 < 0) return Rect(0, 0, w, h)
        return Rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1)
    }

    /** True when (x, y) is empty background (transparent, or the scan's backdrop colour from its corner). */
    fun isBackground(bmp: ByteArray, w: Int, h: Int, x: Int, y: Int): Boolean {
        val i = (max(0, min(h - 1, y)) * w + max(0, min(w - 1, x))) * 4
        if ((bmp[(w + 1) * 4 + 3].toInt() and 0xff) < 200) return (bmp[i + 3].toInt() and 0xff) < 128
        val c = (2 * w + 2) * 4
        return abs((bmp[i].toInt() and 0xff) - (bmp[c].toInt() and 0xff)) +
            abs((bmp[i + 1].toInt() and 0xff) - (bmp[c + 1].toInt() and 0xff)) +
            abs((bmp[i + 2].toInt() and 0xff) - (bmp[c + 2].toInt() and 0xff)) < 48
    }

    /** A UMD scan that shows only the round disc (not the caddy around it). */
    fun isRoundScan(bmp: ByteArray, w: Int, h: Int, box: Rect): Boolean {
        val pts = listOf(0.05 to 0.05, 0.95 to 0.05, 0.05 to 0.95, 0.95 to 0.95)
        val empty = pts.count { (fx, fy) ->
            isBackground(bmp, w, h, (box.x + fx * box.w).roundToInt(), (box.y + fy * box.h).roundToInt())
        }
        return empty >= 3
    }

    /** Mean gradient across a vertical ('v') or horizontal line at [pos], sampled over [a, b) along the line. */
    private fun lineEdge(bmp: ByteArray, w: Int, h: Int, dir: Char, pos: Int, a: Double, b: Double): Double {
        // Cartridge indexes its pixel buffer with the window's start; a fractional start reads undefined in
        // JavaScript, so every sample there is NaN and the snap falls back to its guess. Mirror that exactly
        // (a fractional end just limits the loop).
        if (a % 1.0 != 0.0) return Double.NaN
        var sum = 0.0
        var n = 0
        if (dir == 'v') {
            val x = max(1, min(w - 2, pos))
            var y = max(0, a.toInt())
            while (y < h && y.toDouble() < b) {
                sum += abs(luminance(bmp, (y * w + x + 1) * 4) - luminance(bmp, (y * w + x - 1) * 4))
                n++
                y += 2
            }
        } else {
            val y = max(1, min(h - 2, pos))
            var x = max(0, a.toInt())
            while (x < w && x.toDouble() < b) {
                sum += abs(luminance(bmp, ((y + 1) * w + x) * 4) - luminance(bmp, ((y - 1) * w + x) * 4))
                n++
                x += 2
            }
        }
        return if (n > 0) sum / n else 0.0
    }

    private fun snap(bmp: ByteArray, w: Int, h: Int, dir: Char, guess: Double, span: Double, a: Double, b: Double): Double {
        var best = guess
        var bestV = -1.0
        val vals = ArrayList<Double>()
        val from = (guess - span).roundToInt()
        val to = (guess + span).roundToInt()
        for (p in from..to) {
            val v = lineEdge(bmp, w, h, dir, p, a, b)
            vals.add(v)
            if (v > bestV) {
                bestV = v
                best = p.toDouble()
            }
        }
        vals.sort()
        val median = if (vals.isNotEmpty()) vals[vals.size / 2] else 0.0
        return if (bestV > max(12.0, median * 2.2)) best else guess
    }

    /** The crop rectangle in pixels, or null when this shape has no label spec or the scan's aspect is wrong. */
    fun labelRect(bmp: ByteArray, w: Int, h: Int, shapeKey: String): Rect? {
        val box = objectBox(bmp, w, h)
        val want = ASPECT[shapeKey] ?: return null
        if (want == 0.0 || abs(ln(box.w.toDouble() / box.h.toDouble() / want)) > 0.12) return null
        if (shapeKey == "umd") return box
        if (shapeKey == "disc") {
            val s = min(box.w, box.h)
            return Rect(
                box.x + ((box.w - s) / 2.0).roundToInt(),
                box.y + ((box.h - s) / 2.0).roundToInt(),
                s,
                s,
            )
        }
        val spec = SPECS[shapeKey] ?: return null
        val (l, t, r, b) = spec
        var x0 = box.x + l * box.w
        var x1 = box.x + r * box.w
        var y0 = box.y + t * box.h
        var y1 = box.y + b * box.h
        val sx = box.w * 0.03
        val sy = box.h * 0.03
        val midY0 = y0 + (y1 - y0) * 0.2
        val midY1 = y0 + (y1 - y0) * 0.8
        val midX0 = x0 + (x1 - x0) * 0.2
        val midX1 = x0 + (x1 - x0) * 0.8
        x0 = snap(bmp, w, h, 'v', x0, sx, midY0, midY1)
        x1 = snap(bmp, w, h, 'v', x1, sx, midY0, midY1)
        if (t > 0.01) y0 = snap(bmp, w, h, 'h', y0, sy, midX0, midX1)
        y1 = snap(bmp, w, h, 'h', y1, sy, midX0, midX1)
        // Stay just inside the printed edge.
        val inX = (x1 - x0) * 0.006
        val inY = (y1 - y0) * 0.006
        return Rect(
            (x0 + inX).roundToInt(),
            (y0 + inY).roundToInt(),
            max(8, (x1 - x0 - inX * 2).roundToInt()),
            max(8, (y1 - y0 - inY * 2).roundToInt()),
        )
    }
}
