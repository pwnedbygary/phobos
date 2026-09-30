package com.phobos.emulator.util

private val discTag = Regex("""[(\[]\s*(?:disc|disk|cd)\s*(\d+)(?:\s*of\s*\d+)?\s*[)\]]""", RegexOption.IGNORE_CASE)

/** The disc number in a file name ("Final Fantasy VII (USA) (Disc 2).chd" → 2), or null. */
fun discNumber(fileName: String): Int? = discTag.find(fileName)?.groupValues?.get(1)?.toIntOrNull()

/** The files an .m3u playlist lists, in order; blank lines and # comments don't count. */
fun m3uEntries(text: String): List<String> =
    text.removePrefix("\uFEFF").lineSequence()
        .map { it.trim() }
        .filter { it.isNotEmpty() && !it.startsWith("#") }
        .map { it.replace('\\', '/') }
        .toList()

/**
 * The path a playlist [entry] names, relative to the playlist's own [playlistPath]:
 * "psx/Game/Game.m3u" with "discs/Game (Disc 1).chd" is "psx/Game/discs/Game (Disc 1).chd".
 */
fun m3uEntryPath(playlistPath: String, entry: String): String {
    if (entry.startsWith('/')) return entry
    val parts = playlistPath.substringBeforeLast('/', "").split('/').filter { it.isNotEmpty() }.toMutableList()
    for (part in entry.split('/')) {
        when (part) {
            "", "." -> Unit
            ".." -> parts.removeLastOrNull()
            else -> parts += part
        }
    }
    return parts.joinToString("/", prefix = if (playlistPath.startsWith('/')) "/" else "")
}

private val cueFileLine = Regex("""^(\s*FILE\s+)(?:"([^"]+)"|(\S+))(.*)$""", setOf(RegexOption.IGNORE_CASE, RegexOption.MULTILINE))

/** The track files a .cue sheet names, in order (FILE "Game (Track 1).bin" BINARY). */
fun cueTracks(text: String): List<String> =
    cueFileLine.findAll(text).map { it.groupValues[2].ifEmpty { it.groupValues[3] }.replace('\\', '/') }.toList()

/** The sheet with each track's path replaced by its file name inside [folder]. */
fun cueWithTracksIn(text: String, folder: String): String =
    cueFileLine.replace(text) { line ->
        val track = line.groupValues[2].ifEmpty { line.groupValues[3] }.replace('\\', '/').substringAfterLast('/')
        "${line.groupValues[1]}\"$folder/$track\"${line.groupValues[4]}"
    }

/** A multi-disc game: its title and its discs in order. */
data class DiscSet<T>(val title: String, val discs: List<T>)

/**
 * Splits [files] into games whose discs are files of one folder and format that differ only in the
 * disc number ("… (Disc 1).chd", "… (Disc 2).chd"), and the files that stay on their own.
 */
fun <T> groupDiscSets(files: List<T>, fileName: (T) -> String, folder: (T) -> Any?): Pair<List<DiscSet<T>>, List<T>> {
    val sets = files
        .filter { discNumber(fileName(it)) != null }
        .groupBy { Triple(folder(it), withoutDiscNumber(romTitle(fileName(it))).lowercase(), fileName(it).substringAfterLast('.', "").lowercase()) }
        .values
        .filter { it.size > 1 }
        .map { discs ->
            val sorted = discs.sortedBy { discNumber(fileName(it)) }
            DiscSet(withoutDiscNumber(romTitle(fileName(sorted.first()))), sorted)
        }
    val inSets = sets.flatMap { it.discs }.toSet()
    return sets to files.filter { it !in inSets }
}
