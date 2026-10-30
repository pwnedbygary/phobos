package com.phobos.emulator.ui.carts

import java.io.File
import java.security.MessageDigest

/**
 * Disk cache for RENDERED cart bitmaps (the grid/cards pictures).
 *
 * The key covers every input that changes the pixels: the game, the bitmap kind (grid card / library card),
 * the theme and the resolved face, the look (finish and colour), whether it is a special edition, the wear level
 * and the output size. A key that omitted one of these would serve a correct-looking but wrong picture.
 *
 * Storage is file-based and bounded (a fixed byte budget with LRU pruning by last use), writes are atomic
 * (temp file + rename, so a kill cannot leave a half PNG), the cache is pruned at startup, and an entry that is
 * short or not a PNG is deleted and re-rendered rather than shown.
 *
 * Counters expose hits and misses, and the renderer seam lets tests prove that a re-render performs neither a
 * fetch nor a crop (the caller counts both around its render lambda).
 */
class CartBitmapCache(
    private val dir: File,
    private val maxBytes: Long = 64L * 1024 * 1024,
    private val now: () -> Long = System::currentTimeMillis,
) {

    data class Key(
        val gameId: Long,
        val kind: String,
        val theme: String,
        val face: String,
        val finish: String,
        val color: String,
        val special: Boolean,
        val wear: Int,
        val width: Int,
        val height: Int,
        /** Identity of the label image when one is stamped (absent, mtime), so a late label replaces a blank render. */
        val label: String = "",
    ) {
        /** Stable id over every pixel-affecting field (sha256 of the joined fields). */
        fun id(): String {
            val md = MessageDigest.getInstance("SHA-256")
            val joined = listOf(gameId, kind, theme, face, finish, color, special, wear, width, height, label).joinToString("\u0000")
            return md.digest(joined.toByteArray()).joinToString("") { "%02x".format(it) }
        }
    }

    data class Stats(val entries: Int, val bytes: Long, val hits: Long, val misses: Long)

    @Volatile private var hitCount = 0L
    @Volatile private var missCount = 0L

    init {
        dir.mkdirs()
        prune()
    }

    /** The stored picture for [key] without rendering or counting: the cache-only path while no engine may run. */
    @Synchronized
    fun cached(key: Key): File? {
        val target = File(dir, key.id() + ".png")
        if (target.isFile && validPng(target)) {
            target.setLastModified(now())
            return target
        }
        if (target.isFile) target.delete()
        return null
    }

    /**
     * Returns the cached bitmap for [key], calling [render] exactly once on a miss. [render] must write a complete
     * PNG to the temp file it is given; a file that fails the check is deleted and null returned.
     */
    @Synchronized
    fun get(key: Key, render: (File) -> Unit): File? {
        val target = File(dir, key.id() + ".png")
        if (target.isFile && validPng(target)) {
            target.setLastModified(now())
            hitCount++
            return target
        }
        if (target.isFile) target.delete()
        missCount++
        val temp = File(dir, key.id() + ".tmp")
        temp.delete()
        return try {
            render(temp)
            if (!validPng(temp)) {
                temp.delete()
                null
            } else if (!temp.renameTo(target)) {
                temp.delete()
                null
            } else {
                prune()
                target
            }
        } catch (e: Exception) {
            temp.delete()
            throw e
        }
    }

    /** Drops the oldest entries until the directory fits [maxBytes]. Called at startup and after a write. */
    @Synchronized
    fun prune() {
        val files = dir.listFiles { f -> f.isFile && (f.name.endsWith(".png") || f.name.endsWith(".tmp")) } ?: return
        var total = files.sumOf { it.length() }
        if (total <= maxBytes) return
        for (f in files.sortedBy { it.lastModified() }) {
            if (total <= maxBytes) break
            val len = f.length()
            if (f.delete()) total -= len
        }
    }

    @Synchronized
    fun stats(): Stats {
        val files = dir.listFiles { f -> f.isFile && f.name.endsWith(".png") } ?: emptyArray()
        return Stats(files.size, files.sumOf { it.length() }, hitCount, missCount)
    }

    /** A PNG signature plus a non-trivial length; anything else is treated as corrupt and re-rendered. */
    private fun validPng(f: File): Boolean {
        if (!f.isFile || f.length() < 16) return false
        val head = ByteArray(8)
        return try {
            f.inputStream().use { it.read(head) }
            head.contentEquals(byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A))
        } catch (e: Exception) {
            false
        }
    }
}
