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
