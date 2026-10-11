package com.phobos.emulator.util

import java.io.File
import java.io.InputStream
import java.io.OutputStream

/**
 * The PSP's system fonts: the .pgf files in its flash0:/font, which games that print with the PSP's font library
 * draw their text with. They're the PSP's firmware, so Phobos never ships them: the user gives them once, from a dump
 * of their own PSP's flash0 (tools/psp-flash0-dump puts one in the memory stick's FLASH0DUMP folder), and the app
 * keeps copies in its own files, which the PSP core reads at each load (its "Fonts" option). Without them the core
 * runs with none, and games print nothing in them. The copies stay on the device: backups leave the app's firmware
 * folder out (res/xml/backup_rules.xml and data_extraction_rules.xml).
 */
object PspFonts {
    /** The eighteen a PSP has, in the order its font library lists them. */
    val NAMES: List<String> = listOf("jpn0.pgf") + (0..15).map { "ltn$it.pgf" } + "kr0.pgf"

    /** The biggest file taken for a font: the biggest of the PSP's, jpn0.pgf, is 1.5 MB. */
    const val MAX_BYTES = 4L * 1024 * 1024

    /** Where the fonts are looked for by themselves: the dump's folder in the device's Download folder. */
    const val DUMP_FOLDER = "Download/FLASH0DUMP"

    /** Where the app keeps its copies: its firmware's "PlayStation Portable/font", in its own files. */
    fun folder(filesDir: File): File = File(filesDir, "firmware/PlayStation Portable/font")

    /** Whether a file is one of the eighteen, by its name, whatever its case. */
    fun isFont(name: String): Boolean = name.lowercase() in NAMES

    /** Whether a file of [size] bytes can be a font: 1 byte to [MAX_BYTES]. */
    fun fits(size: Long): Boolean = size in 1..MAX_BYTES

    /** Which of the eighteen [folder] holds (each by name, whatever its case, of a size a font can be). */
    fun installedNames(folder: File): Set<String> =
        folder.listFiles()?.filter { it.isFile && isFont(it.name) && fits(it.length()) }
            ?.map { it.name.lowercase() }?.toSet().orEmpty()

    /** How many of the eighteen [folder] holds. */
    fun installed(folder: File): Int = installedNames(folder).size

    /** What the app holds, for Settings → Firmware: how many of the eighteen, and whether they were found there. */
    data class Held(val count: Int, val found: Boolean)

    fun held(folder: File): Held = Held(installed(folder), foundMark(folder).isFile)

    /** Beside the copies, noting that they were found by themselves in [DUMP_FOLDER] rather than picked. */
    fun foundMark(folder: File): File = File(folder.parentFile, "fonts-found-in.txt")

    /**
     * Where a picked folder keeps its fonts: in the folder itself (flash0's font folder picked), else its "font"
     * (flash0 picked), else its "flash0/font" (the dump's own folder picked), whichever first holds a font. [children]
     * lists a folder's entries by name, with whether each is a folder.
     */
    fun <F> fontFolder(root: F, children: (F) -> List<Triple<String, Boolean, F>>): F? {
        fun holdsFonts(folder: F) = children(folder).any { (name, isFolder, _) -> !isFolder && isFont(name) }
        fun child(folder: F, name: String) =
            children(folder).firstOrNull { (childName, isFolder, _) -> isFolder && childName.equals(name, true) }?.third
        val flash0 = child(root, "flash0")
        return listOfNotNull(root, child(root, "font"), flash0?.let { child(it, "font") }).firstOrNull(::holdsFonts)
    }

    /** A file to copy: its name, its size in bytes (-1 when it can't be told), and a way to open it. */
    class Source(val name: String, val size: Long, val open: () -> InputStream?)

    /** What a copy did: how many fonts it copied, and the fonts it couldn't (other files don't count). */
    data class Copied(val copied: Int, val failed: List<String>)

