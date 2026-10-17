package com.phobos.emulator.util

import java.io.File
import java.io.IOException
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/** How taking a screenshot went ([Screenshots.save]). */
sealed interface ScreenshotResult {
    data class Saved(val file: File) : ScreenshotResult

    /** The folder couldn't be made, or no file made in it. */
    data class NotWritable(val folder: File) : ScreenshotResult

    /** The core had no picture to give: none drawn yet. */
    data object NoPicture : ScreenshotResult
}

/** What the screenshot effect's pill says (its two lines; [failed] tints it), and what TalkBack reads out. */
data class ScreenshotNote(val title: String, val detail: String, val failed: Boolean, val spoken: String)

/**
 * A screenshot's file, named for the game and when it was taken ("God of War - Ghost of Sparta 2026-10-17 14-15-45.png"),
 * and what the player is told about it.
 */
object Screenshots {
    private val TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH-mm-ss")

    // Characters FAT and exFAT (an SD card), and the storage providers behind SAF, don't take in a name.
    private val UNSAFE = Regex("""[\\/:*?"<>|]""")
    private val CONTROL = Regex("""[\u0000-\u001f\u007f]""")

    // The game's part of a name, at most: with the time, a numbered suffix and ".png", the name stays well under the
    // 255 bytes (UTF-8) internal storage takes and the 255 characters of FAT's and exFAT's names.
    private const val MAX_TITLE_BYTES = 150
    private const val MAX_TRIES = 999

    /** The name before ".png": [romName] without its extension, safe for any storage, then [time]. */
    fun name(romName: String, time: LocalDateTime): String = "${safeTitle(romTitle(romName))} ${time.format(TIME)}"

    private fun safeTitle(title: String): String {
        var safe = title.replace(CONTROL, "").replace(UNSAFE, "_").trim().trimStart('.')
        while (safe.toByteArray(Charsets.UTF_8).size > MAX_TITLE_BYTES) {
            // Drop a whole character: both halves of a surrogate pair.
            val end = if (safe.length >= 2 && safe[safe.length - 1].isLowSurrogate()) safe.length - 2 else safe.length - 1
            safe = safe.substring(0, end)
        }
        return safe.trimEnd(' ', '.').ifEmpty { "Screenshot" }
    }

    /**
     * Takes a screenshot of [romName] at [time] into [folder], made if it isn't there: [capture] writes the picture to
     * the file it's given (false when there's none). Never over a file already there: the second taken in a second is
     * "-2", the third "-3"; each file is made before it's written, so even shots taken at once get their own.
     */
    fun save(folder: File, romName: String, time: LocalDateTime, capture: (File) -> Boolean): ScreenshotResult {
        val name = name(romName, time)
        val file = try {
            folder.mkdirs()
            (1..MAX_TRIES).asSequence()
                .map { File(folder, if (it == 1) "$name.png" else "$name-$it.png") }
                .firstOrNull { it.createNewFile() }
        } catch (_: IOException) {
            null
        } catch (_: SecurityException) {
            null
        } ?: return ScreenshotResult.NotWritable(folder)
        if (capture(file)) return ScreenshotResult.Saved(file)
        file.delete()
        return ScreenshotResult.NoPicture
    }

    /**
     * What the player is told: where a saved shot went, as its folder's last two names ("Screenshots › PlayStation
     * Portable"), or why it wasn't saved. A folder in shared storage is shown from [sharedStorage] (its root,
     * /storage/emulated/0); one in [appStorage], the app's own files that no file manager shows, says where to choose
     * another.
     */
    fun note(result: ScreenshotResult, sharedStorage: String, appStorage: String): ScreenshotNote = when (result) {
        is ScreenshotResult.Saved -> {
            val folder = result.file.parentFile?.path.orEmpty()
            if (folder == appStorage || folder.startsWith("$appStorage/")) {
                ScreenshotNote(
                    "Screenshot saved", "In Phobos's own storage: Settings → Paths → Screenshots Path sets a folder you can open",
                    failed = false,
                    spoken = "Screenshot saved in Phobos's own storage. Settings, Paths, Screenshots Path sets a folder you can open.",
                )
            } else {
                val names = shown(folder, sharedStorage).split('/').filter { it.isNotEmpty() }.takeLast(2)
                ScreenshotNote("Screenshot saved", names.joinToString(" › "), failed = false, spoken = "Screenshot saved to ${names.joinToString(", ")}")
            }
        }
        is ScreenshotResult.NotWritable -> {
            val folder = shown(result.folder.path, sharedStorage)
            ScreenshotNote("Screenshot not saved", "Can't write in $folder", failed = true, spoken = "Screenshot not saved: can't write in $folder")
        }
        ScreenshotResult.NoPicture -> ScreenshotNote(
            "Screenshot not saved", "The game has no picture to save yet", failed = true,
            spoken = "Screenshot not saved: the game has no picture to save yet",
        )
    }

    private fun shown(folder: String, sharedStorage: String) = folder.removePrefix("$sharedStorage/")
}
