package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * Proves the rendered-cart cache contract: a re-render performs zero fetches and zero crops, a parameter change
 * is a miss, a corrupt entry is deleted and re-rendered, and the byte budget is enforced. The render lambda
 * counts the two phases separately (the app's lambda fetches the label and crops it; a second counter would hide
 * a re-crop behind a cache hit).
 */
class CartBitmapCacheTest {

    private val png = byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A) + ByteArray(56) { 7 }

    private fun tempDir(): File = Files.createTempDirectory("cartbitmap").toFile().apply { deleteOnExit() }

    private fun key(theme: String = "phobos") = CartBitmapCache.Key(
        gameId = 42, kind = "grid", theme = theme, face = "mplus1", finish = "solid", color = "#6f4fb0",
        special = false, wear = 1, width = 520, height = 693,
    )

    @Test
    fun aReRenderFetchesNothingAndCropsNothing() {
        val dir = tempDir()
        val cache = CartBitmapCache(dir, maxBytes = 1_000_000)
        var fetches = 0
        var crops = 0
        val render = { f: File -> fetches++; crops++; f.writeBytes(png) }

        val first = cache.get(key(), render)
        val second = cache.get(key(), render)

        assertNotNull(first)
        assertEquals(first, second)
        assertEquals("one fetch for the first render", 1, fetches)
        assertEquals("one crop for the first render", 1, crops)
        val stats = cache.stats()
        assertEquals(1, stats.entries)
        assertEquals(1L, stats.misses)
        assertEquals(1L, stats.hits)
    }

    @Test
    fun aThemeOrLookChangeIsAMiss() {
        val dir = tempDir()
        val cache = CartBitmapCache(dir, maxBytes = 1_000_000)
        var fetches = 0
        var crops = 0
        val render = { f: File -> fetches++; crops++; f.writeBytes(png) }

        cache.get(key(theme = "phobos"), render)
        cache.get(key(theme = "manga"), render)
        cache.get(key(theme = "manga").copy(finish = "glitter"), render)

        assertEquals(3, fetches)
        assertEquals(3, crops)
        assertEquals(3, cache.stats().entries)
    }

    @Test
    fun aCorruptEntryIsDeletedAndRefetched() {
        val dir = tempDir()
        val cache = CartBitmapCache(dir, maxBytes = 1_000_000)
        var fetches = 0
        var crops = 0
        val render = { f: File -> fetches++; crops++; f.writeBytes(png) }

        val file = cache.get(key(), render)!!
        file.writeBytes(byteArrayOf(1, 2, 3))

        val again = cache.get(key(), render)
        assertNotNull(again)
        assertEquals(2, fetches)
        assertEquals(2, crops)
    }

    @Test
    fun aFailedRenderLeavesNoEntry() {
        val dir = tempDir()
        val cache = CartBitmapCache(dir, maxBytes = 1_000_000)
        assertNull(cache.get(key()) { f -> f.writeBytes(byteArrayOf(9, 9)) })
        assertEquals(0, cache.stats().entries)
        assertTrue(dir.listFiles()!!.none { it.name.endsWith(".tmp") })
    }

    @Test
    fun theBudgetIsEnforcedAndPrunedAtStartup() {
        val dir = tempDir()
        val small = 5 * 64L // 64-byte entries; a 300-byte budget holds ~4
        val cache = CartBitmapCache(dir, maxBytes = 300)
        var n = 0L
        val render = { f: File -> n++; f.writeBytes(png) }
        for (i in 1..10) cache.get(key(theme = "t$i"), render)
        cache.prune()
        assertTrue("total ${cache.stats().bytes} <= 300", cache.stats().bytes <= 300)
        assertTrue(cache.stats().entries < 10)

        // A new cache over the same directory prunes again at startup.
        val reopened = CartBitmapCache(dir, maxBytes = 120)
        assertTrue(reopened.stats().bytes <= 120)
    }

    @Test
    fun aTwoHundredCardGridShowsTheHitRate() {
        val dir = tempDir()
        val cache = CartBitmapCache(dir, maxBytes = 1_000_000)
        var fetches = 0
        var crops = 0
        val render = { f: File -> fetches++; crops++; f.writeBytes(png) }

        for (i in 1..200) cache.get(key().copy(gameId = i.toLong()), render)   // first screenful page
        for (i in 1..200) cache.get(key().copy(gameId = i.toLong()), render)   // scrolled back

        val s = cache.stats()
        assertEquals(200, s.entries)
        assertEquals(200L, s.misses)
        assertEquals(200L, s.hits)
        assertEquals(200, fetches)
        assertEquals(200, crops)
        println("200-card grid: entries=${s.entries} bytes=${s.bytes} hits=${s.hits} misses=${s.misses}")
    }

    @Test
    fun cachedReturnsAStoredPictureWithoutRendering() {
        val cache = CartBitmapCache(tempDir(), maxBytes = 1_000_000)
        var renders = 0
        val render = { f: File -> renders++; f.writeBytes(png) }
        assertNull("nothing stored yet", cache.cached(key()))
        assertNotNull(cache.get(key(), render))
        assertEquals(1, renders)
        assertNotNull("the cache-only path serves the stored PNG", cache.cached(key()))
        assertEquals("cached() never renders", 1, renders)
        assertNull("a different key is a miss", cache.cached(key(theme = "dark")))
    }

    @Test
    fun aResidentGameGetsTheCachedPictureOrBoxArt() {
        // The Library card's cache-only branch: thumbnailer null (a game is resident), cache warm or cold.
        val cache = CartBitmapCache(tempDir(), maxBytes = 1_000_000)
        val k = key()
        assertNull("a cold cache falls back to box art", cacheOnlyPicture(cache, k))
        cache.get(k) { f -> f.writeBytes(png) }
        assertNotNull("a warm cache serves the stored picture", cacheOnlyPicture(cache, k))
        assertNull("a different card stays on box art", cacheOnlyPicture(cache, key(theme = "dark")))
        assertNull("no cache at all falls back", cacheOnlyPicture(null, k))
        assertNull("no key falls back", cacheOnlyPicture(cache, null))
    }
}
