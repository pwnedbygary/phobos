package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class RomNamesTest {
    @Test
    fun dropsTheExtension() {
        assertEquals("Knuckles' Chaotix (USA)", romTitle("Knuckles' Chaotix (USA).zip"))
        assertEquals("Samurai Spirits ~ Samurai Shodown (Japan) (En,Ja)", romTitle("Samurai Spirits ~ Samurai Shodown (Japan) (En,Ja).chd"))
        assertEquals("Mario Tennis (USA)", romTitle("Mario Tennis (USA).z64"))
    }

    @Test
    fun keepsDotsThatArePartOfTheTitle() {
        assertEquals("Dr. Mario", romTitle("Dr. Mario"))
        assertEquals("Dr. Mario (USA)", romTitle("Dr. Mario (USA).nes"))
        assertEquals("game.v1.2", romTitle("game.v1.2.nds"))
    }

    @Test
    fun keepsANameThatIsOnlyAnExtension() {
        assertEquals(".zip", romTitle(".zip"))
    }

    @Test
    fun dropsTheDiscNumber() {
        assertEquals("Metal Gear Solid (USA) (Rev 1)", withoutDiscNumber("Metal Gear Solid (USA) (Disc 1) (Rev 1)"))
        assertEquals("Metal Gear Solid (USA) (Rev 1)", withoutDiscNumber("Metal Gear Solid (USA) (Disc 2) (Rev 1)"))
        assertEquals("Final Fantasy VII (USA)", withoutDiscNumber("Final Fantasy VII (USA) (Disc 3)"))
        assertEquals("Parasite Eve (USA)", withoutDiscNumber("Parasite Eve (USA) (Disc 1 of 2)"))
        assertEquals("Chrono Cross", withoutDiscNumber("Chrono Cross [CD2]"))
        assertEquals("Lunar", withoutDiscNumber("Lunar (disk 2)"))
    }

    @Test
    fun keepsTitlesWithoutADiscNumber() {
        assertEquals("Crash Bandicoot (USA)", withoutDiscNumber("Crash Bandicoot (USA)"))
        assertEquals("Discworld (USA)", withoutDiscNumber("Discworld (USA)"))
        assertEquals("CD Games (Demo)", withoutDiscNumber("CD Games (Demo)"))
        assertEquals("(Disc 1)", withoutDiscNumber("(Disc 1)"))
    }
}
