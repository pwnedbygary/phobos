package com.phobos.emulator.util

import java.io.InputStream
import java.security.MessageDigest
import java.util.zip.CRC32
import java.util.zip.ZipInputStream

/**
 * What a firmware file is, judged by its content alone (its name plays no part): the SHA-256 of the bytes the
 * loader uses, the file itself or the first file in a zip, and for a zip the CRC32s of all its files. A file 512
 * bytes past a multiple of 8 KiB also has the SHA-256 of what follows those 512 bytes, a copier's header, which
 * some PC Engine card dumps carry.
 */
data class FirmwareContent(val sha256: String?, val zipCrcs: Set<String> = emptySet(), val unheaderedSha256: String? = null)

/** What a slot's file is to it: a known-good dump, some other file, or a file the app can't open. */
enum class FirmwareStatus { Verified, Unrecognized, Unreadable }

/** A firmware file a scan found: its URI, its name (only to order files with the same content) and its content. */
data class FirmwareCandidate(val uri: String, val name: String, val content: FirmwareContent)

object FirmwareIds {
    /**
     * Each slot's known-good dumps, as the SHA-256 of the file the core reads. Most come from the firmware
     * list of upstream ares's desktop frontend (`desktop-ui/emulator/`), which names the exact dump each core
     * expects; the 32X, ZX Spectrum, Game Boy boot and N64 PIF ROMs are the copies Phobos bundles from
     * `mia/Firmware`; the Super Game Boy carts are mia's Super Famicom database entries; and the Neo Geo CD
     * BIOSes are MAME's neocd and neocdz sets (front loader, top loader, CDZ).
     */
    val sha256: Map<String, Set<String>> = mapOf(
        "fw_coleco" to setOf("990bf1956f10207d8781b619eb74f89b00d921c8d45c95c334c16c8cceca09ad"),
        "fw_fds" to setOf("fdc1a76e654feea993fcb38366e05ee5f4eb641f86fe6bebaeefd412e112dd72"),
        "fw_gba" to setOf("fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570"),
        "fw_gg" to setOf("8c8a21335038285cfa03dc076100c1f0bfadf3e4ff70796f11f3dfaaab60eee2"),
        "fw_ms_us" to setOf("477617917a12a30f9f43844909dc2de6e6a617430f5c9a36306c86414a670d50"),
        "fw_ms_jp" to setOf("67846e26764bd862f19179294347f7353a4166b62ac4198a5ec32933b7da486e"),
        "fw_ms_eu" to setOf("477617917a12a30f9f43844909dc2de6e6a617430f5c9a36306c86414a670d50"),
        "fw_mcd_us" to setOf("fb477cdbf94c84424c2feca4fe40656d85393fe7b7b401911b45ad2eb991258c"),
        "fw_mcd_jp" to setOf("7133fc2dd2fe5b7d0acd53a5f10f3d00b5d31270239ad20d74ef32393e24af88"),
        "fw_mcd_eu" to setOf("fe608a2a07676a23ab5fd5eee2f53c9e2526d69a28aa16ccd85c0ec42e6933cb"),
        "fw_laseractive_sega_us" to setOf("e89b5a319f66406611ec82fe5c4aa6827c175a05135bd7bd177366cba0465021"),
        "fw_laseractive_sega_jp" to setOf("dca942d977217f703d8d1c6eb1aeb6b32c78ecc421486bbb46c459d385161c94"),
        "fw_laseractive_nec_us" to setOf("0e87a3385a27b3a4cac51934819b7eefa5b3d690768d2495633838488cd0e2e4"),
        "fw_laseractive_nec_jp" to setOf("459325690a458baebd77495c91e37c4dddfdd542ba13a821ce954e5bb245627f"),
        "fw_laseractive_nec_lp" to setOf("3f43b3b577117d84002e99cb0baeb97b0d65b1d70b4adadc68817185c6a687f0"),
        "fw_laseractive_nec_ge" to setOf("4b86bb96a48a4ca8375fc0109631d0b1d64f255a03b01de70594d40788ba6c3d"),
        "fw_msx" to setOf("413a2b601a94b3792e054be2439cc77a1819cceadbfa9542f88d51c7480f2ef0"),
        "fw_msx2_main" to setOf("0c672d86ead61a97f49a583b88b7c1905da120645cd44f0c9f2baf4f4631e0b1"),
        "fw_msx2_sub" to setOf("6c6f421a10c428d960b7ecc990f99af1c638147f747bddca7b0bf0e2ab738300"),
        "fw_ngp" to setOf("0293555b21c4fac516d25199df7809b26beeae150e1d4504a050db32264a6ad7"),
        "fw_ngpc" to setOf("8fb845a2f71514cec20728e2f0fecfade69444f8d50898b92c2259f1ba63e10d"),
        "fw_n64dd_jp" to setOf("806400ec0df94b0755de6c5b8249d6b6a9866124c5ddbdac198bde22499bfb8b"),
        "fw_n64dd_us" to setOf("e9fec87a45fba02399e88064b9e2f8cf0f2106e351c58279a87f05da5bc984ad"),
        "fw_n64dd_dev" to setOf("9c2962a8b994a29e4cd04b3a6e4ed730a751414655ab6a9799ebf5fc08b79d44"),
        "fw_pce_cd_1_jp" to setOf("afe9f27f91ac918348555b86298b4f984643eafa2773196f2c5441ea84f0c3bb"),
        "fw_pce_cd_3_jp" to setOf("e11527b3b96ce112a037138988ca72fd117a6b0779c2480d9e03eaebece3d9ce"),
        "fw_pce_cd_3_us" to setOf("cadac2725711b3c442bcf237b02f5a5210c96f17625c35fa58f009e0ed39e4db"),
        "fw_pce_cd_ge_jp" to setOf("4b86bb96a48a4ca8375fc0109631d0b1d64f255a03b01de70594d40788ba6c3d"),
        "fw_supergrafx_ac_jp" to setOf("e11527b3b96ce112a037138988ca72fd117a6b0779c2480d9e03eaebece3d9ce"),
        "fw_psx_us" to setOf("11052b6499e466bbf0a709b1f9cb6834a9418e66680387912451e971cf8a1fef"),
        "fw_psx_jp" to setOf("9c0421858e217805f4abe18698afea8d5aa36ff0727eb8484944e00eb5e7eadb"),
        "fw_psx_eu" to setOf("1faaa18fa820a0225e488d9f086296b8e6c46df739666093987ff7d8fd352c09"),
        "fw_32x_g" to setOf("a2d6b6e1544467e27d404634de6e6b0a54ed4b3babb6b71739a260e1be04ebfc"),
        "fw_32x_m" to setOf("4c6128229bd24dae5f11d3f569ca592665f141bef26cef937e2ce4d9b548cc3b"),
        "fw_32x_s" to setOf("7bde4245ce00c51a23a1ce73a881752e7b14dc12416e175352a3807338e04745"),
        "fw_zx48" to setOf("d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42"),
        "fw_zx128" to setOf("3ba308f23b9471d13d9ba30c23030059a9ce5d4b317b85b86274b132651d1425"),
        "fw_zx128_sub" to setOf("8d93c3342321e9d1e51d60afcd7d15f6a7afd978c231b43435a7c0757c60b9a3"),
        "fw_gb_boot" to setOf(
            "26e71cf01e301e5dc40e987cd2ecbf6d0276245890ac829db2a25323da86818e",
            "cf053eccb4ccafff9e67339d4e78e98dce7d1ed59be819d2a1ba2232c6fce1c7",
            "a8cb5f4f1f16f2573ed2ecd8daedb9c5d1dd2c30a481f9b179b5d725d95eafe2",
        ),
        "fw_gbc_boot" to setOf(
            "6dff36cedce210c0856770de51ede01450f617b376bb8c5709ede5b203be98b4",
            "4bf5021be357ce523a59ac5f4efff5d6371ae50112a6db0adf4a75916ad760a9",
        ),
        "fw_n64_pif_ntsc" to setOf("fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd"),
        "fw_n64_pif_pal" to setOf("79c81fffae934b6df251da6c3a394f52c6a0eaa07115a3b9677743e3c1335f4a"),
        "fw_sgb1" to setOf("4d7fc331a811b8dc630b469262fd6f45e289243cef83101f32038158967d1b28"),
        "fw_sgb2" to setOf("e1db895a9da7ce992941c1238f711437a9110c2793083bb04e0b4d49b7915916"),
        "fw_ng_cd" to setOf(
            "2bd8ce118bcac5efde1bd7df71f96040d6750e0b1f495cec1af8c86d13c88e75",
            "c11c33589b2057008a7e4c7c700fcd989a8c0f95f869e7e7539fdd00414d99a7",
            "2e93af5848080ea04d17a7841b742f009330d30e4ff40c3410547581d921c892",
        ),
    )

