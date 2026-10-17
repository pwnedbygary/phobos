package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.time.LocalDateTime

class ScreenshotsTest {
    @get:Rule
    val temp = TemporaryFolder()

    private val taken = LocalDateTime.of(2026, 10, 17, 14, 15, 45)
    private val shared = "/storage/emulated/0"
    private val app = "/data/user/0/com.phobos.emulator/files"

    private fun picture(bytes: String): (File) -> Boolean = { it.writeText(bytes); true }

    @Test
    fun aScreenshotIsNamedForTheGameAndWhenItWasTaken() {
        assertEquals("God of War - Ghost of Sparta 2026-10-17 14-15-45", Screenshots.name("God of War - Ghost of Sparta.chd", taken))
        // The extension goes as the pause menu's title drops it: a dot in the title stays.
        assertEquals("Dr. Mario (USA) 2026-10-17 14-15-45", Screenshots.name("Dr. Mario (USA).nes", taken))
        assertEquals("Dr. Mario 2026-10-17 14-15-45", Screenshots.name("Dr. Mario", taken))
        // Local time, 24-hour, every field two digits.
        assertEquals("Mario Tennis (USA) 2026-01-05 09-04-03", Screenshots.name("Mario Tennis (USA).z64", LocalDateTime.of(2026, 1, 5, 9, 4, 3)))
        assertEquals("Mario Tennis (USA) 2026-12-31 23-59-59", Screenshots.name("Mario Tennis (USA).z64", LocalDateTime.of(2026, 12, 31, 23, 59, 59)))
    }

    @Test
    fun whatAnSdCardWontTakeIsReplaced() {
        assertEquals("Grand Theft Auto_ Chinatown Wars_ 2026-10-17 14-15-45", Screenshots.name("Grand Theft Auto: Chinatown Wars?.iso", taken))
        assertEquals("a_b_c_d_e_f_g_h_i_ 2026-10-17 14-15-45", Screenshots.name("a\\b/c:d*e?f\"g<h>i|.zip", taken))
        // Control characters go; so do spaces around the title and dots before it (a hidden file).
        assertEquals("Bell 2026-10-17 14-15-45", Screenshots.name("Be\u0007ll\u0000.zip", taken))
        assertEquals("hidden 2026-10-17 14-15-45", Screenshots.name("  ..hidden .zip", taken))
        // Nothing left of the title: the name still says what it is.
        assertEquals("Screenshot 2026-10-17 14-15-45", Screenshots.name("....zip", taken))
        assertEquals("Screenshot 2026-10-17 14-15-45", Screenshots.name("", taken))
    }

    @Test
    fun aLongTitleIsCutToFitAnyStorageWithoutSplittingACharacter() {
        val time = " 2026-10-17 14-15-45"
        for (title in listOf("A".repeat(300), "ゼルダの伝説".repeat(50), "\uD83C\uDFAE".repeat(100), "a" + "\uD83C\uDFAE".repeat(100))) {
            val name = Screenshots.name("$title.iso", taken)
            assertTrue(name.endsWith(time))
            val kept = name.removeSuffix(time)
            assertTrue("$kept: ${kept.toByteArray().size} bytes", kept.toByteArray().size in 140..150)
            assertTrue("cut a character: $kept", title.startsWith(kept))
            assertEquals(kept, String(kept.toByteArray(Charsets.UTF_8), Charsets.UTF_8))
            // The whole name, numbered, fits the 255 bytes internal storage takes and FAT's 255 characters.
            assertTrue(("$name-999.png").toByteArray().size <= 255)
        }
    }

