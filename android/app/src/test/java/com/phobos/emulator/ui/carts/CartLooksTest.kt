package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** [CartLooks]: encoding, parsing (malformed entries dropped), adding and removing entries. */
class CartLooksTest {

    @Test
    fun encodesAndDecodesOneLook() {
        val look = LookResolver.Look("metallic", "#c9a349")
        assertEquals("metallic|#c9a349", CartLooks.encode(look))
        assertEquals(look, CartLooks.decode("metallic|#c9a349"))
        assertNull(CartLooks.decode(null))
        assertNull(CartLooks.decode(""))
        assertNull(CartLooks.decode("unknownfinish|#c9a349"))
        assertNull(CartLooks.decode("metallic|red"))
        assertNull(CartLooks.decode("metallic"))
    }

    @Test
    fun parsesValidEntriesAndDropsTheRest() {
        val json = """{"s:Famicom":"solid|#6f4fb0","g:12":"nope","g:13":"clear|#e8ecf2"}"""
        val map = CartLooks.parse(json)
        assertEquals(setOf("s:Famicom", "g:13"), map.keys)
        assertEquals("solid", map["s:Famicom"]!!.finish)
        assertEquals(emptyMap<String, LookResolver.Look>(), CartLooks.parse(null))
        assertEquals(emptyMap<String, LookResolver.Look>(), CartLooks.parse("{not json"))
    }

    @Test
    fun withAddsAndRemovesEntries() {
        val one = CartLooks.with(null, "s:Famicom", LookResolver.Look("solid", "#6f4fb0"))
        assertEquals("solid", CartLooks.parse(one)["s:Famicom"]!!.finish)
        val none = CartLooks.with(one, "s:Famicom", null)
        assertEquals(emptyMap<String, LookResolver.Look>(), CartLooks.parse(none))
    }

    @Test
    fun keysShareTheRenderCachesGameId() {
        assertEquals("s:nes", CartLooks.systemKey("nes"))
        assertEquals("g:123", CartLooks.gameKey("nes", "x").replace(CartLooks.gameId("nes", "x").toString(), "123"))
        assertEquals(CartLooks.gameId("Famicom", "a.nes"), CartBitmapCache.Key(
            CartLooks.gameId("Famicom", "a.nes"), "cart:nes", "t", "f", "", "", false, 0, 1, 1,
        ).gameId)
    }
}
