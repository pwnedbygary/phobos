package com.phobos.emulator.ui.theme

/**
 * A TrueType font's outlines, read from its file in plain Kotlin: as much of the format as the manga
 * backdrops' lettering takes (a format 4 character map, quadratic outlines, simple and composite, the
 * advances, and kerning from pair adjustments listed pair by pair), so it letters the same everywhere.
 */
internal class TrueType(private val data: ByteArray) {
    /** One closed contour in font units, y up: each point's position and whether it's on the curve. */
    class Contour(val x: FloatArray, val y: FloatArray, val on: BooleanArray)

    private val tables = HashMap<String, Int>()
    val unitsPerEm: Int
    private val longLoca: Boolean
    private val glyphCount: Int
    private val metricCount: Int
    private val characterMap: Int
    private val kerning = HashMap<Int, Int>()

    init {
        val count = u16(4)
        for (i in 0 until count) {
            val record = 12 + 16 * i
            tables[String(data, record, 4, Charsets.ISO_8859_1)] = u32(record + 8)
        }
        val head = table("head")
        unitsPerEm = u16(head + 18)
        longLoca = s16(head + 50) != 0
        glyphCount = u16(table("maxp") + 4)
        metricCount = u16(table("hhea") + 34)
        characterMap = unicodeMap()
        tables["GPOS"]?.let(::readKerning)
    }

    private fun table(tag: String) = tables[tag] ?: error("no $tag table")

    private fun u8(at: Int) = data[at].toInt() and 0xFF
    private fun s8(at: Int) = data[at].toInt()
    private fun u16(at: Int) = (u8(at) shl 8) or u8(at + 1)
    private fun s16(at: Int) = u16(at).toShort().toInt()
    private fun u32(at: Int) = (u16(at) shl 16) or u16(at + 2)
    private fun f2dot14(at: Int) = s16(at) / 16384f

    /** The format 4 subtable of the Unicode map (Windows' first, else Unicode's own). */
    private fun unicodeMap(): Int {
        val cmap = table("cmap")
        var found = -1
        for (i in 0 until u16(cmap + 2)) {
            val platform = u16(cmap + 4 + 8 * i)
            val encoding = u16(cmap + 6 + 8 * i)
            val subtable = cmap + u32(cmap + 8 + 8 * i)
            if (u16(subtable) != 4) continue
            if (platform == 3 && encoding == 1) return subtable
            if (platform == 0 && found < 0) found = subtable
        }
        check(found >= 0) { "no format 4 character map" }
        return found
    }

    /** The glyph for [codePoint], or 0 (the font's missing glyph). */
    fun glyph(codePoint: Int): Int {
        if (codePoint > 0xFFFF) return 0
        val segments = u16(characterMap + 6) / 2
        val ends = characterMap + 14
        val starts = ends + 2 * segments + 2
        val deltas = starts + 2 * segments
        val ranges = deltas + 2 * segments
        for (i in 0 until segments) {
            if (codePoint > u16(ends + 2 * i)) continue
            val start = u16(starts + 2 * i)
            if (codePoint < start) return 0
            val delta = u16(deltas + 2 * i)
            val range = u16(ranges + 2 * i)
            if (range == 0) return (codePoint + delta) and 0xFFFF
            val glyph = u16(ranges + 2 * i + range + 2 * (codePoint - start))
            return if (glyph == 0) 0 else (glyph + delta) and 0xFFFF
        }
        return 0
    }

    /** [glyph]'s advance width, in font units. */
    fun advance(glyph: Int): Int = u16(table("hmtx") + 4 * (minOf(glyph, metricCount - 1)))

    /** The kerning between [left] and [right] (added to [left]'s advance), in font units. */
    fun kerning(left: Int, right: Int): Int = kerning[(left shl 16) or right] ?: 0

    /** The pair adjustments of the font's pair positioning lookups, where they're listed pair by pair (format 1). */
    private fun readKerning(gpos: Int) {
        val lookups = gpos + u16(gpos + 8)
        for (l in 0 until u16(lookups)) {
            val lookup = lookups + u16(lookups + 2 + 2 * l)
            for (s in 0 until u16(lookup + 4)) {
                var type = u16(lookup)
                var subtable = lookup + u16(lookup + 6 + 2 * s)
                if (type == 9) {
                    type = u16(subtable + 2)
                    subtable += u32(subtable + 4)
                }
                if (type != 2 || u16(subtable) != 1) continue
                val first = coverage(subtable + u16(subtable + 2))
                val format1 = u16(subtable + 4)
                val format2 = u16(subtable + 6)
                if (format1 and 4 == 0) continue
                val advanceAt = 2 * Integer.bitCount(format1 and 3)
                val record = 2 + 2 * Integer.bitCount(format1) + 2 * Integer.bitCount(format2)
                for ((index, glyph) in first.withIndex()) {
                    val set = subtable + u16(subtable + 10 + 2 * index)
                    for (p in 0 until u16(set)) {
                        val at = set + 2 + record * p
                        kerning.putIfAbsent((glyph shl 16) or u16(at), s16(at + 2 + advanceAt))
                    }
                }
            }
        }
    }

