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

    @Test
    fun theDeckStateNamesTheTapeAndItsRecording() {
        assertEquals(MsxDeck(), MsxDeck.of(IntArray(0)))
        assertEquals(MsxDeck(ZxTape(true, true, 1_500, 30_000)), MsxDeck.of(intArrayOf(1, 1, 1_500, 30_000)))
        assertEquals(
            MsxDeck(ZxTape(true, false, 4_000, 5_000), dataTape = true, recordArmed = true, recording = true),
            MsxDeck.of(intArrayOf(1, 0, 4_000, 5_000, 1, 1, 1)),
        )
    }

    @Test
    fun aBlankOrRecordingDataTapeSaysSo() {
        assertEquals("Blank", MsxDeck(ZxTape(true), dataTape = true).status)
        assertEquals("Blank; records when the MSX saves", MsxDeck(ZxTape(true), dataTape = true, recordArmed = true).status)
        assertEquals("Recording, 0:12", MsxDeck(ZxTape(true, false, 12_400, 13_000), dataTape = true, recordArmed = true, recording = true).status)
        assertEquals("Records after the 0:20 already on it", MsxDeck(ZxTape(true, false, 0, 20_000), dataTape = true, recordArmed = true).status)
        assertEquals("At the start, 0:20 long", MsxDeck(ZxTape(true, false, 0, 20_000), dataTape = true).status)
    }
}
