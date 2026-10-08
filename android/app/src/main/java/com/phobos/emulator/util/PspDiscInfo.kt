package com.phobos.emulator.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.security.MessageDigest

/** The title to show for a game: the disc's own, if it has one, else the file's name. */
fun displayTitle(fileName: String, discTitle: String?): String =
    discTitle?.takeIf { it.isNotBlank() } ?: fileName

/** The cache key for a disc's icon: its URI, size and modification time, so a changed file gets a new icon.
 *  The URI's slashes are replaced so the key is a safe file name. */
fun iconCacheKey(uri: String, size: Long, mtime: Long): String =
    "${uri.replace('/', '_')}|$size|$mtime"

/** The SHA-256 of a cache key, as a file name (64 hex digits). */
fun sha256Hex(input: String): String {
    val digest = MessageDigest.getInstance("SHA-256").digest(input.toByteArray())
    return digest.joinToString("") { "%02x".format(it) }
}

/**
 * Title and disc ID from a `.info` cache file (`title\ndiscId`). Keeps an empty disc ID: `readLines()` would drop
 * the trailing empty field after the final newline, so `"Game\n"` must not be treated as a cache miss.
 */
fun parsePspInfoCache(text: String): Pair<String, String>? {
    val parts = text.replace("\r\n", "\n").replace('\r', '\n').split("\n", limit = 2)
    if (parts.size != 2) return null
    return parts[0] to parts[1]
}

/** The icon's max size in the cache (pixels): a menu row is 48 dp, which is 144 px at 3x. */
const val ICON_MAX_PX = 160

/** A PNG whose declared size is past this is refused at cache time (a hostile icon). */
const val ICON_REFUSE_PX = 512

/** Decodes an icon to a menu size: refused past [ICON_REFUSE_PX], downsampled to [ICON_MAX_PX]. */
fun decodeIcon(icon: ByteArray): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(icon, 0, icon.size, bounds)
    if (bounds.outWidth <= 0 || bounds.outWidth > ICON_REFUSE_PX || bounds.outHeight > ICON_REFUSE_PX) return null
    var sample = 1
    while (bounds.outWidth / (sample * 2) >= ICON_MAX_PX) sample *= 2
    val options = BitmapFactory.Options().apply { inSampleSize = sample }
    return BitmapFactory.decodeByteArray(icon, 0, icon.size, options)
}
