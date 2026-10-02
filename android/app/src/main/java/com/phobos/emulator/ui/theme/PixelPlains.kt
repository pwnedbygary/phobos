package com.phobos.emulator.ui.theme

import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/**
 * The plains backdrop, like a platformer's first level: clouds drifting over rounded hills, floating
 * brick and question blocks with coins spinning above them, and grass over a brick ground. The clouds
 * move a pixel at a time, the coins turn eight times a second and the question marks blink.
 */
internal class PlainsArt(cols: Int, rows: Int, private val palette: PixelPalette, seed: Int = 1985) : PixelArt(cols, rows) {
    private val still = IntArray(cols * rows)
    private val block = (rows * 0.045f).roundToInt().coerceIn(6, 16)
    private val ground = (rows - max(8f, rows * 0.13f)).toInt().coerceIn(0, rows)
    private val outline = PixelKit.mix(palette.background, BLACK, 0.45f)

    private class Cloud(val pixels: IntArray, val width: Int, val height: Int, val start: Float, val top: Int, val speed: Float)

    private val clouds = ArrayList<Cloud>()

    /** Where the question marks' pixels are, and the colors they blink through. */
    private val marks = ArrayList<Int>()
    private val blink = intArrayOf(
        palette.tint(palette.onBackground, 0.95f),
        palette.tint(palette.onBackground, 0.78f),
        palette.tint(palette.tertiary, 0.9f),
        palette.tint(palette.onBackground, 0.78f),
    )

    /** Each coin's center column and bottom row. */
    private val coins = ArrayList<IntArray>()
    private val actors = ArrayList<IntArray>()
    private val foes = ArrayList<IntArray>()
    private val coinHeight = (block * 0.75f).roundToInt().coerceAtLeast(4)
    private val coin = palette.tint(palette.tertiary, 0.95f)
    private val coinShade = palette.tint(palette.tertiary, 0.62f)

    init {
        val skyLow = palette.tint(palette.secondary, 0.35f)
        PixelKit.sky(still, cols, 0, rows, palette.background, skyLow)
        mounds(seed + 1, cols / 60 + 2, rows * 0.5f, rows * 0.33f, palette.tint(palette.secondary, 0.32f), palette.tint(palette.secondary, 0.42f))
        mounds(seed + 2, cols / 45 + 2, rows * 0.25f, rows * 0.15f, palette.tint(palette.secondary, 0.48f), palette.tint(palette.secondary, 0.6f))
        mounds(seed + 3, cols / 70 + 1, rows * 0.06f, rows * 0.04f, palette.tint(palette.secondary, 0.62f), palette.tint(palette.secondary, 0.74f))
        paintGround(palette, seed)
        paintBlocks(palette)

        val cloudColor = palette.tint(palette.onBackground, 0.8f)
        val random = Random(seed + 4)
        val count = (cols / 90).coerceIn(2, 8)
        repeat(count) { i ->
            val far = i % 2 == 0
            val body = if (far) PixelKit.mix(cloudColor, skyLow, 0.35f) else cloudColor
            val height = (rows * (if (far) 0.06f else 0.085f) * (0.8f + 0.4f * random.nextFloat())).roundToInt().coerceAtLeast(3)
            val top = (rows * (0.04f + 0.28f * random.nextFloat())).toInt().coerceAtMost((rows * 0.42f).toInt() - height).coerceAtLeast(0)
            clouds += cloud(height, body, PixelKit.mix(body, skyLow, 0.3f), PixelKit.mix(body, skyLow, 0.6f), random.nextFloat() * (cols + height * 3), top, if (far) 2f else 4f)
        }
        repeat((2 + random.nextInt(3)).coerceAtMost(4)) { skin ->
            actors += intArrayOf(random.nextInt(cols), skin % HEROES.size, random.nextInt(4_000))
        }
        repeat((cols / 140).coerceIn(1, 4)) { foes += intArrayOf(random.nextInt(cols), random.nextInt(4_000)) }
    }

    override fun compose(time: Long, out: IntArray, live: Boolean) {
        System.arraycopy(still, 0, out, 0, still.size)
        val now = if (live) time else 0L
        for (cloud in clouds) {
            val span = (cols + cloud.width).toLong()
            val left = Math.floorMod(floor(cloud.start - now / 1000.0 * cloud.speed).toLong(), span).toInt() - cloud.width
            for (dy in 0 until cloud.height) {
                for (dx in 0 until cloud.width) {
                    val color = cloud.pixels[dy * cloud.width + dx]
                    if (color != 0) PixelKit.put(out, cols, rows, left + dx, cloud.top + dy, color)
                }
            }
        }
        val shade = blink[Math.floorMod(Math.floorDiv(now, 250L), 4L).toInt()]
        for (index in marks) out[index] = shade
        val frame = Math.floorMod(Math.floorDiv(now, COIN_FRAME_MS), 4L).toInt()
        for ((index, c) in coins.withIndex()) {
            val collected = actors.any { actor ->
                val actorX = Math.floorMod((actor[0] + now / 90).toInt(), cols)
                kotlin.math.abs(actorX - c[0]) < block && Math.floorMod(now + actor[2], 4_000L) < 850L
            }
            if (!collected) paintCoin(out, c[0], c[1], frame)
        }
        paintActors(out, now)
    }

