package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class ArtSystemsTest {
    /** The systems the native library lists (PhobosJNI.cpp's systemExtensions). */
    private val phobosSystems = listOf(
        "Atari 2600", "ColecoVision", "Famicom", "Super Famicom", "Super Game Boy", "Arcade", "Mega LD",
        "PC Engine LD", "Nintendo 64", "Game Boy", "Game Boy Color", "Game Boy Advance", "SG-1000", "Master System",
        "Mega Drive", "Mega 32X", "Game Gear", "Mega CD", "Mega CD 32X", "PlayStation", "PlayStation Portable",
        "Neo Geo", "Neo Geo CD", "Neo Geo Pocket", "Neo Geo Pocket Color", "ZX Spectrum", "ZX Spectrum 128",
        "PC Engine", "PC Engine CD", "SuperGrafx", "WonderSwan", "WonderSwan Color", "Pocket Challenge V2", "MSX",
        "MSX2",
    )

    @Test fun everySystemHasFoldersOrIsNamedAsHavingNone() {
        val jni = File("src/main/cpp/PhobosJNI.cpp").readText()
        for (system in phobosSystems) {
            assertTrue("$system is the native library's", jni.contains("{\"$system\","))
            assertTrue("$system: folders or none, not both", hasArt(system) != (system in systemsWithoutArt))
            if (hasArt(system)) assertTrue(artFolders(system, "x.bin").isNotEmpty())
        }
        assertEquals(setOf("Mega LD", "PC Engine LD"), systemsWithoutArt)
    }

    @Test fun systemsWithNoFolderOfTheirOwnLookInTheNearestOnes() {
        assertEquals(listOf("Nintendo - Game Boy"), artFolders("Super Game Boy", "Tetris.gb"))
        assertEquals(listOf("Sega - Mega-CD - Sega CD"), artFolders("Mega CD 32X", "Night Trap.cue"))
        assertEquals(listOf("Bandai - WonderSwan Color"), artFolders("Pocket Challenge V2", "Game.pc2"))
    }

    @Test fun aFileKindWithAFolderOfItsOwnLooksThereFirst() {
        assertEquals(
            listOf("Nintendo - Family Computer Disk System", "Nintendo - Nintendo Entertainment System"),
            artFolders("Famicom", "Zelda no Densetsu (Japan).fds"),
        )
        assertEquals("Nintendo - Nintendo Entertainment System", artFolders("Famicom", "Contra (USA).nes").first())
        assertEquals("Nintendo - Satellaview", artFolders("Super Famicom", "BS Zelda.bs").first())
        assertEquals("Nintendo - Nintendo 64DD", artFolders("Nintendo 64", "F-Zero X Expansion Kit.ndd").first())
    }

    @Test fun anArcadeGameGoesByItsTitleInPhobossArcadeList() {
        val titles = arcadeTitles(File("src/main/assets/Database/Arcade.bml").readText())
        assertEquals(18, titles.size)
        assertEquals("Champion Boxing", titles["chboxing"])
        assertEquals(listOf("Champion Boxing"), artLookups(ArtGame(GameKey("Arcade", "chboxing.zip")), titles))
        assertEquals(emptyList<String>(), artLookups(ArtGame(GameKey("Arcade", "unknown.zip")), titles))
        assertFalse("no loose matching on Arcade", fuzzyArtMatch("Arcade"))
        assertTrue(fuzzyArtMatch("Neo Geo"))
    }

    @Test fun aPspDiscsOwnTitleIsTheSecondNameLookedFor() {
        val key = GameKey("PlayStation Portable", "ULUS10041.iso")
        assertEquals(listOf("ULUS10041", "Lumines"), artLookups(ArtGame(key, "Lumines"), emptyMap()))
        val lumines = GameKey("PlayStation Portable", "Lumines (USA).iso")
        val once = artLookups(ArtGame(lumines, "Lumines (USA)"), emptyMap())
        assertEquals("the same name once", listOf("Lumines (USA)"), once)
        val tetris = GameKey("Game Boy", "Tetris (World).gb")
        assertEquals("no blank title", listOf("Tetris (World)"), artLookups(ArtGame(tetris, " "), emptyMap()))
    }
}
