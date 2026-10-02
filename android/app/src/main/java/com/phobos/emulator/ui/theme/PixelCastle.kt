package com.phobos.emulator.ui.theme

import kotlin.math.max

/** A starry battlement above a brick wall, where wall torches cycle through chunky flame frames. */
internal class CastleArt(cols: Int, rows: Int, private val palette: PixelPalette, seed: Int = 1992) : PixelArt(cols, rows) {
    private val still = IntArray(cols * rows)
    private val stars = StarField(cols, rows, (rows * 0.22f).toInt(), (cols * rows / 450).coerceAtLeast(1), seed)
    private val torches = ArrayList<Pair<Int, Int>>()
    private val brick = palette.tint(palette.primary, 0.32f)
    private val mortar = palette.tint(palette.background, 0.75f)
    private val flame = palette.tint(palette.tertiary, 0.98f)

    init {
        PixelKit.sky(still, cols, 0, rows, palette.background, palette.tint(palette.primary, 0.15f))
        stars.paint(still, palette.tint(palette.onBackground, 0.75f))
        val wallTop = (rows * 0.2f).toInt()
        val brickH = max(3, rows / 25)
        for (y in wallTop until rows) for (x in 0 until cols) {
            val row = (y - wallTop) / brickH
            val offset = if (row % 2 == 0) 0 else 4
            still[y * cols + x] = if ((y - wallTop) % brickH == brickH - 1 || (x + offset) % 9 == 8) mortar else brick
        }
        for (x in 0 until cols) if ((x / 8) % 2 == 0) for (y in (wallTop - brickH) until wallTop) PixelKit.put(still, cols, rows, x, y, brick)
        for (x in (cols / 7) until cols step (cols / 4).coerceAtLeast(12)) torches += x to (wallTop + rows / 5)
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        System.arraycopy(still, 0, out, 0, still.size)
        if (live) stars.twinkle(time, out, palette.tint(palette.onBackground, 0.75f))
        val frame = if (live) Math.floorMod(time / 125, 3).toInt() else 1
        for ((x, y) in torches) {
            PixelKit.sprite(out, cols, rows, listOf(".o.", "ooo", ".|."), x - 1, y - 2 - if (frame == 0) 1 else 0, mapOf('o' to flame, '|' to mortar))
            PixelKit.blend(out, cols, rows, x - 2, y - 1, flame, 0.3f)
            PixelKit.blend(out, cols, rows, x + 2, y - 1, flame, 0.3f)
        }
    }

    override fun hold(time: Long): Long = 125L - Math.floorMod(time, 125L)
}
