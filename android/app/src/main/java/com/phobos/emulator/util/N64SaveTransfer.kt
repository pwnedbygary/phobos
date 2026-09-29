package com.phobos.emulator.util

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.Inflater

/** An N64 battery save, named by the file Phobos keeps it in (N64 byte order). */
enum class N64SaveKind(val fileName: String, val label: String) {
    EEPROM("save.eeprom", "EEPROM"),
    SRAM("save.ram", "SRAM"),
    FLASH("save.flash", "FlashRAM"),
    CONTROLLER_PAK("save.pak", "Controller Pak");

    companion object {
        fun ofFileName(name: String): N64SaveKind? = entries.firstOrNull { it.fileName == name }
    }
}

/** The save formats Phobos reads and writes. */
enum class N64SaveFormat(val label: String) {
    /** `.eep`, `.sra`, `.fla` and `.mpk`, as Mupen64Plus (and Project64) name them. */
    MUPEN64PLUS("Mupen64Plus"),
    /** One `.srm` holding every save type, as RetroArch's Mupen64Plus core writes it. */
    RETROARCH("RetroArch"),
    /** Phobos's own files: a folder with `save.eeprom`, `save.ram`, `save.flash` and `save.pak`. */
    PHOBOS("Phobos"),
}

/** A save file read for import: its saves in N64 byte order, and the empty parts left out. */
data class N64SaveSource(
    val name: String,
    val format: N64SaveFormat,
    val parts: Map<N64SaveKind, ByteArray>,
    val emptyParts: List<N64SaveKind> = emptyList(),
)

sealed interface N64SaveRead {
    data class Ok(val source: N64SaveSource) : N64SaveRead
    data class Unreadable(val name: String, val reason: String) : N64SaveRead
}

/** What an import writes (with the file each save comes from) and what it leaves out, and why. */
data class N64SaveImportPlan(
    val writes: Map<N64SaveKind, ByteArray>,
    val sources: Map<N64SaveKind, String>,
    val notes: List<String>,
)

object N64SaveTransfer {
    const val EEPROM_4KBIT = 0x200
    const val EEPROM_16KBIT = 0x800
    const val SRAM_SIZE = 0x8000
    const val FLASH_SIZE = 0x20000
    const val PAK_SIZE = 0x8000
    /** Larger than any N64 save file; bigger files aren't read. */
    const val MAX_FILE_SIZE = 1 shl 20

    private const val SRM_SIZE = EEPROM_16KBIT + 4 * PAK_SIZE + SRAM_SIZE + FLASH_SIZE
    private val RZIP_MAGIC = "#RZIPv".toByteArray(Charsets.US_ASCII)

    /**
     * Mupen64Plus keeps SRAM and FlashRAM as host-order 32-bit words, so on every little-endian
     * device each 4-byte word is reversed relative to the N64 (and Phobos). Swapping is its own
     * inverse.
     */
    fun wordSwap(data: ByteArray): ByteArray {
        val out = data.copyOf()
        for (i in 0 until data.size - data.size % 4 step 4) {
            out[i] = data[i + 3]
            out[i + 1] = data[i + 2]
            out[i + 2] = data[i + 1]
            out[i + 3] = data[i]
        }
        return out
    }