    /**
     * Copies the fonts among [files] (the eighteen by name, any case, 1 byte to [MAX_BYTES]) into [folder], each
     * replacing the one there of that name (lower-cased: the core finds them whatever their case). Each goes to a
     * file beside it first, renamed over it once whole, so one that fails part way leaves the one there was. One that
     * can't be read, or is empty or too big, is left out and named in what's returned.
     */
    @Synchronized
    fun copy(files: List<Source>, folder: File): Copied {
        folder.mkdirs()
        var copied = 0
        val failed = mutableListOf<String>()
        for (file in files) {
            if (!isFont(file.name)) continue
            val target = File(folder, file.name.lowercase())
            val partial = File(folder, "${target.name}.part")
            val whole = try {
                val input = if (file.size < 0 || fits(file.size)) file.open() else null
                input?.use { stream -> partial.outputStream().use { copyAtMost(stream, it, MAX_BYTES) } }
                input != null && fits(partial.length()) && partial.renameTo(target)
            } catch (e: Exception) {
                false
            }
            if (whole) {
                copied++
            } else {
                partial.delete()
                failed += file.name
            }
        }
        return Copied(copied, failed)
    }

    /** Copies the fonts of a folder the user picked: the copies are then that folder's, not found by themselves. */
    fun copyPicked(files: List<Source>, folder: File): Copied {
        val result = copy(files, folder)
        if (result.copied > 0) foundMark(folder).delete()
        return result
    }

    /**
     * What the picker says once it's done: [result] is its copy's (null: the folder couldn't be read), [count] how
     * many of the eighteen the app holds then. Fonts that couldn't be copied are named, apart from none being there.
     */
    fun pickedMessage(result: Copied?, count: Int): String {
        val failed = result?.failed.orEmpty().joinToString()
        return when {
            result == null -> "Couldn't read that folder"
            result.copied == 0 && result.failed.isEmpty() -> "No PSP fonts (jpn0, ltn0-ltn15, kr0.pgf) in that folder"
            result.copied == 0 -> "Couldn't copy the PSP fonts in that folder: $failed"
            else -> "Copied ${result.copied} PSP font${if (result.copied == 1) "" else "s"}: $count of the 18 now" +
                if (result.failed.isEmpty()) "" else ". Couldn't copy: $failed"
        }
    }

    /**
     * The dump's font folder in the device's shared storage under [storage] ([DUMP_FOLDER]'s flash0/font, its font,
     * or the folder itself, as [fontFolder] finds them), or null when there's none there or Android doesn't let the
     * app read it (it has no access to all files): a font there must open, not only be listed.
     */
    fun dumpFonts(storage: File): File? {
        val children = { folder: File -> folder.listFiles().orEmpty().map { Triple(it.name, it.isDirectory, it) } }
        val fonts = fontFolder(File(storage, DUMP_FOLDER), children) ?: return null
        val readable = fonts.listFiles().orEmpty().any { it.isFile && isFont(it.name) && opens(it) }
        return fonts.takeIf { readable }
    }

    private fun opens(file: File): Boolean = try {
        file.inputStream().use { it.read() >= 0 }
    } catch (e: Exception) {
        false
    }

    /** What [autoImport] did: the folder it found the fonts in (null: none it could read), and its copy's result. */
    data class Found(val from: File?, val copied: Int, val failed: List<String>)

    /**
     * The fonts found by themselves: when [folder] holds fewer than the eighteen, those it hasn't got are copied from
     * the dump's folder ([dumpFonts]) as a picked folder's are (the eighteen by name, of a size a font can be), and
     * the copies noted as found ([foundMark]). Null when the app has all eighteen: then nothing outside its own files
     * is read.
     */
    @Synchronized
    fun autoImport(storage: File, folder: File): Found? {
        val have = installedNames(folder)
        if (have.size == NAMES.size) return null
        val from = dumpFonts(storage) ?: return Found(null, 0, emptyList())
        val missing = from.listFiles().orEmpty().filter { it.isFile && isFont(it.name) }
            .filter { it.name.lowercase() !in have }
        val sources = missing.map { file ->
            Source(file.name, file.length().takeIf { it > 0 } ?: -1) { file.inputStream() }
        }
        val result = copy(sources, folder)
        if (result.copied > 0) foundMark(folder).writeText("${from.path}\n")
        return Found(from, result.copied, result.failed)
    }

    /** Copies [input] to [output], stopping one byte past [limit]: a file bigger than that isn't a font. */
    private fun copyAtMost(input: InputStream, output: OutputStream, limit: Long) {
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (total <= limit) {
            val read = input.read(buffer, 0, minOf(buffer.size.toLong(), limit + 1 - total).toInt())
            if (read < 0) break
            output.write(buffer, 0, read)
            total += read
        }
    }
}
