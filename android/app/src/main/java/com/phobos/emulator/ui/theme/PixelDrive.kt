package com.phobos.emulator.ui.theme

import java.util.Arrays
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/**
 * The night drive backdrop: a road racing toward a striped sun setting between the mountains, its
 * stripes, curbs and verges rushing past in perspective, with a car out in front. The road redraws
 * 10 times a second; the sky, the sun and the mountains are a still picture.
 */
internal class NightDriveArt(cols: Int, rows: Int, palette: PixelPalette, seed: Int = 1982) : PixelArt(cols, rows) {
    private val horizon = (rows * 0.55f).toInt().coerceIn(0, rows)
    private val still = IntArray(cols * rows)

    private val verge = intArrayOf(palette.tint(palette.secondary, 0.26f), palette.tint(palette.secondary, 0.18f))
    private val curb = intArrayOf(palette.tint(palette.primary, 0.9f), palette.tint(palette.onBackground, 0.62f))
    private val asphalt = intArrayOf(palette.tint(palette.onBackground, 0.13f), palette.tint(palette.onBackground, 0.1f))
    private val lane = palette.tint(palette.onBackground, 0.6f)
    private val showCar = cols >= 80 && rows - horizon >= 24
    private val carColors = mapOf(
        'k' to PixelKit.mix(palette.background, BLACK, 0.5f),
        'g' to palette.tint(palette.secondary, 0.35f),
        'b' to palette.tint(palette.primary, 0.75f),
        'r' to PixelKit.mix(palette.tint(palette.primary, 0.9f), TAIL_LIGHT, if (palette.isDark) 0.7f else 0.45f),
        'p' to palette.tint(palette.onBackground, 0.7f),
        'w' to PixelKit.mix(palette.background, BLACK, 0.65f),
    )

    init {
        PixelKit.sky(still, cols, 0, horizon, palette.background, palette.tint(palette.primary, 0.5f), bands = 7)
        StarField(cols, rows, (horizon * 0.55f).toInt(), cols * horizon / 300, seed).paint(still, palette.tint(palette.onBackground, 0.7f))
        paintSun(palette.tint(palette.tertiary, 0.95f), palette.tint(palette.primary, 0.95f))
        paintRidge(PixelKit.ridge(cols, seed + 1, 0.2f), rows * 0.12f, palette.tint(palette.secondary, 0.3f))
        paintRidge(PixelKit.ridge(cols, seed + 2, 0.4f), rows * 0.06f, palette.tint(palette.secondary, 0.17f))
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        System.arraycopy(still, 0, out, 0, horizon * cols)
        val now = if (live) time else 0L
        paintRoad(now, out)
        if (showCar) {
            // The engine's rumble: the car bobs a pixel now and then.
            val bob = if (live && Math.floorMod(Math.floorDiv(now, 160L), 2L) == 1L) 1 else 0
            PixelKit.sprite(out, cols, rows, CAR, cols / 2 - CAR[0].length / 2, rows - CAR.size - 2 - bob, carColors)
        }
    }

    override fun hold(time: Long): Long = ROAD_FRAME_MS - Math.floorMod(time, ROAD_FRAME_MS)

    /** A sun on the horizon in bands of [top] to [low], cut by stripes that widen toward the horizon. */
    private fun paintSun(top: Int, low: Int) {
        val r = min(cols * 0.17f, horizon * 0.8f)
        if (r < 3f) return
        val cx = cols * 0.5f
        val base = horizon.toFloat()
        val bands = 4
        for (y in max(0, (base - r).toInt()) until horizon) {
            val rise = base - (y + 0.5f)
            val v = rise / r
            if (v < 0.55f) {
                val period = max(3f, r * 0.16f)
                if (((base - y) % period) / period < (0.55f - v) * 1.2f) continue
            }
            val half = sqrt(max(0f, r * r - rise * rise))
            val t = (1f - v).coerceIn(0f, 1f) * (bands - 1)
            val band = floor(t).toInt()
            val within = t - band
            for (x in max(0, (cx - half).toInt()) until min(cols, ceil(cx + half).toInt())) {
                if (abs(x + 0.5f - cx) >= half) continue
                val next = within > 0.75f && (within - 0.75f) * 4f > PixelKit.threshold(x, y)
                still[y * cols + x] = PixelKit.mix(top, low, (if (next) band + 1 else band).coerceAtMost(bands - 1) / (bands - 1f))
            }
        }
    }

