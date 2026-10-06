package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException
import java.io.InputStream

class PspFontsTest {
    @get:Rule
    val temp = TemporaryFolder()

    private fun source(name: String, text: String) =
        PspFonts.Source(name, text.length.toLong()) { text.byteInputStream() }

    /** A stream of [size] bytes, made as it's read. */
    private fun bytes(size: Long) = object : InputStream() {
        var given = 0L
        override fun read(): Int = if (given++ < size) 'x'.code else -1
    }

    @Test
    fun theEighteenInTheirOrder() {
        assertEquals(18, PspFonts.NAMES.size)
        assertEquals("jpn0.pgf", PspFonts.NAMES.first())
        assertEquals("ltn15.pgf", PspFonts.NAMES[16])
        assertEquals("kr0.pgf", PspFonts.NAMES.last())
    }

    @Test
    fun theCopiesGoInTheAppsOwnFirmware() {
        val files = File("/data/user/0/com.phobos.emulator/files")
        assertEquals(File(files, "firmware/PlayStation Portable/font"), PspFonts.folder(files))
    }

    @Test
    fun onlyTheEighteenAreFonts() {
        assertTrue(PspFonts.isFont("ltn0.pgf"))
        assertTrue(PspFonts.isFont("JPN0.PGF"))
        assertTrue(PspFonts.isFont("Kr0.Pgf"))
        assertFalse(PspFonts.isFont("ltn16.pgf"))
        assertFalse(PspFonts.isFont("ltn99.pgf"))
        assertFalse(PspFonts.isFont("notes.pgf"))
        assertFalse(PspFonts.isFont("gb3s1518.bwfon"))
        assertFalse(PspFonts.isFont("imagefont.bin"))
        assertFalse(PspFonts.isFont(".pgf"))
        assertFalse(PspFonts.isFont("../ltn0.pgf"))
        assertFalse(PspFonts.isFont("font\\ltn0.pgf"))
        assertFalse(PspFonts.isFont(".ltn0.pgf"))
    }

    @Test
    fun copiesTheFontsAndOnlyThem() {
        val folder = File(temp.root, "font")
        val files = listOf(
            source("ltn0.pgf", "latin"),
            source("JPN0.PGF", "japanese"),
            source("ltn99.pgf", "not one of the eighteen"),
            source("imagefont.bin", "not a font"),
            source("../kr0.pgf", "escapes"),
        )
        assertEquals(PspFonts.Copied(2, emptyList()), PspFonts.copy(files, folder))
        assertEquals("latin", File(folder, "ltn0.pgf").readText())
        assertEquals("japanese", File(folder, "jpn0.pgf").readText())
        assertEquals(setOf("ltn0.pgf", "jpn0.pgf"), folder.list()!!.toSet())
        assertFalse(File(temp.root, "kr0.pgf").exists())
        assertEquals(2, PspFonts.installed(folder))
    }

    @Test
    fun aFontReplacesItsOldCopyOnlyOnceWhole() {
        val folder = File(temp.root, "font").apply { mkdirs() }
        File(folder, "ltn1.pgf").writeText("old")
        val failing = object : InputStream() {
            var given = 0
            override fun read(): Int = if (given++ < 3) 'x'.code else throw IOException("unplugged")
        }
        val unplugged = PspFonts.copy(listOf(PspFonts.Source("ltn1.pgf", -1) { failing }), folder)
        assertEquals(PspFonts.Copied(0, listOf("ltn1.pgf")), unplugged)
        assertEquals("old", File(folder, "ltn1.pgf").readText())
        assertEquals(listOf("ltn1.pgf"), folder.list()!!.toList())  //no part copy left behind
        val nothing = listOf(PspFonts.Source("ltn2.pgf", -1) { null }, source("ltn3.pgf", ""))
        assertEquals(PspFonts.Copied(0, listOf("ltn2.pgf", "ltn3.pgf")), PspFonts.copy(nothing, folder))
        assertEquals(PspFonts.Copied(1, emptyList()), PspFonts.copy(listOf(source("ltn1.pgf", "new")), folder))
        assertEquals("new", File(folder, "ltn1.pgf").readText())
    }

    @Test
    fun aFontIsOneByteToFourMiB() {
        val folder = File(temp.root, "font")
        val most = PspFonts.MAX_BYTES
        val files = listOf(
            PspFonts.Source("ltn4.pgf", most) { bytes(most) },
            PspFonts.Source("ltn5.pgf", most + 1) { bytes(most + 1) },       //too big, as its size says
            PspFonts.Source("ltn6.pgf", -1) { bytes(most + 1) },             //too big, its size unknown
            PspFonts.Source("ltn7.pgf", 10) { bytes(most + 100) },           //bigger than it said
            PspFonts.Source("ltn8.pgf", 1) { bytes(1) },
        )
        val result = PspFonts.copy(files, folder)
        assertEquals(PspFonts.Copied(2, listOf("ltn5.pgf", "ltn6.pgf", "ltn7.pgf")), result)
        assertEquals(most, File(folder, "ltn4.pgf").length())
        assertEquals(setOf("ltn4.pgf", "ltn8.pgf"), folder.list()!!.toSet())  //no part copies left behind
        assertFalse(PspFonts.fits(0))
        assertTrue(PspFonts.fits(1) && PspFonts.fits(most))
        assertFalse(PspFonts.fits(most + 1))
    }

