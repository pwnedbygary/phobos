package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class MsxTapesTest {
    @Test
    fun aTapeNeedsABasicBios() {
        assertEquals(listOf("fw_msx_basic"), msxTapeFirmware("MSX", "Game.cas", emptySet()))
        assertEquals(listOf("fw_msx_basic"), msxTapeFirmware("MSX", "Game.WAV", setOf("fw_msx2_main")))
        assertEquals(emptyList<String>(), msxTapeFirmware("MSX", "Game.tzx", setOf("fw_msx")))
    }

    @Test
    fun anMsx2TapeNeedsTheMainAndSubBios() {
        assertEquals(listOf("fw_msx2_basic"), msxTapeFirmware("MSX2", "Game.tsx", setOf("fw_msx2_main")))
        assertEquals(emptyList<String>(), msxTapeFirmware("MSX2", "Game.cas", setOf("fw_msx2_main", "fw_msx2_sub")))
    }

    @Test
    fun cartridgesAndOtherSystemsNeedNothing() {
        assertEquals(emptyList<String>(), msxTapeFirmware("MSX", "Metal Gear.rom", emptySet()))
        assertEquals(emptyList<String>(), msxTapeFirmware("ZX Spectrum", "Jetpac.tzx", emptySet()))
        assertEquals(emptyList<String>(), msxTapeFirmware("MSX", "Game", emptySet()))
    }
}
