package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class PspDiscInfoTest {
    @Test
    fun showsTheDiscsTitleWhenItHasOne() {
        assertEquals("Test Game", displayTitle("test-game.iso", "Test Game"))
    }

    @Test
    fun fallsBackToTheFileNameWhenTheTitleIsBlank() {
        assertEquals("test-game.iso", displayTitle("test-game.iso", null))
        assertEquals("test-game.iso", displayTitle("test-game.iso", ""))
        assertEquals("test-game.iso", displayTitle("test-game.iso", "  "))
    }

    @Test
    fun cacheKeyIsUriPlusSizePlusMtime() {
        assertEquals("content:__media_external_12345|104857600|1700000000000",
            iconCacheKey("content://media/external/12345", 104857600, 1700000000000))
    }

    @Test
    fun cacheKeyChangesWhenTheFileChanges() {
        val uri = "content://media/external/12345"
        val before = iconCacheKey(uri, 104857600, 1700000000000)
        val after = iconCacheKey(uri, 104857601, 1700000000000)
        assertEquals(before != after, true)
        val afterMtime = iconCacheKey(uri, 104857600, 1700000000001)
        assertEquals(before != afterMtime, true)
    }
}
