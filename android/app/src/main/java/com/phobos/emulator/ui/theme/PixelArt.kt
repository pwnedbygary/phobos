package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import com.phobos.emulator.data.PixelBackdropScene
import kotlin.math.PI
import kotlin.math.floor
import kotlin.math.sin
import kotlin.random.Random

/**
 * An animated pixel backdrop of [cols] by [rows] art pixels. [compose] draws the picture at a moment
 * into an opaque ARGB array, one element per art pixel; [hold] says how long the picture stays as it
 * is from that moment, so the backdrop only redraws when something has moved.
 */
internal abstract class PixelArt(val cols: Int, val rows: Int) {
    /**
     * The picture at [time], in milliseconds on a steady clock, into [out]. Without [live], the still
     * picture, as it shows when Android's animations are off: nothing in flight and nothing twinkling.
     */
    abstract fun compose(time: Long, out: IntArray, live: Boolean = true)

    /** How long the picture at [time] stays as it is, in milliseconds. */
    abstract fun hold(time: Long): Long

    companion object {
        /** A frame's length at 60 Hz: what [hold] gives while something is in flight. */
        const val FRAME_MS = 16L

        fun of(scene: PixelBackdropScene, cols: Int, rows: Int, scheme: ColorScheme, isDark: Boolean): PixelArt = when (scene) {
            PixelBackdropScene.SAKURA_CYCLE -> SakuraArt(cols, rows, pixelPalette(scheme, isDark), SakuraMode.CYCLE)
            PixelBackdropScene.SAKURA_DAY -> SakuraArt(cols, rows, pixelPalette(scheme, isDark), SakuraMode.DAY)
            PixelBackdropScene.SAKURA_NIGHT -> SakuraArt(cols, rows, pixelPalette(scheme, isDark), SakuraMode.NIGHT)
            PixelBackdropScene.PLAINS -> PlainsArt(cols, rows, pixelPalette(scheme, isDark))
            PixelBackdropScene.NIGHT_DRIVE -> NightDriveArt(cols, rows, pixelPalette(scheme, isDark))
        }
    }
}

/** A theme's colors for the pixel backdrops, as opaque ARGB. */
internal data class PixelPalette(
    val background: Int,
    val onBackground: Int,
    val primary: Int,
    val secondary: Int,
    val tertiary: Int,
    val isDark: Boolean,
) {
    /** [color], [amount] of the way from the background; less far on light themes, which get a paler scene. */
    fun tint(color: Int, amount: Float): Int = PixelKit.mix(background, color, amount * if (isDark) 1f else 0.55f)
}

/** Drawing helpers the pixel backdrops share. */
internal object PixelKit {
    private val BAYER_4 = intArrayOf(0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)

    /** The 4x4 ordered-dither threshold at ([x], [y]), from 0 to 15/16. */
    fun threshold(x: Int, y: Int): Float = BAYER_4[(y and 3) * 4 + (x and 3)] / 16f