    override fun hold(time: Long): Long = COIN_FRAME_MS - Math.floorMod(time, COIN_FRAME_MS)

    /** Tiny platform heroes run, periodically jump, and squash the wandering mushroom-shaped foes. */
    private fun paintActors(out: IntArray, time: Long) {
        for (actor in actors) {
            val x = Math.floorMod((actor[0] + time / 90).toInt(), cols + 8) - 4
            val jump = Math.floorMod(time + actor[2], 4_000L)
            val lift = if (jump < 900) (sin(jump / 900.0 * Math.PI) * block * 3).toInt() else 0
            val colors = HERO_COLORS[actor[1] % HERO_COLORS.size].map { it.first to palette.tint(it.second, 0.9f) }.toMap()
            PixelKit.sprite(out, cols, rows, HEROES[actor[1] % HEROES.size], x, ground - 6 - lift, colors)
        }
        for (foe in foes) {
            val x = Math.floorMod((foe[0] + if ((time + foe[1]) / 1_200 % 2L == 0L) time / 150 else -time / 150).toInt(), cols)
            val stomped = actors.any { actor ->
                val heroX = Math.floorMod((actor[0] + time / 90).toInt(), cols)
                val jumping = Math.floorMod(time + actor[2], 4_000L) < 900L
                jumping && kotlin.math.abs(heroX - x) < 3
            }
            if (!stomped) PixelKit.sprite(out, cols, rows, FOE, x, ground - 4, mapOf('b' to palette.tint(palette.primary, 0.42f), 'd' to outline))
        }
    }

    /** Rounded hills standing on the ground, up to [width] wide and [height] tall, with a lighter crest. */
    private fun mounds(seed: Int, count: Int, width: Float, height: Float, color: Int, crest: Int) {
        val random = Random(seed)
        val tops = IntArray(cols) { ground }
        repeat(count) {
            val center = random.nextFloat() * cols
            val w = width * (0.6f + 0.8f * random.nextFloat())
            val h = height * (0.6f + 0.4f * random.nextFloat())
            for (x in max(0, (center - w).toInt()) until min(cols, ceil(center + w).toInt())) {
                val u = (x + 0.5f - center) / w
                if (abs(u) >= 1f) continue
                tops[x] = min(tops[x], (ground - h * sqrt(1f - u * u)).toInt().coerceAtLeast(0))
            }
        }
        for (x in 0 until cols) {
            for (y in tops[x] until ground) still[y * cols + x] = if (y < tops[x] + 2) crest else color
        }
    }

    private fun paintGround(palette: PixelPalette, seed: Int) {
        val grass = palette.tint(palette.secondary, 0.78f)
        val grassDark = palette.tint(palette.secondary, 0.58f)
        val dirt = palette.tint(palette.primary, 0.34f)
        val mortar = palette.tint(palette.primary, 0.18f)
        for (x in 0 until cols) if (PixelKit.unit(seed, x) < 0.35f) PixelKit.put(still, cols, rows, x, ground - 1, grass)
        for (y in ground until rows) {
            val dy = y - ground
            for (x in 0 until cols) {
                still[y * cols + x] = when {
                    dy < 3 -> grass
                    dy == 3 -> grassDark
                    else -> {
                        val by = dy - 4
                        val bx = x + if ((by / 4) % 2 == 0) 0 else 4
                        if (by % 4 == 3 || bx % 8 == 7) mortar else dirt
                    }
                }
            }
        }
    }

    private fun paintBlocks(palette: PixelPalette) {
        val brick = Triple(palette.tint(palette.primary, 0.55f), palette.tint(palette.primary, 0.8f), palette.tint(palette.primary, 0.3f))
        val question = Triple(palette.tint(palette.tertiary, 0.6f), palette.tint(palette.tertiary, 0.85f), palette.tint(palette.tertiary, 0.35f))
        val b = block
        val low = ground - (b * 4.2f).toInt()
        if (low - b < (rows * 0.42f).toInt()) return
        val start = (cols * 0.56f).toInt() - 5 * b / 2
        listOf(false, true, false, true, false).forEachIndexed { k, isQuestion ->
            paintBlock(start + k * b, low, if (isQuestion) question else brick, isQuestion)
        }
        paintBlock((cols * 0.24f).toInt() - b / 2, low, question, true)
        val high = ground - (b * 8.4f).toInt()
        if (high > (rows * 0.42f).toInt()) {
            for (k in 0 until 3) paintBlock((cols * 0.6f).toInt() + k * b, high, brick, false)
        }
    }