    @Test
    fun aShotNeverOverwritesAFile() {
        val folder = temp.newFolder("PlayStation Portable")
        val first = Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken, picture("first"))
        val second = Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken, picture("second"))
        val third = Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken, picture("third"))
        assertEquals(
            listOf(
                "God of War - Ghost of Sparta 2026-10-17 14-15-45.png",
                "God of War - Ghost of Sparta 2026-10-17 14-15-45-2.png",
                "God of War - Ghost of Sparta 2026-10-17 14-15-45-3.png",
            ),
            listOf(first, second, third).map { (it as ScreenshotResult.Saved).file.name },
        )
        assertEquals(listOf("first", "second", "third"), listOf(first, second, third).map { (it as ScreenshotResult.Saved).file.readText() })
        // A file already there under a shot's name, of whatever kind, is kept as it is.
        File(folder, "Mario Tennis (USA) 2026-10-17 14-15-45.png").writeText("someone else's")
        val after = Screenshots.save(folder, "Mario Tennis (USA).z64", taken, picture("new")) as ScreenshotResult.Saved
        assertEquals("Mario Tennis (USA) 2026-10-17 14-15-45-2.png", after.file.name)
        assertEquals("someone else's", File(folder, "Mario Tennis (USA) 2026-10-17 14-15-45.png").readText())
    }

    @Test
    fun theFolderIsMadeWhenItIsntThere() {
        val folder = File(temp.root, "Screenshots/PlayStation Portable")
        val result = Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken, picture("png")) as ScreenshotResult.Saved
        assertEquals(File(folder, "God of War - Ghost of Sparta 2026-10-17 14-15-45.png"), result.file)
        assertEquals("png", result.file.readText())
    }

    @Test
    fun noPictureLeavesNoFile() {
        val folder = temp.newFolder("PlayStation Portable")
        assertEquals(ScreenshotResult.NoPicture, Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken) { false })
        assertEquals(0, folder.listFiles()!!.size)
    }

    @Test
    fun aPictureLeftUnwrittenIsNoShot() {
        // The core had a picture but its file stayed empty (the PNG writer says it wrote even when a full disk refuses).
        val folder = temp.newFolder("PlayStation Portable")
        assertEquals(ScreenshotResult.NotWritable(folder), Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken) { true })
        assertEquals(0, folder.listFiles()!!.size)
    }

    @Test
    fun aFolderThatCantBeWrittenTakesNoShot() {
        val folder = File(temp.newFile("not a folder"), "PlayStation Portable")
        var captured = false
        val result = Screenshots.save(folder, "God of War - Ghost of Sparta.chd", taken) { captured = true; true }
        assertEquals(ScreenshotResult.NotWritable(folder), result)
        assertFalse(captured)
    }

    @Test
    fun theNoteSaysWhereTheShotWentOrWhyThereIsNone() {
        val saved = File("$shared/Emulation Settings/Phobos/Screenshots/PlayStation Portable/God of War - Ghost of Sparta 2026-10-17 14-15-45.png")
        // The pill's two lines, and what TalkBack reads out.
        assertEquals(
            ScreenshotNote("Screenshot saved", "Screenshots › PlayStation Portable", failed = false, spoken = "Screenshot saved to Screenshots, PlayStation Portable"),
            Screenshots.note(ScreenshotResult.Saved(saved), shared, app),
        )
        // An SD card's folder: its last two names too.
        assertEquals(
            "Pictures › PlayStation Portable",
            Screenshots.note(ScreenshotResult.Saved(File("/storage/1C1F-1234/Pictures/PlayStation Portable/a.png")), shared, app).detail,
        )
        // The app's own storage, which no file manager shows: where to choose another folder, shown even on a narrow
        // screen.
        assertEquals(
            ScreenshotNote(
                "Screenshot saved", "In Phobos's own storage: Settings → Paths → Screenshots Path sets a folder you can open", failed = false,
                spoken = "Screenshot saved in Phobos's own storage. Settings, Paths, Screenshots Path sets a folder you can open.",
                hint = true,
            ),
            Screenshots.note(ScreenshotResult.Saved(File("$app/screenshots/PlayStation Portable/a.png")), shared, app),
        )
        // A folder whose name only starts like the app's isn't in it.
        assertEquals(
            "filessystem › PlayStation Portable",
            Screenshots.note(ScreenshotResult.Saved(File("${app}system/PlayStation Portable/a.png")), shared, app).detail,
        )
        assertEquals(
            ScreenshotNote(
                "Screenshot not saved", "Can't write in Emulation Settings/Phobos/Screenshots/PlayStation Portable", failed = true,
                spoken = "Screenshot not saved: can't write in Emulation Settings/Phobos/Screenshots/PlayStation Portable",
            ),
            Screenshots.note(ScreenshotResult.NotWritable(File("$shared/Emulation Settings/Phobos/Screenshots/PlayStation Portable")), shared, app),
        )
        assertEquals(
            ScreenshotNote(
                "Screenshot not saved", "The game has no picture to save yet", failed = true,
                spoken = "Screenshot not saved: the game has no picture to save yet",
            ),
            Screenshots.note(ScreenshotResult.NoPicture, shared, app),
        )
    }
}