    /** Linear mix of two opaque ARGB colors, [t] toward [b]. */
    fun mix(a: Int, b: Int, t: Float): Int {
        val u = t.coerceIn(0f, 1f)
        fun channel(shift: Int) = ((a shr shift and 0xFF) * (1 - u) + (b shr shift and 0xFF) * u + 0.5f).toInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    /** A well-mixed hash of [x]. */
    fun hash(x: Int): Int {
        var h = x * 0x9E3779B9.toInt()
        h = h xor (h ushr 16)
        h *= 0x7FEB352D
        h = h xor (h ushr 15)
        h *= 0x846CA68B.toInt()
        return h xor (h ushr 16)
    }

    /** A number from 0 until 1, fixed by [seed] and [n]. */
    fun unit(seed: Int, n: Int): Float = (hash(seed * 0x2545F491 + n) ushr 8) / 16777216f

    /**
     * A sky from [top] to [bottom] down rows [from] until [to] of [out], in [bands] hard bands, the last
     * quarter of each dithering into the next with an ordered pattern.
     */
    fun sky(out: IntArray, cols: Int, from: Int, to: Int, top: Int, bottom: Int, bands: Int = 6) {
        val span = (to - from).coerceAtLeast(1)
        for (y in from until to) {
            val t = ((y - from) / span.toFloat()).coerceIn(0f, 1f) * (bands - 1)
            val band = floor(t).toInt()
            val within = t - band
            val next = (band + 1).coerceAtMost(bands - 1)
            for (x in 0 until cols) {
                val useNext = within > 0.75f && (within - 0.75f) * 4f > threshold(x, y)
                out[y * cols + x] = mix(top, bottom, (if (useNext) next else band) / (bands - 1f))
            }
        }
    }

    /** [color] mixed [amount] into the pixel at ([x], [y]), when it's in the picture. */
    fun blend(out: IntArray, cols: Int, rows: Int, x: Int, y: Int, color: Int, amount: Float) {
        if (x !in 0 until cols || y !in 0 until rows) return
        val index = y * cols + x
        out[index] = mix(out[index], color, amount)
    }

    /** [color] at ([x], [y]), when it's in the picture. */
    fun put(out: IntArray, cols: Int, rows: Int, x: Int, y: Int, color: Int) {
        if (x in 0 until cols && y in 0 until rows) out[y * cols + x] = color
    }

    /**
     * [sprite], rows of characters of the same length, at ([left], [top]): each character [colors]
     * maps is a pixel of that color, and any other leaves the picture as it is.
     */
    fun sprite(out: IntArray, cols: Int, rows: Int, sprite: List<String>, left: Int, top: Int, colors: Map<Char, Int>) {
        sprite.forEachIndexed { dy, line ->
            line.forEachIndexed { dx, c -> colors[c]?.let { put(out, cols, rows, left + dx, top + dy, it) } }
        }
    }

    /** A ridge line across the picture: for each column, a height from 0 to 1 that wanders smoothly. */
    fun ridge(cols: Int, seed: Int, roughness: Float): FloatArray {
        val random = Random(seed)
        val waves = List(4) { k -> Triple(random.nextFloat() * 6.283f, (k + 1) * (1.6f + random.nextFloat()), 1f / (k + 1)) }
        val total = waves.sumOf { it.third.toDouble() }.toFloat()
        return FloatArray(cols) { x ->
            val u = x / cols.coerceAtLeast(1).toFloat()
            var h = 0f
            for ((phase, frequency, weight) in waves) h += weight * sin(phase + u * frequency * 6.283f)
            val jag = (unit(seed, x) - 0.5f) * roughness
            (0.5f + 0.5f * h / total + jag).coerceIn(0f, 1f)
        }
    }
}

/**
 * Stars in the top [skyRows] rows: a few bright with a small cross, most dim. A few at a time twinkle,
 * brightening into a four-point sparkle and fading back.
 */
internal class StarField(private val cols: Int, private val rows: Int, skyRows: Int, count: Int, seed: Int) {
    private val seed = seed
    private val xs = IntArray(count)
    private val ys = IntArray(count)
    private val glow = FloatArray(count)

    init {
        val random = Random(seed)
        for (i in 0 until count) {
            xs[i] = random.nextInt(cols)
            ys[i] = random.nextInt(skyRows.coerceAtLeast(1))
            glow[i] = random.nextFloat()
        }
    }

    /** The stars at rest, in [color]: [shown] of them, from 0 to 1, the brightest first, as they come out at dusk. */
    fun paint(out: IntArray, color: Int, shown: Float = 1f) {
        for (i in xs.indices) {
            if (glow[i] < 1f - shown) continue
            val x = xs[i]
            val y = ys[i]
            when {
                glow[i] > 0.93f -> {
                    PixelKit.blend(out, cols, rows, x, y, color, 1f)
                    cross(out, x, y, 1, color, 0.45f)
                }
                glow[i] > 0.6f -> PixelKit.blend(out, cols, rows, x, y, color, 0.75f)
                else -> PixelKit.blend(out, cols, rows, x, y, color, 0.35f)
            }
        }
    }

    /** The stars twinkling at [time], over the stars at rest; only those of the [shown] that are out. */
    fun twinkle(time: Long, out: IntArray, color: Int, shown: Float = 1f) {
        if (xs.isEmpty()) return
        val slot = Math.floorDiv(time, TWINKLE_EVERY)
        for (k in slot - TWINKLE_LENGTH / TWINKLE_EVERY..slot) {
            val phase = (time - k * TWINKLE_EVERY) / TWINKLE_LENGTH.toFloat()
            if (phase < 0f || phase >= 1f) continue
            // Four brightness steps up and back down, as pixel art would draw it.
            val level = floor(sin(PI * phase).toFloat() * 4f) / 4f
            if (level <= 0f) continue
            val star = Math.floorMod(PixelKit.hash(k.toInt() * 31 + seed), xs.size)
            if (glow[star] < 1f - shown) continue
            val x = xs[star]
            val y = ys[star]
            PixelKit.blend(out, cols, rows, x, y, color, 0.4f + 0.6f * level)
            if (level >= 0.5f) cross(out, x, y, 1, color, 0.55f * level)
            if (level >= 1f) cross(out, x, y, 2, color, 0.3f)
        }
    }

    private fun cross(out: IntArray, x: Int, y: Int, reach: Int, color: Int, amount: Float) {
        PixelKit.blend(out, cols, rows, x + reach, y, color, amount)
        PixelKit.blend(out, cols, rows, x - reach, y, color, amount)
        PixelKit.blend(out, cols, rows, x, y + reach, color, amount)
        PixelKit.blend(out, cols, rows, x, y - reach, color, amount)
    }

    companion object {
        const val TWINKLE_EVERY = 600L

        /** Each of its eight brightness steps lasts about 300 ms, longer than the scenes' 250 ms between redraws. */
        const val TWINKLE_LENGTH = 2400L
    }
}
