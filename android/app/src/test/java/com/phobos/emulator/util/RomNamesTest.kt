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
}
