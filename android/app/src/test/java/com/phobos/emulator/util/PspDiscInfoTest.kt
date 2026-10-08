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

    @Test
    fun sha256OfTheCacheKeyIsSixtyFourHexDigits() {
        val key = iconCacheKey("content://media/external/12345", 104857600, 1700000000000)
        val hash = sha256Hex(key)
        assertEquals(64, hash.length)
        // The SHA-256 of "abc" is a well-known value; the function's output format is checked here.
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha256Hex("abc"))
    }

    @Test
    fun infoCacheKeepsAnEmptyDiscId() {
        // writeText("title\n") — readLines() would drop the empty second field; parse must not.
        assertEquals("Homebrew" to "", parsePspInfoCache("Homebrew\n"))
        assertEquals("Game" to "ULUS12345", parsePspInfoCache("Game\nULUS12345"))
        assertEquals("" to "ULUS12345", parsePspInfoCache("\nULUS12345"))
        assertEquals(null, parsePspInfoCache("only-one-line"))
    }
}