    /**
     * MAME sets the app hands over whole, known by the CRC32s of the files Phobos reads from them: the Neo Geo
     * BIOS set's zoom table, fix-layer font and MVS BIOS (000-lo.lo, sfix.sfix, sp-s2.sp1), and the Aleck64 PIF.
     */
    private val zipSets: List<Pair<Set<String>, Set<String>>> = listOf(
        setOf("5a86cff2", "c2ea0cfd", "9036d879") to setOf("fw_ng_bios", "fw_ng_aes", "fw_ng_mvs"),
        setOf("5ec82be9") to setOf("fw_aleck64"),
    )

    /** Slots whose system falls back to a copy Phobos ships (pak() in PhobosRunner.cpp), so they work empty. */
    val builtIn = setOf(
        "fw_32x_g", "fw_32x_m", "fw_32x_s", "fw_zx48", "fw_zx128", "fw_zx128_sub",
        "fw_n64_pif_ntsc", "fw_n64_pif_pal", "fw_gb_boot", "fw_gbc_boot",
        "fw_msx", "fw_msx2_main", "fw_msx2_sub",
    )

    /** PC Engine card slots: their loader drops a copier's 512-byte header (pak() in PhobosRunner.cpp). */
    private val headerStripped = setOf("fw_pce_cd_1_jp", "fw_pce_cd_3_jp", "fw_pce_cd_3_us", "fw_pce_cd_ge_jp", "fw_supergrafx_ac_jp")

