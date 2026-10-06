package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException
import java.io.InputStream

class PspFontsTest {
    @get:Rule
    val temp = TemporaryFolder()

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
    fun onlyPlainPgfNamesAreFonts() {
        assertTrue(PspFonts.isFont("ltn0.pgf"))
        assertTrue(PspFonts.isFont("JPN0.PGF"))
        assertFalse(PspFonts.isFont("gb3s1518.bwfon"))
        assertFalse(PspFonts.isFont("imagefont.bin"))
        assertFalse(PspFonts.isFont(".pgf"))
        assertFalse(PspFonts.isFont("../ltn0.pgf"))
        assertFalse(PspFonts.isFont("font\\ltn0.pgf"))
        assertFalse(PspFonts.isFont(".hidden.pgf"))
    }

    @Test
    fun copiesTheFontsAndOnlyThem() {
        val folder = File(temp.root, "font")
        val files = listOf(
            "ltn0.pgf" to { "latin".byteInputStream() as InputStream? },
            "JPN0.PGF" to { "japanese".byteInputStream() as InputStream? },
            "imagefont.bin" to { "not a font".byteInputStream() as InputStream? },
            "../kr0.pgf" to { "escapes".byteInputStream() as InputStream? },
        )
        assertEquals(2, PspFonts.copy(files, folder))
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
        assertEquals(0, PspFonts.copy(listOf("ltn1.pgf" to { failing as InputStream? }), folder))
        assertEquals("old", File(folder, "ltn1.pgf").readText())
        assertEquals(listOf("ltn1.pgf"), folder.list()!!.toList())  //no part copy left behind
        assertEquals(0, PspFonts.copy(listOf("ltn2.pgf" to { null }, "ltn3.pgf" to { "".byteInputStream() }), folder))
        assertEquals(1, PspFonts.copy(listOf("ltn1.pgf" to { "new".byteInputStream() as InputStream? }), folder))
        assertEquals("new", File(folder, "ltn1.pgf").readText())
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
}