    /**
     * RetroArch's save compression: "#RZIPv", a version byte and "#", then the chunk size (u32)
     * and the uncompressed size (u64), then zlib streams each preceded by its size (u32), all
     * little-endian. Returns null when [data] isn't RZIP or is damaged.
     */
    fun rzipDecode(data: ByteArray): ByteArray? {
        if (data.size < 20 || !data.copyOfRange(0, RZIP_MAGIC.size).contentEquals(RZIP_MAGIC) || data[7] != '#'.code.toByte()) {
            return null
        }
        val header = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val chunkSize = header.getInt(8)
        val total = header.getLong(12)
        if (chunkSize <= 0 || chunkSize > MAX_FILE_SIZE || total < 0 || total > MAX_FILE_SIZE) return null
        val out = ByteArrayOutputStream(total.toInt())
        val chunk = ByteArray(chunkSize)
        var position = 20
        while (out.size() < total) {
            if (position + 4 > data.size) return null
            val length = header.getInt(position)
            position += 4
            if (length <= 0 || length > data.size - position) return null
            val inflater = Inflater()
            try {
                inflater.setInput(data, position, length)
                var produced = 0
                while (!inflater.finished() && produced < chunkSize) {
                    val count = inflater.inflate(chunk, produced, chunkSize - produced)
                    if (count == 0 && (inflater.needsInput() || inflater.needsDictionary())) break
                    produced += count
                }
                if (!inflater.finished()) return null
                out.write(chunk, 0, produced)
            } catch (e: java.util.zip.DataFormatException) {
                return null
            } finally {
                inflater.end()
            }
            position += length
        }
        return out.toByteArray().takeIf { it.size.toLong() == total }
    }

    /**
     * Reads one save file by its extension. [cartridgeOrder] marks `.sra` and `.fla` files as
     * already in N64 byte order (cartridge dumps, ares), instead of Mupen64Plus's.
     */
    fun read(name: String, data: ByteArray, cartridgeOrder: Boolean = false): N64SaveRead {
        fun unreadable(reason: String) = N64SaveRead.Unreadable(name, reason)
        fun ok(format: N64SaveFormat, kind: N64SaveKind, bytes: ByteArray) =
            N64SaveRead.Ok(N64SaveSource(name, format, mapOf(kind to bytes)))
        fun eeprom(format: N64SaveFormat) =
            if (data.size == EEPROM_4KBIT || data.size == EEPROM_16KBIT) ok(format, N64SaveKind.EEPROM, data)
            else unreadable("an EEPROM save is 512 bytes or 2 KiB, and this file is ${data.size} bytes")
        fun wordAligned(format: N64SaveFormat, kind: N64SaveKind, swap: Boolean) =
            if (data.isNotEmpty() && data.size % 4 == 0) ok(format, kind, if (swap) wordSwap(data) else data)
            else unreadable("a ${kind.label} save is a whole number of 4-byte words, and this file is ${data.size} bytes")
        fun pak(format: N64SaveFormat, maxPaks: Int) =
            if (data.isNotEmpty() && data.size % PAK_SIZE == 0 && data.size / PAK_SIZE <= maxPaks) {
                ok(format, N64SaveKind.CONTROLLER_PAK, data.copyOf(PAK_SIZE))
            } else unreadable("a Controller Pak save is a multiple of 32 KiB, and this file is ${data.size} bytes")

        return when (name.substringAfterLast('.', "").lowercase()) {
            "eep" -> eeprom(N64SaveFormat.MUPEN64PLUS)
            "sra" -> wordAligned(N64SaveFormat.MUPEN64PLUS, N64SaveKind.SRAM, swap = !cartridgeOrder)
            "fla" -> wordAligned(N64SaveFormat.MUPEN64PLUS, N64SaveKind.FLASH, swap = !cartridgeOrder)
            "mpk" -> pak(N64SaveFormat.MUPEN64PLUS, maxPaks = 4)
            "srm" -> readRetroArch(name, data)
            "eeprom" -> eeprom(N64SaveFormat.PHOBOS)
            "ram" -> wordAligned(N64SaveFormat.PHOBOS, N64SaveKind.SRAM, swap = false)
            "flash" -> wordAligned(N64SaveFormat.PHOBOS, N64SaveKind.FLASH, swap = false)
            "pak" -> pak(N64SaveFormat.PHOBOS, maxPaks = MAX_FILE_SIZE / PAK_SIZE)
            else -> unreadable("it isn't an N64 save file (.eep, .sra, .fla, .mpk, .srm, or Phobos's save files)")
        }
    }

