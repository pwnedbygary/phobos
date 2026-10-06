package com.phobos.emulator.util

import java.io.File
import java.io.IOException
import java.io.InputStream

/**
 * The PSP's system fonts: the .pgf files in its flash0:/font, which games that print with the PSP's font library
 * draw their text with. They're the PSP's firmware, so Phobos never ships them: the user gives them once, from a dump
 * of their own PSP's flash0 (tools/psp-flash0-dump puts one in the memory stick's FLASH0DUMP folder), and the app
 * keeps copies in its own files, which the PSP core reads at each load (its "Fonts" option). Without them the core
 * runs with none, and games print nothing in them.
 */
object PspFonts {
    /** The eighteen a PSP has, in the order its font library lists them. */
    val NAMES: List<String> = listOf("jpn0.pgf") + (0..15).map { "ltn$it.pgf" } + "kr0.pgf"

    /** Where the app keeps its copies: its firmware's "PlayStation Portable/font", in its own files. */
    fun folder(filesDir: File): File = File(filesDir, "firmware/PlayStation Portable/font")

    /** Whether a file of a picked folder is a font to copy: a plain name ending in ".pgf". */
    fun isFont(name: String): Boolean =
        name.length > 4 && name.endsWith(".pgf", ignoreCase = true) && name.none { it == '/' || it == '\\' } &&
            !name.startsWith(".")

    /** How many of the eighteen [folder] holds (each by name, whatever its case, and not empty). */
    fun installed(folder: File): Int {
        val present = folder.listFiles()?.filter { it.isFile && it.length() > 0 }?.map { it.name.lowercase() }.orEmpty()
        return NAMES.count { it in present }
    }

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

    /**
     * Copies the fonts among [files] (each a name and a way to open it) into [folder], each replacing the one there of
     * that name (lower-cased: the core finds them whatever their case). Each goes to a file beside it first, renamed
     * over it once whole, so one that fails part way leaves the one there was. Returns how many were copied.
     */
    fun copy(files: List<Pair<String, () -> InputStream?>>, folder: File): Int {
        folder.mkdirs()
        var copied = 0
        for ((name, open) in files) {
            if (!isFont(name)) continue
            val target = File(folder, name.lowercase())
            val partial = File(folder, "${target.name}.part")
            try {
                val input = open() ?: continue
                input.use { stream -> partial.outputStream().use { stream.copyTo(it) } }
                if (partial.length() == 0L || !partial.renameTo(target)) {
                    partial.delete()
                    continue
                }
                copied++
            } catch (e: IOException) {
                partial.delete()
            }
        }
        return copied
    }
}
