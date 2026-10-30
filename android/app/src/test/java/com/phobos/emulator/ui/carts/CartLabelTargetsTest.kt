package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** [CartLabelTargets] against [CartShapes]: the ones with an entry map through, the ones without do not. */
class CartLabelTargetsTest {

    @Test
    fun mappedSystemsCarryCartridgesIdAndMedia() {
        val famicom = CartLabelTargets.of("Famicom")!!
        assertEquals("nes", famicom.systemId)
        assertEquals("cart", famicom.mediaType)
        assertEquals("nes", famicom.mediaShape)

        val psp = CartLabelTargets.of("PlayStation Portable")!!
        assertEquals("psp", psp.systemId)
        assertEquals("umd", psp.mediaShape)

        val megaCd = CartLabelTargets.of("Mega CD")!!
        assertEquals("segacd", megaCd.systemId)
        assertEquals("disc", megaCd.mediaType)
        assertEquals("cd", megaCd.mediaShape)

        val gbc = CartLabelTargets.of("Game Boy Color")!!
        assertEquals("gbc", gbc.systemId)
        assertEquals("gb", gbc.mediaShape)
    }

    @Test
    fun nearestMappingsFollowCartridgesEntries() {
        assertEquals("mastersystem", CartLabelTargets.of("SG-1000")!!.systemId)
        assertEquals("gb", CartLabelTargets.of("Super Game Boy")!!.systemId)
        assertEquals("wonderswancolor", CartLabelTargets.of("Pocket Challenge V2")!!.systemId)
    }

    @Test
    fun systemsCartridgeLacksHaveNoTarget() {
        assertNull(CartLabelTargets.of("Neo Geo CD"))
        assertNull(CartLabelTargets.of("SuperGrafx"))
        assertNull(CartLabelTargets.of("ColecoVision"))
        assertNull(CartLabelTargets.of("ZX Spectrum"))
        assertNull(CartLabelTargets.of("MSX2"))
        assertNull(CartLabelTargets.of("Mega LD"))
    }

    @Test
    fun regionDefaultsToCartridgesDefaultOrderAndCanBeOverridden() {
        assertEquals("US", CartLabelTargets.of("Famicom")!!.region)
        assertEquals("JP", CartLabelTargets.of("Famicom", "JP")!!.region)
    }
}
