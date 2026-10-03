package com.phobos.emulator.util

import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FirmwareIdsTest {
    private val psxUs = "11052b6499e466bbf0a709b1f9cb6834a9418e66680387912451e971cf8a1fef"
    private val psxEu = "1faaa18fa820a0225e488d9f086296b8e6c46df739666093987ff7d8fd352c09"
    private val smsUsEu = "477617917a12a30f9f43844909dc2de6e6a617430f5c9a36306c86414a670d50"
    private val systemCard3 = "e11527b3b96ce112a037138988ca72fd117a6b0779c2480d9e03eaebece3d9ce"
    private val neoGeoSet = setOf("5a86cff2", "c2ea0cfd", "9036d879", "a7aab458")

    private fun sha256(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun crc(bytes: ByteArray) = "%08x".format(CRC32().apply { update(bytes) }.value)

    private fun zip(vararg files: Pair<String, ByteArray>): ByteArray = ByteArrayOutputStream().also { out ->
        ZipOutputStream(out).use { zip ->
            zip.putNextEntry(ZipEntry("folder/"))
            zip.closeEntry()
            for ((name, data) in files) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(data)
                zip.closeEntry()
            }
        }
    }.toByteArray()

    @Test
    fun aFileIsKnownByItsContent() {
        assertEquals(setOf("fw_psx_us"), FirmwareIds.slotsFor(FirmwareContent(psxUs)))
        assertEquals(setOf("fw_psx_eu"), FirmwareIds.slotsFor(FirmwareContent(psxEu)))
        assertEquals(emptySet<String>(), FirmwareIds.slotsFor(FirmwareContent("0".repeat(64))))
        assertEquals(emptySet<String>(), FirmwareIds.slotsFor(FirmwareContent(null)))
    }

    @Test
    fun oneDumpCanBeSeveralSlotsBios() {
        assertEquals(setOf("fw_ms_us", "fw_ms_eu"), FirmwareIds.slotsFor(FirmwareContent(smsUsEu)))
        assertEquals(setOf("fw_pce_cd_3_jp", "fw_supergrafx_ac_jp"), FirmwareIds.slotsFor(FirmwareContent(systemCard3)))
    }

    @Test
    fun mameSetsAreKnownByTheCrcsOfTheFilesPhobosReads() {
        assertEquals(setOf("fw_ng_bios", "fw_ng_aes", "fw_ng_mvs"), FirmwareIds.slotsFor(FirmwareContent(null, neoGeoSet)))
        assertEquals(setOf("fw_aleck64"), FirmwareIds.slotsFor(FirmwareContent(null, setOf("5ec82be9", "12345678"))))
        assertEquals(emptySet<String>(), FirmwareIds.slotsFor(FirmwareContent(null, neoGeoSet - "9036d879")))
    }

    @Test
    fun aZippedBiosIsItsFirstFile() {
        assertEquals(setOf("fw_psx_eu"), FirmwareIds.slotsFor(FirmwareContent(psxEu, setOf("deadbeef"))))
    }

    @Test
    fun readsAPlainFile() {
        val data = ByteArray(5000) { (it * 7).toByte() }
        assertEquals(FirmwareContent(sha256(data)), firmwareContentOf(data.inputStream()))
    }

    @Test
    fun readsAZipByItsSignatureWhateverItIsCalled() {
        val first = ByteArray(3000) { it.toByte() }
        val second = "second".toByteArray()
        val content = firmwareContentOf(zip("bios.bin" to first, "other.rom" to second).inputStream())
        assertEquals(sha256(first), content.sha256)
        assertEquals(setOf(crc(first), crc(second)), content.zipCrcs)
    }

    @Test
    fun readsATinyFile() {
        assertEquals(FirmwareContent(sha256(byteArrayOf(1, 2))), firmwareContentOf(byteArrayOf(1, 2).inputStream()))
        assertFalse(isZipSignature(byteArrayOf(0x50, 0x4B)))
        assertTrue(isZipSignature(byteArrayOf(0x50, 0x4B, 0x03, 0x04, 0x14)))
    }

    @Test
    fun aScanGivesEachSlotAVerifiedFileWhateverItsName() {
        val found = listOf(
            FirmwareCandidate("uri:scph7003", "scph7003.bin", FirmwareContent(psxUs)),
            FirmwareCandidate("uri:eu", "whatever.bin", FirmwareContent(psxEu)),
            FirmwareCandidate("uri:scph7001", "scph7001.bin", FirmwareContent("1".repeat(64))),
        )
        assertEquals(mapOf("fw_psx_us" to "uri:scph7003", "fw_psx_eu" to "uri:eu"), firmwareAssignments(found, emptySet()))
    }

    @Test
    fun aScanKeepsAVerifiedFileAndOrdersIdenticalOnesByName() {
        val found = listOf(
            FirmwareCandidate("uri:b", "b.sms", FirmwareContent(smsUsEu)),
            FirmwareCandidate("uri:a", "A.sms", FirmwareContent(smsUsEu)),
        )
        assertEquals(mapOf("fw_ms_eu" to "uri:a"), firmwareAssignments(found, setOf("fw_ms_us")))
    }
}