    /** A block with its top left at ([left], [top]), in body, light and shadow colors; a question block gets a blinking mark and a coin. */
    private fun paintBlock(left: Int, top: Int, colors: Triple<Int, Int, Int>, isQuestion: Boolean) {
        val (body, light, shadow) = colors
        val b = block
        val half = b / 2
        for (dy in 0 until b) {
            for (dx in 0 until b) {
                val color = when {
                    dx == b - 1 || dy == b - 1 -> outline
                    dx == 0 || dy == 0 -> light
                    dx == b - 2 || dy == b - 2 -> shadow
                    isQuestion && (dx == 2 || dx == b - 3) && (dy == 2 || dy == b - 3) -> shadow
                    !isQuestion && (dy == half || (dy < half && dx == half) || (dy > half && (dx == b / 4 || dx == 3 * b / 4))) -> shadow
                    else -> body
                }
                PixelKit.put(still, cols, rows, left + dx, top + dy, color)
            }
        }
        if (!isQuestion) return
        if (b >= 10) {
            val gx = left + (b - 5) / 2
            val gy = top + (b - 7) / 2
            MARK.forEachIndexed { dy, line ->
                line.forEachIndexed { dx, c ->
                    val x = gx + dx
                    val y = gy + dy
                    if (c == '#' && x in 0 until cols && y in 0 until rows) marks += y * cols + x
                }
            }
        }
        coins += intArrayOf(left + b / 2, top - 3)
    }

    /** A coin centered on column [cx] with its bottom on row [bottom], turned to [frame] of four. */
    private fun paintCoin(out: IntArray, cx: Int, bottom: Int, frame: Int) {
        val h = coinHeight
        val width = when (frame) {
            0 -> h * 0.62f
            1, 3 -> h * 0.4f
            else -> 1f
        }
        val color = if (frame == 2) coinShade else coin
        val top = bottom - h
        for (dy in 0 until h) {
            val v = (dy + 0.5f - h / 2f) / (h / 2f)
            val half = width / 2f * sqrt(max(0f, 1f - v * v))
            val extent = max(0.5f, half)
            for (x in floor(cx + 0.5f - extent).toInt() until ceil(cx + 0.5f + extent).toInt()) {
                val shaded = frame != 2 && x >= cx + extent - 1f && width > 2f
                PixelKit.put(out, cols, rows, x, top + dy, if (shaded) coinShade else color)
            }
        }
    }

    /** A cloud [height] rows tall: three puffs on a flat base, shaded along the bottom and outlined. */
    private fun cloud(height: Int, body: Int, shade: Int, edge: Int, start: Float, top: Int, speed: Float): Cloud {
        val width = (height * 2.2f).roundToInt().coerceAtLeast(3)
        val h = height.toFloat()
        val w = width.toFloat()
        fun inside(x: Int, y: Int): Boolean {
            if (x !in 0 until width || y !in 0 until height) return false
            val px = x + 0.5f
            val py = y + 0.5f
            fun puff(cx: Float, cy: Float, r: Float) = (px - cx) * (px - cx) + (py - cy) * (py - cy) < r * r
            return puff(0.3f * w, 0.62f * h, 0.38f * h) || puff(0.55f * w, 0.45f * h, 0.45f * h) ||
                puff(0.78f * w, 0.65f * h, 0.33f * h) || (px in 0.15f * w..0.88f * w && py >= 0.62f * h)
        }
        // A pixel of margin all round leaves room for the outline.
        val pw = width + 2
        val ph = height + 2
        val pixels = IntArray(pw * ph)
        for (y in 0 until ph) {
            for (x in 0 until pw) {
                val sx = x - 1
                val sy = y - 1
                pixels[y * pw + x] = when {
                    inside(sx, sy) -> if (!inside(sx, sy + 2)) shade else body
                    inside(sx - 1, sy) || inside(sx + 1, sy) || inside(sx, sy - 1) || inside(sx, sy + 1) -> edge
                    else -> 0
                }
            }
        }
        return Cloud(pixels, pw, ph, start, (top - 1).coerceAtLeast(0), speed)
    }

    private companion object {
        const val COIN_FRAME_MS = 125L
        const val BLACK = 0xFF000000.toInt()
        val MARK = listOf(".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#..")
        val HEROES = listOf(
            listOf(".rr.", "rrrr", ".ss.", "s.ss"),
            listOf(".gg.", "gggg", ".ss.", "s.ss"),
            listOf(".ww.", "wwww", ".ss.", "s.ss"),
            listOf(".gg.", "gggg", "gssg", ".gg."),
        )
        val HERO_COLORS = listOf(
            listOf('r' to 0xFFFF4030.toInt(), 's' to 0xFFF6C38A.toInt()),
            listOf('g' to 0xFF4ACB5C.toInt(), 's' to 0xFFF6C38A.toInt()),
            listOf('w' to 0xFFFFE8D0.toInt(), 's' to 0xFFF6C38A.toInt()),
            listOf('g' to 0xFF56C86A.toInt(), 's' to 0xFFE9B45D.toInt()),
        )
        val FOE = listOf(".bb.", "bbbb", "d..d", ".dd.")
    }
}