    @Test
    fun anyFailureLeavesThatFontAloneAndTheRestCopied() {
        val folder = File(temp.root, "font").apply { mkdirs() }
        File(folder, "ltn9.pgf").writeText("old")
        val breaking = object : InputStream() {
            var given = 0
            override fun read(): Int = if (given++ < 5) 'x'.code else throw IllegalStateException("provider gone")
        }
        val files = listOf(
            PspFonts.Source("ltn9.pgf", -1) { breaking },
            PspFonts.Source("ltn10.pgf", -1) { throw SecurityException("no grant") },
            source("ltn11.pgf", "fine"),
        )
        assertEquals(PspFonts.Copied(1, listOf("ltn9.pgf", "ltn10.pgf")), PspFonts.copy(files, folder))
        assertEquals("old", File(folder, "ltn9.pgf").readText())
        assertEquals("fine", File(folder, "ltn11.pgf").readText())
        assertEquals(setOf("ltn9.pgf", "ltn11.pgf"), folder.list()!!.toSet())
    }

    @Test
    fun failuresAreToldApartFromNoneFound() {
        assertEquals("Couldn't read that folder", PspFonts.pickedMessage(null, 0))
        assertEquals(
            "No PSP fonts (jpn0, ltn0-ltn15, kr0.pgf) in that folder",
            PspFonts.pickedMessage(PspFonts.Copied(0, emptyList()), 0),
        )
        assertEquals(
            "Couldn't copy the PSP fonts in that folder: ltn2.pgf, kr0.pgf",
            PspFonts.pickedMessage(PspFonts.Copied(0, listOf("ltn2.pgf", "kr0.pgf")), 0),
        )
        assertEquals("Copied 1 PSP font: 3 of the 18 now", PspFonts.pickedMessage(PspFonts.Copied(1, emptyList()), 3))
        assertEquals(
            "Copied 17 PSP fonts: 17 of the 18 now. Couldn't copy: JPN0.PGF",
            PspFonts.pickedMessage(PspFonts.Copied(17, listOf("JPN0.PGF")), 17),
        )
    }

    @Test
    fun countsTheEighteenWhateverTheirCase() {
        val folder = File(temp.root, "font").apply { mkdirs() }
        assertEquals(0, PspFonts.installed(File(temp.root, "none")))
        File(folder, "LTN0.PGF").writeText("x")
        File(folder, "kr0.pgf").writeText("x")
        File(folder, "ltn99.pgf").writeText("x")  //not one of the eighteen
        File(folder, "ltn2.pgf").writeText("")    //empty
        assertEquals(2, PspFonts.installed(folder))
        assertEquals(setOf("ltn0.pgf", "kr0.pgf"), PspFonts.installedNames(folder))
    }

    @Test
    fun findsTheFontsInWhicheverFolderWasPicked() {
        //a folder as name -> entries; an entry with entries of its own is a folder
        data class Node(val entries: Map<String, Node> = emptyMap())
        val font = Node(mapOf("ltn0.pgf" to Node(), "jpn0.pgf" to Node()))
        val flash0 = Node(mapOf("font" to font, "vsh" to Node(mapOf("etc" to Node()))))
        val dump = Node(mapOf("flash0" to flash0, "EBOOT.PBP" to Node()))
        val children = { node: Node ->
            node.entries.map { (name, child) -> Triple(name, child.entries.isNotEmpty() || name == "vsh", child) }
        }
        assertEquals(font, PspFonts.fontFolder(font, children))
        assertEquals(font, PspFonts.fontFolder(flash0, children))
        assertEquals(font, PspFonts.fontFolder(dump, children))
        assertEquals(font, PspFonts.fontFolder(Node(mapOf("FLASH0" to Node(mapOf("FONT" to font)))), children))
        assertNull(PspFonts.fontFolder(Node(mapOf("readme.txt" to Node())), children))
    }

    /** A device's shared storage with tools/psp-flash0-dump's folder in Download, its fonts in [fonts] inside it. */
    private fun storage(fonts: String = "flash0/font", names: List<String> = PspFonts.NAMES): File {
        val root = File(temp.root, "storage").apply { mkdirs() }
        val folder = File(root, "${PspFonts.DUMP_FOLDER}/$fonts").apply { mkdirs() }
        for (name in names) File(folder, name).writeText("dumped $name")
        File(folder, "imagefont.bin").writeText("not a font")
        File(root, "${PspFonts.DUMP_FOLDER}/EBOOT.PBP").writeText("the dumper")
        return root
    }