    // RetroArch's N64 save: EEPROM, four Controller Paks, SRAM and FlashRAM, SRAM and FlashRAM in
    // Mupen64Plus's word order. Unused parts are 0xFF (a blank pak for the paks) and are left out.
    private fun readRetroArch(name: String, data: ByteArray): N64SaveRead {
        val body = rzipDecode(data) ?: data
        if (body.size != SRM_SIZE) {
            return N64SaveRead.Unreadable(name, "a RetroArch N64 save is ${SRM_SIZE / 1024} KiB, and this file is ${body.size} bytes")
        }
        val eeprom = body.copyOfRange(0, EEPROM_16KBIT)
        val pak = body.copyOfRange(EEPROM_16KBIT, EEPROM_16KBIT + PAK_SIZE)
        val sramStart = EEPROM_16KBIT + 4 * PAK_SIZE
        val sram = wordSwap(body.copyOfRange(sramStart, sramStart + SRAM_SIZE))
        val flash = wordSwap(body.copyOfRange(sramStart + SRAM_SIZE, SRM_SIZE))
        val parts = linkedMapOf<N64SaveKind, ByteArray>()
        val empty = mutableListOf<N64SaveKind>()
        fun add(kind: N64SaveKind, bytes: ByteArray, blank: Boolean) = if (blank) empty += kind else parts[kind] = bytes
        add(N64SaveKind.EEPROM, eeprom, eeprom.all { it == 0xFF.toByte() })
        add(N64SaveKind.SRAM, sram, sram.all { it == 0xFF.toByte() } || sram.all { it == 0.toByte() })
        add(N64SaveKind.FLASH, flash, flash.all { it == 0xFF.toByte() })
        add(N64SaveKind.CONTROLLER_PAK, pak, isBlankPak(pak))
        return N64SaveRead.Ok(N64SaveSource(name, N64SaveFormat.RETROARCH, parts, empty))
    }

    /** A Controller Pak with an empty note table holds no game saves. */
    fun isBlankPak(pak: ByteArray): Boolean = pak.size >= 0x500 && (0x300 until 0x500).all { pak[it] == 0.toByte() }

    /**
     * A freshly formatted 32 KiB Controller Pak, byte for byte as ares formats one here (and as
     * Mupen64Plus does): the ID block and its three copies with serial number 0, device ID 1 and one
     * bank, the inode table and its copy with pages 5–127 free, no notes.
     */
    fun blankPak(): ByteArray {
        val pak = ByteArray(PAK_SIZE)
        for (area in intArrayOf(1, 3, 4, 6)) {
            val base = area * 0x20
            pak[base + 0x19] = 0x01
            pak[base + 0x1a] = 0x01
            var checksum = 0
            var inverted = 0
            for (half in 0 until 14) {
                val value = ((pak[base + half * 2].toInt() and 0xFF) shl 8) or (pak[base + half * 2 + 1].toInt() and 0xFF)
                checksum += value
                inverted += value.inv() and 0xFFFF
            }
            putHalf(pak, base + 0x1c, checksum)
            putHalf(pak, base + 0x1e, inverted)
        }
        for (page in intArrayOf(1, 2)) {
            for (slot in 5 until 128) pak[page * 0x100 + slot * 2 + 1] = 0x03
            var sum = 0
            for (i in 2 until 0x100) sum += pak[page * 0x100 + i].toInt() and 0xFF
            pak[page * 0x100 + 1] = sum.toByte()
        }
        return pak
    }

    private fun putHalf(data: ByteArray, offset: Int, value: Int) {
        data[offset] = (value shr 8).toByte()
        data[offset + 1] = value.toByte()
    }

