package com.phobos.emulator.ui.theme

import com.phobos.emulator.ui.systemIconSlugs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PlatformGlyphsTest {
    @Test
    fun everySystemIconSlugHasAGlyph() {
        assertEquals(systemIconSlugs, PlatformGlyphs.bySlug.keys)
    }

    @Test
    fun everySystemHasAGlyphOfItsOwn() {
        // At least four cells apart, so no two systems look alike at a glance.
        val slugs = PlatformGlyphs.bySlug.keys.sorted()
        val alike = slugs.flatMapIndexed { i, a -> slugs.drop(i + 1).map { b -> Triple(a, b, cellsApart(a, b)) } }.filter { it.third < 4 }
        assertTrue("glyphs too alike (slug, slug, cells apart): $alike", alike.isEmpty())
    }

    private fun cellsApart(a: String, b: String): Int {
        val rowsA = PlatformGlyphs.bySlug.getValue(a)
        val rowsB = PlatformGlyphs.bySlug.getValue(b)
        val width = maxOf(rowsA.maxOf { it.length }, rowsB.maxOf { it.length })
        fun cell(rows: List<String>, row: Int, column: Int) = rows.getOrNull(row)?.getOrNull(column) ?: '.'
        return (0 until maxOf(rowsA.size, rowsB.size)).sumOf { row -> (0 until width).count { column -> cell(rowsA, row, column) != cell(rowsB, row, column) } }
    }

    @Test
    fun glyphRowsAreRectangularAndUseKnownCells() {
        PlatformGlyphs.bySlug.forEach { (slug, rows) ->
            assertTrue("$slug has no rows", rows.isNotEmpty())
            val width = rows.first().length
            assertTrue("$slug has an empty row", width > 0)
            rows.forEach { row ->
                assertEquals("$slug has uneven rows", width, row.length)
                assertTrue("$slug contains an unknown cell", row.all { it in ".XWSA" })
            }
        }
    }
}
