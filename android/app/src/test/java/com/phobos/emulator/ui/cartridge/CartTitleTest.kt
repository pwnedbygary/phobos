package com.phobos.emulator.ui.cartridge

import org.junit.Assert.assertEquals
import org.junit.Test

class CartTitleTest {

    @Test
    fun stripsExtensionAndTags() {
        assertEquals(
            "Chrono Trigger - Refined Text Edition",
            cartTitle("Chrono Trigger - Refined Text Edition [v2.0].zip"),
        )
        assertEquals(
            "Super Mario All-Stars & Super Mario World",
            cartTitle("Super Mario All-Stars & Super Mario World (USA).sfc"),
        )
        assertEquals("Sonic the Hedgehog", cartTitle("Sonic the Hedgehog (USA) (Rev 1).md"))
        assertEquals("Metal Gear Solid", cartTitle("Metal Gear Solid (Disc 1).cue"))
    }

    @Test
    fun keepsADiscTitleWhenThereIsOne() {
        assertEquals("A PSP Title", cartTitle("ULUS00000.iso", "A PSP Title"))
    }

    @Test
    fun anEmptyResultFallsBackToTheFile() {
        assertEquals("(USA)", cartTitle("(USA).zip"))
    }
}
