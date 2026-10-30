package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CartShapesTest {

    @Test
    fun labelShapeFollowsScraperDotJs() {
        assertEquals("nes", CartShapes.labelShape("Famicom"))
        assertEquals("snes", CartShapes.labelShape("Super Famicom"))
        assertEquals("gb", CartShapes.labelShape("Game Boy"))
        assertEquals("gbc", CartShapes.labelShape("Game Boy Color"))
        assertEquals("gba", CartShapes.labelShape("Game Boy Advance"))
        assertEquals("n64", CartShapes.labelShape("Nintendo 64"))
        assertEquals("snes", CartShapes.labelShape("Super Famicom"))
        // Disc media crops as the whole printed face, however the shell is rendered.
        assertEquals("disc", CartShapes.labelShape("PlayStation"))
        assertEquals("disc", CartShapes.labelShape("Mega CD"))
        assertEquals("disc", CartShapes.labelShape("PC Engine CD"))
        // PSP keeps Cartridge's 'umd' media shape (a cart-type system with a UMD crop).
        assertEquals("umd", CartShapes.labelShape("PlayStation Portable"))
        // Systems Cartridge does not have take no scans.
        assertNull(CartShapes.labelShape("Neo Geo CD"))
        assertNull(CartShapes.labelShape("ZX Spectrum"))
        assertNull(CartShapes.labelShape("MSX2"))
        assertNull(CartShapes.labelShape("Mega LD"))
    }

    @Test
    fun shapesAreTheNearestOnesTheScopeChose() {
        assertEquals("sms", CartShapes.cartShape("SG-1000"))
        assertEquals("cd", CartShapes.cartShape("Neo Geo CD"))
        assertEquals("hucard", CartShapes.cartShape("SuperGrafx"))
        assertEquals("a26", CartShapes.cartShape("ColecoVision"))
        assertEquals("minidisc", CartShapes.cartShape("PlayStation Portable"))
        assertEquals("small", CartShapes.cartShape("Pocket Challenge V2"))
        assertNull(CartShapes.cartShape("ZX Spectrum"))
        assertNull(CartShapes.cartShape("Mega LD"))
    }

    @Test
    fun discSystemsUseTheDiscOutputs() {
        assertTrue(CartShapes.isDisc("PlayStation"))
        assertTrue(CartShapes.isDisc("Mega CD"))
        assertTrue(CartShapes.isDisc("PlayStation Portable"))
        assertTrue(CartShapes.isDisc("Neo Geo CD"))
        assertTrue(!CartShapes.isDisc("Famicom"))
        assertTrue(!CartShapes.isDisc("Arcade"))
    }

    @Test
    fun everyShapeExistsInTheGeneratedAssets() {
        val names = setOf("famicom", "sfc", "n64", "gb", "gbc", "gba", "sms", "md", "small", "aes", "hucard", "a26", "pcb", "cd", "psx", "minidisc")
        for (system in listOf(
            "Famicom", "Super Famicom", "Super Game Boy", "Game Boy", "Game Boy Color", "Game Boy Advance",
            "Nintendo 64", "Master System", "SG-1000", "Mega Drive", "Mega 32X", "Game Gear", "Mega CD",
            "Mega CD 32X", "PlayStation", "PlayStation Portable", "Neo Geo", "Neo Geo CD", "Neo Geo Pocket",
            "Neo Geo Pocket Color", "PC Engine", "PC Engine CD", "SuperGrafx", "WonderSwan", "WonderSwan Color",
            "Pocket Challenge V2", "Atari 2600", "ColecoVision", "Arcade",
        )) {
            val shape = CartShapes.cartShape(system)
            assertTrue("$system has a generated shape", shape in names)
        }
    }
}