    /** The slots [content] is a known-good dump for. */
    fun slotsFor(content: FirmwareContent): Set<String> {
        zipSets.firstOrNull { (crcs, _) -> content.zipCrcs.containsAll(crcs) }?.let { return it.second }
        val hash = content.sha256
        val unheadered = content.unheaderedSha256
        return sha256.filter { (key, hashes) ->
            (hash != null && hash in hashes) || (unheadered != null && key in headerStripped && unheadered in hashes)
        }.keys
    }

    fun isVerified(key: String, content: FirmwareContent?): Boolean = content != null && key in slotsFor(content)
}

private val ZIP_SIGNATURE = byteArrayOf(0x50, 0x4B, 0x03, 0x04)

/** Whether [head], a file's first bytes, starts a zip archive. */
fun isZipSignature(head: ByteArray): Boolean =
    head.size >= ZIP_SIGNATURE.size && ZIP_SIGNATURE.indices.all { head[it] == ZIP_SIGNATURE[it] }

/** Reads [input] to its end into a [FirmwareContent]. A zip is recognized by its signature, not by a name. */
fun firmwareContentOf(input: InputStream): FirmwareContent {
    val buffered = input.buffered()
    buffered.mark(ZIP_SIGNATURE.size)
    val head = ByteArray(ZIP_SIGNATURE.size)
    var read = 0
    while (read < head.size) {
        val n = buffered.read(head, read, head.size - read)
        if (n < 0) break
        read += n
    }
    buffered.reset()
    if (!isZipSignature(head.copyOf(read))) {
        val hashes = hashesOf(buffered)
        return FirmwareContent(hashes.sha256, unheaderedSha256 = hashes.unheaderedSha256)
    }
    var first: Hashes? = null
    val crcs = mutableSetOf<String>()
    ZipInputStream(buffered).use { zip ->
        while (true) {
            val entry = zip.nextEntry ?: break
            if (entry.isDirectory) continue
            val crc = CRC32()
            val hashes = hashesOf(zip, crc)
            crcs += "%08x".format(crc.value)
            if (first == null) first = hashes
        }
    }
    return FirmwareContent(first?.sha256, crcs, first?.unheaderedSha256)
}

private const val COPIER_HEADER = 512

private class Hashes(val sha256: String, val unheaderedSha256: String?)

/**
 * The SHA-256 of what's left of [input], whose bytes also go to [crc], and when it runs 512 bytes past a multiple
 * of 8 KiB, the SHA-256 of what follows those 512 bytes.
 */
private fun hashesOf(input: InputStream, crc: CRC32? = null): Hashes {
    val digest = MessageDigest.getInstance("SHA-256")
    val pastHeader = MessageDigest.getInstance("SHA-256")
    val buffer = ByteArray(64 * 1024)
    var total = 0L
    while (true) {
        val n = input.read(buffer)
        if (n < 0) break
        digest.update(buffer, 0, n)
        crc?.update(buffer, 0, n)
        val skip = (COPIER_HEADER - total).coerceIn(0L, n.toLong()).toInt()
        if (skip < n) pastHeader.update(buffer, skip, n - skip)
        total += n
    }
    val unheadered = if (total % 8192 == COPIER_HEADER.toLong()) hex(pastHeader.digest()) else null
    return Hashes(hex(digest.digest()), unheadered)
}

private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it) }

/**
 * The slots a scan sets and the URI each gets: every slot whose current file isn't a verified dump takes a
 * verified one from [found] (the first by name when several files have the same content). A slot with no
 * verified file in [found] keeps what it has, so a file picked by hand stays until a verified one turns up.
 */
fun firmwareAssignments(found: List<FirmwareCandidate>, verifiedSlots: Set<String>): Map<String, String> {
    val assignments = mutableMapOf<String, String>()
    for (candidate in found.sortedBy { it.name.lowercase() }) {
        for (key in FirmwareIds.slotsFor(candidate.content)) {
            if (key !in verifiedSlots && key !in assignments) assignments[key] = candidate.uri
        }
    }
    return assignments
}