    @Test
    fun theDumpsFontsAreFoundByThemselves() {
        val storage = storage()
        val copies = PspFonts.folder(File(temp.root, "files"))
        val found = PspFonts.autoImport(storage, copies)!!
        assertEquals(File(storage, "Download/FLASH0DUMP/flash0/font"), found.from)
        assertEquals(18, found.copied)
        assertEquals(emptyList<String>(), found.failed)
        assertEquals(PspFonts.Held(18, true), PspFonts.held(copies))
        assertEquals("dumped kr0.pgf", File(copies, "kr0.pgf").readText())
        assertFalse(File(copies, "imagefont.bin").exists())
        //with all eighteen, nothing more is looked for (nor copied over)
        File(copies, "kr0.pgf").writeText("kept")
        assertNull(PspFonts.autoImport(storage, copies))
        assertEquals("kept", File(copies, "kr0.pgf").readText())
    }

    @Test
    fun onlyTheFontsTheAppHasntGotAreCopied() {
        val storage = storage(fonts = "font")  //the font folder straight inside the dump's
        val copies = PspFonts.folder(File(temp.root, "files")).apply { mkdirs() }
        File(copies, "jpn0.pgf").writeText("picked")
        File(copies, "ltn3.pgf").writeText("")  //empty: missing
        val found = PspFonts.autoImport(storage, copies)!!
        assertEquals(File(storage, "Download/FLASH0DUMP/font"), found.from)
        assertEquals(17, found.copied)
        assertEquals("picked", File(copies, "jpn0.pgf").readText())
        assertEquals("dumped ltn3.pgf", File(copies, "ltn3.pgf").readText())
        assertEquals(18, PspFonts.installed(copies))
    }

    @Test
    fun aDumpWithSomeFontsGivesThoseAndIsAskedAgainForTheRest() {
        val storage = storage(names = listOf("ltn0.pgf", "LTN1.PGF"))
        val copies = PspFonts.folder(File(temp.root, "files"))
        assertEquals(2, PspFonts.autoImport(storage, copies)!!.copied)
        assertEquals(setOf("ltn0.pgf", "ltn1.pgf"), PspFonts.installedNames(copies))
        val again = PspFonts.autoImport(storage, copies)!!  //fewer than eighteen: looked at again, nothing new
        assertEquals(0, again.copied)
        assertTrue(again.from != null)
    }

    @Test
    fun noDumpOrNoneReadableLeavesThePickerToDoIt() {
        val copies = PspFonts.folder(File(temp.root, "files"))
        val empty = File(temp.root, "empty").apply { mkdirs() }
        assertEquals(PspFonts.Found(null, 0, emptyList()), PspFonts.autoImport(empty, copies))
        val noFonts = storage(names = emptyList())
        assertEquals(PspFonts.Found(null, 0, emptyList()), PspFonts.autoImport(noFonts, copies))
        assertEquals(PspFonts.Held(0, false), PspFonts.held(copies))
        //a dump the app isn't let into (as Android leaves another app's files without a grant), or whose fonts are
        //listed but don't open: nothing
        val shut = storage()
        val dump = File(shut, PspFonts.DUMP_FOLDER)
        val fonts = File(dump, "flash0/font").listFiles()!!.toList()
        fonts.forEach { it.setReadable(false) }
        try {
            assumeFalse("permissions don't hold back this user", fonts.first().canRead())
            assertNull(PspFonts.dumpFonts(shut))
            assertEquals(PspFonts.Found(null, 0, emptyList()), PspFonts.autoImport(shut, copies))
            listOf(File(dump, "flash0/font"), File(dump, "flash0"), dump).forEach { it.setReadable(false) }
            assertNull(PspFonts.dumpFonts(shut))
            assertEquals(PspFonts.Found(null, 0, emptyList()), PspFonts.autoImport(shut, copies))
        } finally {
            listOf(dump, File(dump, "flash0"), File(dump, "flash0/font")).forEach { it.setReadable(true) }
            fonts.forEach { it.setReadable(true) }
        }
        assertEquals(File(dump, "flash0/font"), PspFonts.dumpFonts(shut))
    }

    @Test
    fun aPickedFolderTakesTheFoundMarkAway() {
        val storage = storage(names = listOf("ltn0.pgf"))
        val copies = PspFonts.folder(File(temp.root, "files"))
        PspFonts.autoImport(storage, copies)
        assertEquals(PspFonts.Held(1, true), PspFonts.held(copies))
        assertEquals(PspFonts.Copied(0, emptyList()), PspFonts.copyPicked(listOf(source("readme.txt", "x")), copies))
        assertEquals(PspFonts.Held(1, true), PspFonts.held(copies))  //nothing copied: still the found ones
        val picked = PspFonts.copyPicked(listOf(source("ltn2.pgf", "picked")), copies)
        assertEquals(PspFonts.Copied(1, emptyList()), picked)
        assertEquals(PspFonts.Held(2, false), PspFonts.held(copies))
    }
}