    /** The glyphs a coverage table lists, in coverage order. */
    private fun coverage(at: Int): IntArray = when (u16(at)) {
        1 -> IntArray(u16(at + 2)) { u16(at + 4 + 2 * it) }
        else -> {
            val glyphs = ArrayList<Int>()
            for (r in 0 until u16(at + 2)) {
                val range = at + 4 + 6 * r
                for (g in u16(range)..u16(range + 2)) glyphs += g
            }
            glyphs.toIntArray()
        }
    }

    /** [glyph]'s outline: its contours in font units (none for a blank glyph). */
    fun contours(glyph: Int): List<Contour> = contours(glyph, 0)

    private fun contours(glyph: Int, depth: Int): List<Contour> {
        if (glyph !in 0 until glyphCount || depth > 8) return emptyList()
        val loca = table("loca")
        val start = if (longLoca) u32(loca + 4 * glyph) else u16(loca + 2 * glyph) * 2
        val end = if (longLoca) u32(loca + 4 * glyph + 4) else u16(loca + 2 * glyph + 2) * 2
        if (end <= start) return emptyList()
        val at = table("glyf") + start
        val count = s16(at)
        return if (count >= 0) simple(at, count) else composite(at, depth)
    }

    private fun simple(at: Int, count: Int): List<Contour> {
        if (count == 0) return emptyList()
        val ends = IntArray(count) { u16(at + 10 + 2 * it) }
        val points = ends.last() + 1
        var p = at + 10 + 2 * count
        p += 2 + u16(p)
        val flags = IntArray(points)
        var i = 0
        while (i < points) {
            val flag = u8(p++)
            flags[i++] = flag
            if (flag and 8 != 0) repeat(u8(p++)) { if (i < points) flags[i++] = flag }
        }
        val xs = FloatArray(points)
        val ys = FloatArray(points)
        var x = 0
        for (k in 0 until points) {
            val flag = flags[k]
            x += when {
                flag and 2 != 0 -> u8(p++).let { if (flag and 16 != 0) it else -it }
                flag and 16 != 0 -> 0
                else -> s16(p).also { p += 2 }
            }
            xs[k] = x.toFloat()
        }
        var y = 0
        for (k in 0 until points) {
            val flag = flags[k]
            y += when {
                flag and 4 != 0 -> u8(p++).let { if (flag and 32 != 0) it else -it }
                flag and 32 != 0 -> 0
                else -> s16(p).also { p += 2 }
            }
            ys[k] = y.toFloat()
        }
        var first = 0
        return ends.map { last ->
            val range = first..last
            first = last + 1
            Contour(xs.sliceArray(range), ys.sliceArray(range), BooleanArray(range.count()) { flags[range.first + it] and 1 != 0 })
        }
    }

    /** A composite glyph: its components' contours, each moved by its offset and transformed by its matrix. */
    private fun composite(at: Int, depth: Int): List<Contour> {
        val out = ArrayList<Contour>()
        var p = at + 10
        do {
            val flags = u16(p)
            val component = u16(p + 2)
            p += 4
            val dx: Float
            val dy: Float
            if (flags and 1 != 0) {
                dx = s16(p).toFloat()
                dy = s16(p + 2).toFloat()
                p += 4
            } else {
                dx = s8(p).toFloat()
                dy = s8(p + 1).toFloat()
                p += 2
            }
            var a = 1f
            var b = 0f
            var c = 0f
            var d = 1f
            when {
                flags and 8 != 0 -> { a = f2dot14(p); d = a; p += 2 }
                flags and 0x40 != 0 -> { a = f2dot14(p); d = f2dot14(p + 2); p += 4 }
                flags and 0x80 != 0 -> { a = f2dot14(p); b = f2dot14(p + 2); c = f2dot14(p + 4); d = f2dot14(p + 6); p += 8 }
            }
            // Offsets that name points to match instead (flag 2 clear) are rare enough to place at the origin.
            val ox = if (flags and 2 != 0) dx else 0f
            val oy = if (flags and 2 != 0) dy else 0f
            for (contour in contours(component, depth + 1)) {
                out += Contour(
                    FloatArray(contour.x.size) { a * contour.x[it] + c * contour.y[it] + ox },
                    FloatArray(contour.x.size) { b * contour.x[it] + d * contour.y[it] + oy },
                    contour.on,
                )
            }
        } while (flags and 0x20 != 0)
        return out
    }
}
