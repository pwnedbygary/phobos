package com.phobos.emulator.util

import com.phobos.emulator.util.N64SaveKind.CONTROLLER_PAK
import com.phobos.emulator.util.N64SaveKind.EEPROM
import com.phobos.emulator.util.N64SaveKind.FLASH
import com.phobos.emulator.util.N64SaveKind.SRAM
import com.phobos.emulator.util.N64SaveTransfer.FLASH_SIZE
import com.phobos.emulator.util.N64SaveTransfer.PAK_SIZE
import com.phobos.emulator.util.N64SaveTransfer.SRAM_SIZE
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.zip.Deflater

class N64SaveTransferTest {
    private fun ascii(text: String) = text.toByteArray(Charsets.US_ASCII)
    private fun filled(size: Int, value: Int) = ByteArray(size) { value.toByte() }
    private fun sram(text: String) = ByteArray(SRAM_SIZE).also { ascii(text).copyInto(it, 0x10) }
    private fun ok(read: N64SaveRead) = (read as N64SaveRead.Ok).source
    private fun source(name: String, vararg parts: Pair<N64SaveKind, ByteArray>) =
        N64SaveSource(name, N64SaveFormat.MUPEN64PLUS, mapOf(*parts))
    private fun sha256(data: ByteArray) = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }

    // RetroArch's layout: EEPROM, four paks, SRAM and FlashRAM, the last two word-swapped.
    private fun srm(eeprom: ByteArray, pak: ByteArray, sram: ByteArray, flash: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(eeprom)
        out.write(pak)
        repeat(3) { out.write(N64SaveTransfer.blankPak()) }
        out.write(N64SaveTransfer.wordSwap(sram))
        out.write(N64SaveTransfer.wordSwap(flash))
        return out.toByteArray()
    }

    // As RetroArch compresses saves: "#RZIPv\u0001#", chunk size, total size, then sized zlib chunks.
    private fun rzip(data: ByteArray, chunkSize: Int = 0x20000): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(ascii("#RZIPv"))
        out.write(1)
        out.write('#'.code)
        out.write(ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN).putInt(chunkSize).putLong(data.size.toLong()).array())
        for (start in data.indices step chunkSize) {
            val deflater = Deflater()
            deflater.setInput(data, start, minOf(chunkSize, data.size - start))
            deflater.finish()
            val compressed = ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (!deflater.finished()) compressed.write(buffer, 0, deflater.deflate(buffer))
            deflater.end()
            out.write(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(compressed.size()).array())
            out.write(compressed.toByteArray())
        }
        return out.toByteArray()
    }

    // Mupen64Plus's FlashRAM files read "iraMtS o yro" where Paper Mario's save says "Mario Story".
    @Test
    fun wordSwapReversesEachWord() {
        assertArrayEquals(ascii("iraMtS o yro"), N64SaveTransfer.wordSwap(ascii("Mario Story ")))
        val odd = ByteArray(10) { it.toByte() }
        assertArrayEquals(odd, N64SaveTransfer.wordSwap(N64SaveTransfer.wordSwap(odd)))
    }

    @Test
    fun mupenSramAndFlashAreSwappedToN64Order() {
        val n64 = sram("ZELDAZ  ")
        val read = ok(N64SaveTransfer.read("Zelda.sra", N64SaveTransfer.wordSwap(n64)))
        assertEquals(N64SaveFormat.MUPEN64PLUS, read.format)
        assertArrayEquals(n64, read.parts[SRAM])
        assertArrayEquals(n64, ok(N64SaveTransfer.read("Zelda.sra", n64, cartridgeOrder = true)).parts[SRAM])
        val flash = filled(FLASH_SIZE, 0xFF).also { ascii("Mario Story 006 ").copyInto(it) }
        assertArrayEquals(flash, ok(N64SaveTransfer.read("Paper Mario.fla", N64SaveTransfer.wordSwap(flash))).parts[FLASH])
    }

    @Test
    fun eepromPakAndPhobosFilesKeepTheirBytes() {
        val eeprom = filled(0x800, 0x5A)
        assertArrayEquals(eeprom, ok(N64SaveTransfer.read("Game.eep", eeprom)).parts[EEPROM])
        assertArrayEquals(eeprom.copyOf(0x200), ok(N64SaveTransfer.read("Game.EEP", eeprom.copyOf(0x200))).parts[EEPROM])
        val pak = N64SaveTransfer.blankPak().also { it[0x300] = 0x4E }
        val mpk = pak + N64SaveTransfer.blankPak() + N64SaveTransfer.blankPak() + N64SaveTransfer.blankPak()
        assertArrayEquals(pak, ok(N64SaveTransfer.read("Game.mpk", mpk)).parts[CONTROLLER_PAK])
        val sram = sram("ZELDAZ  ")
        for ((name, data) in listOf("save.eeprom" to eeprom, "save.ram" to sram, "save.flash" to filled(FLASH_SIZE, 7), "save.pak" to pak)) {
            val read = ok(N64SaveTransfer.read(name, data))
            assertEquals(N64SaveFormat.PHOBOS, read.format)
            assertArrayEquals(data, read.parts.values.single())
        }
    }

    @Test
    fun wrongSizesAndOtherFilesAreRefused() {
        assertTrue(N64SaveTransfer.read("Game.eep", ByteArray(0x300)) is N64SaveRead.Unreadable)
        assertTrue(N64SaveTransfer.read("Game.mpk", ByteArray(0x9000)) is N64SaveRead.Unreadable)
        assertTrue(N64SaveTransfer.read("Game.sra", ByteArray(0)) is N64SaveRead.Unreadable)
        assertTrue(N64SaveTransfer.read("Game.srm", ByteArray(0x8000)) is N64SaveRead.Unreadable)
        assertTrue(N64SaveTransfer.read("notes.txt", ByteArray(0x800)) is N64SaveRead.Unreadable)
    }

    @Test
    fun retroArchSavesReadPlainOrCompressed() {
        val eeprom = filled(0x800, 0xFF).also { ascii("CONKER").copyInto(it) }
        val sram = sram("ZELDAZ  ")
        val file = srm(eeprom, N64SaveTransfer.blankPak(), sram, filled(FLASH_SIZE, 0xFF))
        for (bytes in listOf(file, rzip(file), rzip(file, chunkSize = 0x10000))) {
            val read = ok(N64SaveTransfer.read("Game.srm", bytes))
            assertEquals(N64SaveFormat.RETROARCH, read.format)
            assertEquals(setOf(EEPROM, SRAM), read.parts.keys)
            assertArrayEquals(eeprom, read.parts[EEPROM])
            assertArrayEquals(sram, read.parts[SRAM])
            assertEquals(listOf(FLASH, CONTROLLER_PAK), read.emptyParts)
        }
    }

    @Test
    fun damagedCompressionIsRefused() {
        val file = srm(filled(0x800, 1), N64SaveTransfer.blankPak(), sram("A   "), filled(FLASH_SIZE, 2))
        val compressed = rzip(file)
        assertTrue(N64SaveTransfer.read("Game.srm", compressed.copyOf(compressed.size - 40)) is N64SaveRead.Unreadable)
        assertEquals(null, N64SaveTransfer.rzipDecode(compressed.copyOf(19)))
    }

    @Test
    fun blankPakMatchesTheOneAresFormats() {
        val blank = N64SaveTransfer.blankPak()
        // A Controller Pak ares formatted in Phobos, identical to RetroArch's unused paks.
        assertEquals("da9edb0602aab9f1130efdea82acc4ebbf891669b9308709a0113578076bd994", sha256(blank))
        assertTrue(N64SaveTransfer.isBlankPak(blank))
        assertFalse(N64SaveTransfer.isBlankPak(blank.copyOf().also { it[0x4FF] = 1 }))
    }

    @Test
    fun planFitsEachSaveToTheGame() {
        val eeprom16 = filled(0x800, 0x11)
        val fourKbit = mapOf(EEPROM to 0x200)
        val plan = N64SaveTransfer.plan(listOf(source("a.eep", EEPROM to eeprom16)), fourKbit)
        assertArrayEquals(eeprom16.copyOf(0x200), plan.writes[EEPROM])
        assertEquals("a.eep", plan.sources[EEPROM])
        assertTrue(plan.notes.isEmpty())

        val tooSmall = N64SaveTransfer.plan(listOf(source("b.eep", EEPROM to filled(0x200, 1))), mapOf(EEPROM to 0x800))
        assertTrue(tooSmall.writes.isEmpty())
        assertEquals(1, tooSmall.notes.size)

        val mixed = N64SaveTransfer.plan(
            listOf(
                source("c.eep", EEPROM to filled(0x200, 2)),
                source("d.sra", SRAM to filled(SRAM_SIZE, 3)),
                source("e.mpk", CONTROLLER_PAK to N64SaveTransfer.blankPak()),
                source("f.eep", EEPROM to filled(0x200, 4)),
            ),
            fourKbit,
        )
        assertEquals(setOf(EEPROM), mixed.writes.keys)
        assertArrayEquals(filled(0x200, 2), mixed.writes[EEPROM])
        assertEquals(3, mixed.notes.size)
    }

    @Test
    fun emptyPartsAreNotedOnlyForTheGamesSaveTypes() {
        val retroArch = N64SaveSource("Game.srm", N64SaveFormat.RETROARCH, mapOf(EEPROM to filled(0x800, 9)), listOf(FLASH, CONTROLLER_PAK))
        val plan = N64SaveTransfer.plan(listOf(retroArch), mapOf(EEPROM to 0x800, CONTROLLER_PAK to PAK_SIZE))
        assertEquals(setOf(EEPROM), plan.writes.keys)
        assertEquals(1, plan.notes.size)
        assertTrue(plan.notes.single().contains("Controller Pak"))
    }

    @Test
    fun exportsReadBackToTheSameSaves() {
        val saves = mapOf(
            EEPROM to filled(0x200, 0x21),
            SRAM to sram("ZELDAZ  "),
            CONTROLLER_PAK to N64SaveTransfer.blankPak().also { it[0x300] = 1 },
        )
        val game = mapOf(EEPROM to 0x200, SRAM to SRAM_SIZE, CONTROLLER_PAK to PAK_SIZE)

        val mupen = N64SaveTransfer.export(N64SaveFormat.MUPEN64PLUS, saves, "Game")
        assertEquals(listOf("Game.eep", "Game.sra", "Game.mpk"), mupen.map { it.first })
        assertEquals(0x800, mupen[0].second.size)
        assertEquals(4 * PAK_SIZE, mupen[2].second.size)
        val fromMupen = N64SaveTransfer.plan(mupen.map { (name, data) -> ok(N64SaveTransfer.read(name, data)) }, game)
        saves.forEach { (kind, data) -> assertArrayEquals(data, fromMupen.writes[kind]) }

        val retroArch = N64SaveTransfer.export(N64SaveFormat.RETROARCH, saves, "Game").single()
        assertEquals("Game.srm", retroArch.first)
        assertEquals(0x48800, retroArch.second.size)
        val fromRetroArch = N64SaveTransfer.plan(listOf(ok(N64SaveTransfer.read(retroArch.first, retroArch.second))), game)
        saves.forEach { (kind, data) -> assertArrayEquals(data, fromRetroArch.writes[kind]) }

        val phobos = N64SaveTransfer.export(N64SaveFormat.PHOBOS, saves, "Game")
        assertEquals(listOf("Game/save.eeprom", "Game/save.ram", "Game/save.pak"), phobos.map { it.first })
        phobos.forEach { (name, data) -> assertArrayEquals(saves[N64SaveKind.ofFileName(name.substringAfter('/'))], data) }
    }
}
