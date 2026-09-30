package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DiscSetsTest {
    @Test
    fun readsTheDiscNumber() {
        assertEquals(2, discNumber("Final Fantasy VII (USA) (Disc 2).chd"))
        assertEquals(1, discNumber("Parasite Eve (USA) (Disc 1 of 2).cue"))
        assertEquals(3, discNumber("Chrono Cross [CD3].chd"))
        assertNull(discNumber("Crash Bandicoot (USA).chd"))
        assertNull(discNumber("Discworld (USA).cue"))
    }

    @Test
    fun readsAnM3uPlaylist() {
        val text = "\uFEFF#EXTM3U\r\nMetal Gear Solid (USA) (Disc 1).chd\r\n\r\n# second disc\r\n  Discs\\Metal Gear Solid (USA) (Disc 2).chd  \r\n"
        assertEquals(
            listOf("Metal Gear Solid (USA) (Disc 1).chd", "Discs/Metal Gear Solid (USA) (Disc 2).chd"),
            m3uEntries(text),
        )
    }

    @Test
    fun followsAnM3uEntrysPathFromThePlaylist() {
        val playlist = "EBFF-F6C0:ROMs/psx/Metal Gear Solid/Metal Gear Solid.m3u"
        assertEquals("EBFF-F6C0:ROMs/psx/Metal Gear Solid/Disc 1.chd", m3uEntryPath(playlist, "Disc 1.chd"))
        assertEquals("EBFF-F6C0:ROMs/psx/Metal Gear Solid/discs/Disc 2.chd", m3uEntryPath(playlist, "./discs/Disc 2.chd"))
        assertEquals("/storage/ROMs/psx/Game/Game.chd", m3uEntryPath("/storage/ROMs/psx/Playlists/Game.m3u", "../Game/Game.chd"))
        assertEquals("/storage/ROMs/Game.chd", m3uEntryPath("/storage/ROMs/psx/Game.m3u", "/storage/ROMs/Game.chd"))
    }

    @Test
    fun readsACueSheetsTracks() {
        val text = """
            FILE "Game (Track 1).bin" BINARY
              TRACK 01 MODE2/2352
                INDEX 01 00:00:00
            FILE Game_Track2.bin BINARY
              TRACK 02 AUDIO
                INDEX 01 00:00:00
        """.trimIndent()
        assertEquals(listOf("Game (Track 1).bin", "Game_Track2.bin"), cueTracks(text))
    }

    @Test
    fun movesACueSheetsTracksIntoAFolder() {
        val text = "FILE \"Game (Track 1).bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\nFILE data\\Game_Track2.bin BINARY\r\n"
        assertEquals(
            "FILE \"tracks/Game (Track 1).bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\nFILE \"tracks/Game_Track2.bin\" BINARY\r\n",
            cueWithTracksIn(text, "tracks"),
        )
    }

    private data class File(val name: String, val folder: String)

    @Test
    fun groupsTheDiscsOfAGame() {
        val files = listOf(
            File("Metal Gear Solid (USA) (Disc 2) (Rev 1).chd", "psx"),
            File("Metal Gear Solid (USA) (Disc 1) (Rev 1).chd", "psx"),
            File("Crash Bandicoot (USA).chd", "psx"),
            File("Final Fantasy VII (USA) (Disc 1).cue", "psx"),
            File("Final Fantasy VII (USA) (Disc 1).chd", "psx"),
            File("Tomba (USA) (Disc 1).chd", "psx"),
            File("Tomba (USA) (Disc 2).chd", "other"),
        )
        val (sets, singles) = groupDiscSets(files, { it.name }, { it.folder })
        assertEquals(1, sets.size)
        assertEquals("Metal Gear Solid (USA) (Rev 1)", sets[0].title)
        assertEquals(listOf(files[1], files[0]), sets[0].discs)
        assertEquals(files.drop(2), singles)
    }
}