    /**
     * Matches the files read for import against the running game's saves ([game]: each save type
     * the game has, with its size). A 2 KiB EEPROM file fills a 512-byte EEPROM from its start, as
     * Mupen64Plus stores 4 Kbit EEPROMs; any other size difference leaves the save out.
     */
    fun plan(sources: List<N64SaveSource>, game: Map<N64SaveKind, Int>): N64SaveImportPlan {
        val writes = linkedMapOf<N64SaveKind, ByteArray>()
        val from = linkedMapOf<N64SaveKind, String>()
        val notes = mutableListOf<String>()
        for (source in sources) {
            for (kind in source.emptyParts) {
                if (kind in game) notes += "${source.name} has no ${kind.label} data, so this game's ${kind.label} stays as it is."
            }
            for ((kind, data) in source.parts) {
                val size = game[kind]
                val fits = when {
                    size == null -> null
                    data.size == size -> data
                    kind == N64SaveKind.EEPROM && size == EEPROM_4KBIT && data.size == EEPROM_16KBIT -> data.copyOf(EEPROM_4KBIT)
                    else -> null
                }
                val leftOut = when {
                    size == null && kind == N64SaveKind.CONTROLLER_PAK ->
                        "${source.name}: no Controller Pak is attached (N64 Settings → Controller Pak), so its pak data is left out."
                    size == null -> "${source.name}: this game doesn't save to ${kind.label}, so that part is left out."
                    kind in writes -> "${source.name}: ${from[kind]} already provides the ${kind.label} save, so this one is left out."
                    fits == null -> "${source.name}: its ${kind.label} save is ${data.size} bytes and this game's is $size, so it is left out."
                    else -> null
                }
                if (leftOut != null) {
                    notes += leftOut
                } else if (fits != null) {
                    writes[kind] = fits
                    from[kind] = source.name
                }
            }
        }
        return N64SaveImportPlan(writes, from, notes)
    }

    /**
     * The files an export writes for [saves] (N64 byte order), named after the game's ROM
     * ([romBase]). Phobos's files go in a folder named after the ROM, the way Phobos keeps them.
     */
    fun export(format: N64SaveFormat, saves: Map<N64SaveKind, ByteArray>, romBase: String): List<Pair<String, ByteArray>> =
        when (format) {
            N64SaveFormat.MUPEN64PLUS -> buildList {
                saves[N64SaveKind.EEPROM]?.let { add("$romBase.eep" to padded(it, EEPROM_16KBIT)) }
                saves[N64SaveKind.SRAM]?.let { add("$romBase.sra" to wordSwap(it)) }
                saves[N64SaveKind.FLASH]?.let { add("$romBase.fla" to wordSwap(it)) }
                saves[N64SaveKind.CONTROLLER_PAK]?.let { add("$romBase.mpk" to it.copyOf(PAK_SIZE) + blankPak() + blankPak() + blankPak()) }
            }
            N64SaveFormat.RETROARCH -> {
                val srm = ByteArrayOutputStream(SRM_SIZE)
                srm.write(padded(saves[N64SaveKind.EEPROM] ?: ByteArray(0), EEPROM_16KBIT))
                srm.write(saves[N64SaveKind.CONTROLLER_PAK]?.copyOf(PAK_SIZE) ?: blankPak())
                repeat(3) { srm.write(blankPak()) }
                srm.write(wordSwap(padded(saves[N64SaveKind.SRAM] ?: ByteArray(0), SRAM_SIZE)))
                srm.write(wordSwap(padded(saves[N64SaveKind.FLASH] ?: ByteArray(0), FLASH_SIZE)))
                listOf("$romBase.srm" to srm.toByteArray())
            }
            N64SaveFormat.PHOBOS -> N64SaveKind.entries.mapNotNull { kind -> saves[kind]?.let { "$romBase/${kind.fileName}" to it } }
        }

    // Fits [data] to [size]: shorter data is padded with 0xFF, the value of unused save memory.
    private fun padded(data: ByteArray, size: Int): ByteArray =
        ByteArray(size) { i -> if (i < data.size) data[i] else 0xFF.toByte() }
}
