package com.phobos.emulator.ui.theme

import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/** The pixel backdrop's colors as ARGB, derived from the theme (see [pixelSceneColors]). */
internal data class PixelSceneColors(
    val skyTop: Int,
    val skyBottom: Int,
    val star: Int,
    val planetLit: Int,
    val planetShade: Int,
    val crater: Int,
    val rim: Int,
    val glow: Int,
)

/**
 * The pixel-art backdrop as a small ARGB image, one element per art pixel, meant to be scaled up
 * without filtering: a sky in hard bands with ordered dithering between them, stars in the upper
 * sky, and the limb of a planet along the bottom with a lit side, a shaded side, craters and a
 * bright rim with a dithered glow. The same size, colors and seed always give the same image.
 */
internal object PixelScene {
    private const val SKY_BANDS = 6

    /** Where the planet's top is, as a fraction of the height. */
    const val HORIZON = 0.74f

    private val BAYER_4 = intArrayOf(0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)

    fun render(cols: Int, rows: Int, colors: PixelSceneColors, seed: Int = 1977): IntArray {
        require(cols > 0 && rows > 0)
        val pixels = IntArray(cols * rows)
        val random = Random(seed)
        val horizonRow = (rows * HORIZON).toInt()
        val radius = cols * 1.1f
        val cx = cols * 0.56f
        val cy = horizonRow + radius

        for (y in 0 until rows) {
            val t = (y / horizonRow.toFloat()).coerceIn(0f, 1f) * (SKY_BANDS - 1)
            val band = floor(t).toInt()
            val within = t - band
            for (x in 0 until cols) {
                val next = (band + 1).coerceAtMost(SKY_BANDS - 1)
                // The last quarter of each band dithers into the next with a 4x4 ordered pattern.
                val threshold = BAYER_4[(y and 3) * 4 + (x and 3)] / 16f
                val useNext = within > 0.75f && (within - 0.75f) * 4f > threshold
                val level = if (useNext) next else band
                pixels[y * cols + x] = mix(colors.skyTop, colors.skyBottom, level / (SKY_BANDS - 1f))
            }
        }

        val starCount = cols * rows / 170
        repeat(starCount) {
            val x = random.nextInt(cols)
            val y = random.nextInt((horizonRow * 0.85f).toInt().coerceAtLeast(1))
            val brightness = random.nextFloat()
            when {
                brightness > 0.93f -> {
                    plot(pixels, cols, rows, x, y, colors.star, 1f)
                    for ((dx, dy) in listOf(1 to 0, -1 to 0, 0 to 1, 0 to -1)) plot(pixels, cols, rows, x + dx, y + dy, colors.star, 0.45f)
                }
                brightness > 0.6f -> plot(pixels, cols, rows, x, y, colors.star, 0.75f)
                else -> plot(pixels, cols, rows, x, y, colors.star, 0.35f)
            }
        }

        val craters = List(14) {
            val angle = random.nextFloat() * 1.3f - 0.65f
            val depth = 3f + random.nextFloat() * (rows - horizonRow) * 0.8f
            val r = 1.5f + random.nextFloat() * cols * 0.045f
            Triple(cx + (radius - depth) * sin(angle), cy - (radius - depth) * cos(angle), r)
        }
        for (y in (horizonRow - 4).coerceAtLeast(0) until rows) {
            for (x in 0 until cols) {
                val dx = x + 0.5f - cx
                val dy = y + 0.5f - cy
                val d = sqrt(dx * dx + dy * dy)
                val index = y * cols + x
                when {
                    d < radius - 1.2f -> {
                        // Lit from the upper left: the shaded side grows toward the right, dithered at the terminator.
                        val shadeT = ((x - cx * 0.95f) / (cols * 0.55f)).coerceIn(0f, 1f)
                        val threshold = BAYER_4[(y and 3) * 4 + (x and 3)] / 16f
                        var color = if (shadeT > threshold) colors.planetShade else colors.planetLit
                        for ((ccx, ccy, cr) in craters) {
                            val ex = x + 0.5f - ccx
                            val ey = (y + 0.5f - ccy) * 1.6f
                            val e = ex * ex + ey * ey
                            if (e < cr * cr) color = colors.crater
                            else if (e < (cr + 1f) * (cr + 1f) && ey > 0f) color = mix(color, colors.rim, 0.35f)
                        }
                        pixels[index] = color
                    }
                    d < radius -> pixels[index] = colors.rim
                    d < radius + 3.5f -> {
                        val strength = 1f - (d - radius) / 3.5f
                        val threshold = BAYER_4[(y and 3) * 4 + (x and 3)] / 16f
                        if (strength * 0.8f > threshold) pixels[index] = mix(pixels[index], colors.glow, 0.45f)
                    }
                }
            }
        }
        return pixels
    }

    private fun plot(pixels: IntArray, cols: Int, rows: Int, x: Int, y: Int, color: Int, amount: Float) {
        if (x !in 0 until cols || y !in 0 until rows) return
        val index = y * cols + x
        pixels[index] = mix(pixels[index], color, amount)
    }

    /** Linear mix of two opaque ARGB colors, [t] toward [b]. */
    fun mix(a: Int, b: Int, t: Float): Int {
        val u = t.coerceIn(0f, 1f)
        fun channel(shift: Int) = ((a shr shift and 0xFF) * (1 - u) + (b shr shift and 0xFF) * u + 0.5f).toInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }
}