    /** Mountains along the horizon, up to [height] rows tall, in [color]. */
    private fun paintRidge(ridge: FloatArray, height: Float, color: Int) {
        for (x in 0 until cols) {
            val top = (horizon - height * (0.3f + 0.7f * ridge[x])).toInt().coerceIn(0, horizon)
            for (y in top until horizon) still[y * cols + x] = color
        }
    }

    /** The road at [time]: each row is a slice of the road at its distance, striped by where it falls. */
    private fun paintRoad(time: Long, out: IntArray) {
        val span = (rows - horizon).toFloat()
        if (span <= 0f) return
        val scroll = Math.floorMod(time, LOOP_MS) / 1000.0 * SPEED
        for (y in horizon until rows) {
            val depth = y - horizon + 1f
            val s = depth / span
            val half = cols * (0.012f + 0.47f * s)
            val center = cols * (0.5f + 0.13f * (1f - s) * (1f - s))
            val curbWidth = half * 0.15f
            val laneHalf = max(0.5f, half * 0.016f)
            val row = y * cols
            // Far off, a stripe would be thinner than a row and shimmer; the two shades dither instead.
            if (SEGMENT * depth * depth / CAMERA < 1f) {
                for (x in 0 until cols) {
                    val p = (x + y) and 1
                    val off = abs(x + 0.5f - center)
                    out[row + x] = when {
                        off >= half + curbWidth -> verge[p]
                        off >= half -> curb[p]
                        p == 0 && off < laneHalf -> lane
                        else -> asphalt[p]
                    }
                }
                continue
            }
            // Nearer, a row is seven spans: verge, curb, asphalt with the lane down the middle on light
            // stripes, curb and verge, each pixel by its center's distance from the road's.
            val p = Math.floorMod(floor((CAMERA / depth + scroll) / SEGMENT).toLong(), 2L).toInt()
            fun leftOf(distance: Float) = (floor(center - distance - 0.5f).toInt() + 1).coerceIn(0, cols)
            fun rightOf(distance: Float) = ceil(center + distance - 0.5f).toInt().coerceIn(0, cols)
            val curbStart = leftOf(half + curbWidth)
            val roadStart = leftOf(half).coerceAtLeast(curbStart)
            val roadEnd = rightOf(half).coerceAtLeast(roadStart)
            val curbEnd = rightOf(half + curbWidth).coerceAtLeast(roadEnd)
            Arrays.fill(out, row, row + curbStart, verge[p])
            Arrays.fill(out, row + curbStart, row + roadStart, curb[p])
            Arrays.fill(out, row + roadStart, row + roadEnd, asphalt[p])
            Arrays.fill(out, row + roadEnd, row + curbEnd, curb[p])
            Arrays.fill(out, row + curbEnd, row + cols, verge[p])
            if (p == 0) {
                val laneStart = (floor(center - laneHalf - 0.5f).toInt() + 1).coerceIn(roadStart, roadEnd)
                val laneEnd = ceil(center + laneHalf - 0.5f).toInt().coerceIn(laneStart, roadEnd)
                Arrays.fill(out, row + laneStart, row + laneEnd, lane)
            }
        }
    }

    internal companion object {
        const val ROAD_FRAME_MS = 100L
        private const val CAMERA = 1000f
        private const val SEGMENT = 1f

        /** Stripes passing a second; the scroll wraps after [LOOP_MS], an even number of stripes. */
        private const val SPEED = 5.0
        private const val LOOP_MS = 400_000L
        private const val BLACK = 0xFF000000.toInt()
        private const val TAIL_LIGHT = 0xFFFF3B3B.toInt()

        /** The car from behind: k outline, g glass, b body, r tail lights, p plate, w tires. */
        val CAR = listOf(
            "..........kkkkkkkkkkkkkkkk..........",
            "........kkggggggggggggggggkk........",
            ".......kggggggggggggggggggggk.......",
            "......kggggggggggggggggggggggk......",
            ".....kbbbbbbbbbbbbbbbbbbbbbbbbk.....",
            "...kkbbbbbbbbbbbbbbbbbbbbbbbbbbkk...",
            "..kbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbk..",
            ".kbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbk.",
            "kbrrrrrbbbbbbbbbbbbbbbbbbbbbbrrrrrbk",
            "kbrrrrrbbbbbbbkppppppkbbbbbbbrrrrrbk",
            "kbbbbbbbbbbbbbkppppppkbbbbbbbbbbbbbk",
            ".kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk.",
            "..wwwww......................wwwww..",
            "..wwwww......................wwwww..",
        )
    }
}
