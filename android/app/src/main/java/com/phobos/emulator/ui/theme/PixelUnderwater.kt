package com.phobos.emulator.ui.theme

import kotlin.math.floor
import kotlin.math.max
import kotlin.random.Random

/** An underwater reef with a rippling surface, growing coral, and bubble streams that rise in loops. */
internal class UnderwaterArt(cols: Int, rows: Int, private val palette: PixelPalette, seed: Int = 2001) : PixelArt(cols, rows) {
    private val still = IntArray(cols * rows)
    private val bubbles: Array<IntArray>
    private val water = palette.tint(palette.secondary, 0.58f)
    private val light = palette.tint(palette.onBackground, 0.65f)

    init {
        PixelKit.sky(still, cols, 0, rows, PixelKit.mix(water, 0xFF3799C6.toInt(), 0.5f), palette.tint(palette.primary, 0.28f))
        val sand = palette.tint(palette.tertiary, 0.45f)
        for (y in (rows * 0.8f).toInt() until rows) for (x in 0 until cols) PixelKit.put(still, cols, rows, x, y, sand)
        for (x in 0 until cols) PixelKit.put(still, cols, rows, x, (rows * 0.8f).toInt(), light)
        val random = Random(seed)
        repeat((cols / 28).coerceIn(3, 14)) {
            val x = random.nextInt(cols)
            val height = random.nextInt(max(3, rows / 12), max(4, rows / 4))
            val coral = if (it % 2 == 0) palette.tint(palette.primary, 0.78f) else palette.tint(palette.tertiary, 0.7f)
            for (dy in 0..height) {
                PixelKit.put(still, cols, rows, x, (rows * 0.8f).toInt() - dy, coral)
                if (dy > 2 && dy % 4 == 0) PixelKit.put(still, cols, rows, x + if (it % 3 == 0) 1 else -1, (rows * 0.8f).toInt() - dy, coral)
            }
        }
        bubbles = Array((cols * rows / 1800).coerceIn(5, 42)) {
            intArrayOf(random.nextInt(cols), random.nextInt(rows), 2 + random.nextInt(5), random.nextInt(1_000))
        }
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        System.arraycopy(still, 0, out, 0, still.size)
        val now = if (live) time else 0L
        for (x in 0 until cols) {
            val y = (1 + ((x + now / 180) % 9)).toInt()
            PixelKit.blend(out, cols, rows, x, y, light, 0.45f)
        }
        for (bubble in bubbles) {
            val y = Math.floorMod(bubble[1] * 10 + bubble[3] - (now / bubble[2]).toInt(), rows * 11) / 11
            val x = bubble[0] + ((now / 500 + bubble[3]) % 3).toInt() - 1
            PixelKit.put(out, cols, rows, x, y, light)
            PixelKit.put(out, cols, rows, x + 1, y, light)
            PixelKit.put(out, cols, rows, x, y + 1, light)
        }
    }

    override fun hold(time: Long): Long = PixelArt.FRAME_MS * 4 - Math.floorMod(time, PixelArt.FRAME_MS * 4)
}
