package com.phobos.emulator.ui.theme

import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * The CRT terminal's backdrop, drawn into an ARGB array: a picture tube at rest. The background glows
 * toward the [phosphor] below the middle ([crtBloom], [bloomAt]); the curved face darkens into rounded
 * corners ([crtVignette]); a little grain and a faint reflection on the glass catch the light; and a
 * terminal's boot text is burned in, a ghost, at the bottom left. The scanlines, the aperture grille and the
 * slow refresh band are drawn over it at the screen's own pixels ([CrtBackdrop]), since this picture may be
 * scaled. Plain Kotlin, drawn once per size and theme.
 */
internal object CrtScreen {
    /** The burned-in text, top to bottom. */
    val BOOT = listOf("PHOBOS SYSTEM MONITOR V2.1", "MEMORY TEST   65536K OK", "SYSTEMS       34 READY", ">RUN LIBRARY_")

    /** How far the burned-in text is toward the phosphor. */
    fun ghost(isDark: Boolean): Float = if (isDark) 0.22f else 0.14f

    /** The brightest the glass's reflection makes the screen, toward white. */
    const val REFLECTION = 0.0175f

    fun render(width: Int, height: Int, background: Int, phosphor: Int, isDark: Boolean): IntArray {
        val w = width.coerceAtLeast(1)
        val h = height.coerceAtLeast(1)
        val px = IntArray(w * h)
        val bloom = crtBloom(isDark)
        val vignette = crtVignette(isDark)
        val unit = max(1f, min(w, h) / 420f)
        val text = bootText(w, h, unit)
        val cx = w / 2f
        val cy = h / 2f
        for (y in 0 until h) {
            for (x in 0 until w) {
                val dx = (x + 0.5f - cx) / cx
                val dy = (y + 0.5f - cy) / cy
                var color = PixelKit.mix(background, phosphor, bloom * bloomAt(x, y, w, h))
                text[y * w + x].takeIf { it > 0f }?.let { color = PixelKit.mix(color, phosphor, ghost(isDark) * it) }
                // The glass catches a soft band of light across its upper right, away from the top bar's title.
                val sheen = REFLECTION * exp(-((-dx * 0.55f + dy + 0.55f) / 0.22f).let { it * it }) * (1f - dy).coerceIn(0f, 1f)
                if (sheen > 0.002f) color = PixelKit.mix(color, WHITE, sheen)
                // The face curves away into rounded corners: darker toward them, as a squarish oval.
                val ax = abs(dx)
                val ay = abs(dy)
                val edge = (ax * ax * ax * ax * ax + ay * ay * ay * ay * ay).pow(0.2f)
                val shade = vignette * smooth((edge - 0.62f) / 0.48f)
                color = PixelKit.mix(color, BLACK, shade)
                px[y * w + x] = grain(color, x, y, isDark)
            }
        }
        return px
    }

    /**
     * How much of the bloom's strength reaches pixel ([x], [y]) of a [w] by [h] screen, 0 to 1: it's
     * brightest below the middle, so the top bar's title stays on darker glass, and falls off over the
     * shorter side, so a wide screen's top is as dark as a narrower one's.
     */
    fun bloomAt(x: Int, y: Int, w: Int, h: Int): Float {
        val dx = x + 0.5f - w / 2f
        val dy = y + 0.5f - h * BLOOM_DOWN
        return smooth(1f - sqrt(dx * dx + dy * dy) / (min(w, h) * 0.95f))
    }

    /** How much of each pixel the burned-in text lights, 0 to 1, with a soft spill a font pixel around it. */
    private fun bootText(w: Int, h: Int, unit: Float): FloatArray {
        val out = FloatArray(w * h)
        val dot = max(1, (unit * 1.5f).roundToInt())
        val advance = 6 * dot
        val lineHeight = 10 * dot
        val left = (w * 0.06f).roundToInt()
        val top = h - (h * 0.08f).roundToInt() - BOOT.size * lineHeight
        BOOT.forEachIndexed { row, line ->
            line.forEachIndexed { col, char ->
                val glyph = FONT[char] ?: return@forEachIndexed
                for (gy in 0 until 7) for (gx in 0 until 5) {
                    if (glyph[gy][gx] != '#') continue
                    val x0 = left + col * advance + gx * dot
                    val y0 = top + row * lineHeight + gy * dot
                    for (y in y0 - dot until y0 + 2 * dot) for (x in x0 - dot until x0 + 2 * dot) {
                        if (x !in 0 until w || y !in 0 until h) continue
                        val inside = x in x0 until x0 + dot && y in y0 until y0 + dot
                        val i = y * w + x
                        out[i] = max(out[i], if (inside) 1f else 0.3f)
                    }
                }
            }
        }
        return out
    }

    /** A level of grain either way, fixed per pixel. */
    private fun grain(color: Int, x: Int, y: Int, isDark: Boolean): Int {
        val n = PixelKit.unit(1972, y * 7919 + x)
        val d = if (n < 0.2f) -1 else if (n > 0.8f && isDark) 1 else 0
        if (d == 0) return color
        fun channel(shift: Int) = ((color shr shift and 0xFF) + d).coerceIn(0, 255) shl shift
        return (0xFF shl 24) or channel(16) or channel(8) or channel(0)
    }

    private fun smooth(t: Float): Float = t.coerceIn(0f, 1f).let { it * it * (3 - 2 * it) }

    /** Where the bloom is brightest, down the height. */
    private const val BLOOM_DOWN = 0.6f
    private const val WHITE = 0xFFFFFFFF.toInt()
    private const val BLACK = 0xFF000000.toInt()

    /** A 5 by 7 terminal font, just the letters the boot text uses. */
    private val FONT: Map<Char, List<String>> = mapOf(
        'A' to listOf(".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
        'B' to listOf("####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."),
        'D' to listOf("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."),
        'E' to listOf("#####", "#....", "#....", "####.", "#....", "#....", "#####"),
        'H' to listOf("#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
        'I' to listOf(".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."),
        'K' to listOf("#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"),
        'L' to listOf("#....", "#....", "#....", "#....", "#....", "#....", "#####"),
        'M' to listOf("#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"),
        'N' to listOf("#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"),
        'O' to listOf(".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."),
        'P' to listOf("####.", "#...#", "#...#", "####.", "#....", "#....", "#...."),
        'R' to listOf("####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"),
        'S' to listOf(".####", "#....", "#....", ".###.", "....#", "....#", "####."),
        'T' to listOf("#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."),
        'U' to listOf("#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."),
        'V' to listOf("#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."),
        'Y' to listOf("#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."),
        '1' to listOf("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."),
        '2' to listOf(".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"),
        '3' to listOf("####.", "....#", "....#", ".###.", "....#", "....#", "####."),
        '4' to listOf("...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."),
        '5' to listOf("#####", "#....", "####.", "....#", "....#", "#...#", ".###."),
        '6' to listOf("..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."),
        '.' to listOf(".....", ".....", ".....", ".....", ".....", ".##..", ".##.."),
        '>' to listOf(".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."),
        '_' to listOf(".....", ".....", ".....", ".....", ".....", ".....", "#####"),
    )
}
