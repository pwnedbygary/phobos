package com.phobos.emulator.util

/** The title to show for a game: the disc's own, if it has one, else the file's name. */
fun displayTitle(fileName: String, discTitle: String?): String =
    discTitle?.takeIf { it.isNotBlank() } ?: fileName

/** The cache key for a disc's icon: its URI, size and modification time, so a changed file gets a new icon.
 *  The URI's slashes are replaced so the key is a safe file name. */
fun iconCacheKey(uri: String, size: Long, mtime: Long): String =
    "${uri.replace('/', '_')}|$size|$mtime"
