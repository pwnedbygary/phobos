// After Cartridge 0.4.9.3 (MIT; docs/third-party/cartridge.md): electron/scraper.js's times (a listing kept two
// weeks, a miss tried again after a week, a failure after an hour). Phobos's own: the file, the cap and eviction.
package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest
import java.util.EnumMap

/** How much the downloaded pictures may take, all kinds together. */
const val ART_CAP_BYTES = 256L * 1024 * 1024

/** How long a game with no picture on the server waits before it's looked for again (Cartridge's week). */
const val ART_MISS_RETRY_MS = 7L * 24 * 3_600_000

/** How long a game whose download failed (no network, a timeout, a server error) waits before it's tried again. */
const val ART_FAILURE_RETRY_MS = 3_600_000L

/** How long a server folder's listing is used before it's fetched again (Cartridge's two weeks). */
const val ART_LISTING_MS = 14L * 24 * 3_600_000

/** What the store holds: [pictures] files taking [bytes]. */
data class ArtStats(val pictures: Int, val bytes: Long)

private class Picture(val file: String, val bytes: Long)

private class Entry(val key: GameKey, var discTitle: String?, var seen: Long) {
    val pictures = EnumMap<ArtKind, Picture>(ArtKind::class.java)
    val retryAt = EnumMap<ArtKind, Long>(ArtKind::class.java)
}

private fun sha1(text: String): String =
    MessageDigest.getInstance("SHA-1").digest(text.toByteArray()).joinToString("") { "%02x".format(it) }

/**
 * The downloaded pictures in [dir], and for every game they were looked for: when it was last seen (in a scan or a
 * Library row), each kind's picture, or when to look again after a miss or a failure. One JSON file, `art.json`,
 * version 1, rewritten whole (to a .tmp file, then renamed) after every 20 changes and at [flush]. A missing file is an
 * empty store; an unreadable one is moved aside to `art.json.corrupt`, the pictures it described are deleted, and the
 * store starts empty; one from a newer Phobos is left alone, and nothing is downloaded. Calls may come from any thread.
 */
class ArtStore(
    private val dir: File,
    private val now: () -> Long = System::currentTimeMillis,
    val capBytes: Long = ART_CAP_BYTES,
    private val log: (String) -> Unit = {},
) {
    private val stateFile = File(dir, FILE_NAME)
    private val entries = LinkedHashMap<GameKey, Entry>()
    private var loaded = false
    private var writable = true
    private var changes = 0
    private val _boxes = MutableStateFlow<Map<GameKey, File>>(emptyMap())
    private val _stats = MutableStateFlow(ArtStats(0, 0))

    /** Each game's box art file, for the Library's cards and a system's list. */
    val boxes: StateFlow<Map<GameKey, File>> = _boxes.asStateFlow()

    /** How many pictures there are and what they take, for the Artwork page. */
    val stats: StateFlow<ArtStats> = _stats.asStateFlow()

    /** Reads the file, if it hasn't been yet. */
    @Synchronized
    fun load() {
        if (loaded) return
        loaded = true
        if (stateFile.exists()) {
            try {
                read(stateFile.readText())
            } catch (e: JSONException) {
                log("Artwork: ${stateFile.name} couldn't be read (${e.message}); starting again")
                entries.clear()
                stateFile.renameTo(File(dir, "$FILE_NAME.corrupt"))
            } catch (e: IOException) {
                log("Artwork: ${stateFile.name} couldn't be read (${e.message}); downloads are off")
                entries.clear()
                writable = false
            }
        }
        if (writable) sweep()
        publish()
    }

    private fun read(text: String) {
        val root = JSONObject(text)
        val version = root.getInt("version")
        if (version > VERSION) {
            log("Artwork: ${stateFile.name} is from a newer Phobos (version $version); downloads are off")
            writable = false
            return
        }
        if (version < 1) throw JSONException("version $version")
        val games = root.getJSONArray("games")
        for (i in 0 until games.length()) {
            val game = games.getJSONObject(i)
            val key = GameKey(game.getString("system"), game.getString("name"))
            val entry = Entry(key, game.optString("discTitle").ifEmpty { null }, game.getLong("seen"))
            for (kind in ArtKind.entries) {
                val state = game.optJSONObject(kind.name.lowercase()) ?: continue
                if (state.has("file")) {
                    val picture = Picture(state.getString("file"), state.getLong("bytes"))
                    if (File(dir, picture.file).isFile) entry.pictures[kind] = picture
                } else if (state.has("retryAt")) {
                    entry.retryAt[kind] = state.getLong("retryAt")
                }
            }
            entries[key] = entry
        }
    }

    /** Deletes picture files no game holds: left by a failed write, or by a file that couldn't be read. */
    private fun sweep() {
        val held = entries.values.flatMap { e -> e.pictures.values.map { it.file } }.toHashSet()
        for (kind in ArtKind.entries) {
            val folder = File(dir, kind.name.lowercase())
            folder.listFiles()?.forEach { if ("${folder.name}/${it.name}" !in held) it.delete() }
        }
        File(dir, TEMP_FOLDER).deleteRecursively()
    }

    /** Whether pictures may be downloaded at all: not with a file from a newer Phobos, or one that couldn't be read. */
    @Synchronized
    fun writable(): Boolean {
        load()
        return writable
    }

    /** [games] were seen now, in a scan or a Library row: their pictures are the last to go when room is needed. */
    @Synchronized
    fun seen(games: Collection<ArtGame>) {
        load()
        val time = now()
        for (game in games) {
            val entry = entries.getOrPut(game.key) { Entry(game.key, game.discTitle, time) }
            entry.seen = time
            if (game.discTitle != null) entry.discTitle = game.discTitle
        }
        changed(games.size)
    }

    /**
     * Whether [key]'s [kind] picture should be downloaded: not when it has one (unless [force]), nor while it waits
     * after a miss or a failure (unless [retry] or [force]).
     */
    @Synchronized
    fun wanted(key: GameKey, kind: ArtKind, force: Boolean = false, retry: Boolean = false): Boolean {
        load()
        val entry = entries[key] ?: return false
        if (!writable) return false
        if (force) return true
        if (kind in entry.pictures) return false
        val retryAt = entry.retryAt[kind] ?: return true
        return retry || now() >= retryAt
    }

    /** A new file for a picture being downloaded, for [stored] to take. */
    @Synchronized
    fun newTempFile(): File {
        val folder = File(dir, TEMP_FOLDER).apply { mkdirs() }
        return File.createTempFile("art", ".tmp", folder)
    }

    /** [file], a picture just downloaded, is [key]'s [kind]: it's moved into the store. */
    @Synchronized
    fun stored(key: GameKey, kind: ArtKind, file: File) {
        load()
        val entry = entries[key]
        if (entry == null || !writable) {
            file.delete()
            return
        }
        val name = "${kind.name.lowercase()}/${sha1("${key.system}\n${key.name}")}.${if (kind.png) "png" else "jpg"}"
        val target = File(dir, name)
        target.parentFile?.mkdirs()
        if (!file.renameTo(target)) {
            file.delete()
            throw IOException("Couldn't move ${file.name} to $name")
        }
        entry.pictures[kind] = Picture(name, target.length())
        entry.retryAt.remove(kind)
        changed()
        publish()
    }

    /** [key]'s [kind] wasn't found, or ([failed]) couldn't be fetched: it waits a week, or an hour. */
    @Synchronized
    fun missed(key: GameKey, kind: ArtKind, failed: Boolean) {
        load()
        val entry = entries[key] ?: return
        entry.retryAt[kind] = now() + if (failed) ART_FAILURE_RETRY_MS else ART_MISS_RETRY_MS
        changed()
    }

    /**
     * Makes room for another picture: while the pictures take [capBytes] or more, those of the game seen longest ago
     * go, all its kinds together, but never those of a game in [keep] (the Library's rows) or one seen at [since] or
     * later (the games being downloaded for now). False when that can't bring them under the cap.
     */
    @Synchronized
    fun makeRoom(keep: Set<GameKey>, since: Long): Boolean {
        load()
        var total = totalBytes()
        if (total < capBytes) return true
        val candidates = entries.values
            .filter { it.pictures.isNotEmpty() && it.key !in keep && it.seen < since }
            .sortedWith(compareBy<Entry> { it.seen }.thenBy { it.key.system }.thenBy { it.key.name })
        for (entry in candidates) {
            if (total < capBytes) break
            total -= entry.pictures.values.sumOf { it.bytes }
            deletePictures(entry)
        }
        changed()
        publish()
        return total < capBytes
    }

    /** Deletes [key]'s pictures and forgets its misses, for a re-download. */
    @Synchronized
    fun forget(key: GameKey) {
        load()
        val entry = entries[key] ?: return
        deletePictures(entry)
        entry.retryAt.clear()
        changed()
        publish()
    }

    /** Deletes every picture and forgets every miss; the games stay known, for a re-download of them all. */
    @Synchronized
    fun forgetAll() {
        load()
        for (kind in ArtKind.entries) File(dir, kind.name.lowercase()).deleteRecursively()
        for (entry in entries.values) {
            entry.pictures.clear()
            entry.retryAt.clear()
        }
        changed(entries.size)
        publish()
    }

    /** Forgets every miss and failure, so the games without pictures are looked for again. */
    @Synchronized
    fun forgetMisses() {
        load()
        for (entry in entries.values) entry.retryAt.clear()
        changed(entries.size)
    }

    /** Every game ever looked for. */
    @Synchronized
    fun known(): List<ArtGame> {
        load()
        return entries.values.map { ArtGame(it.key, it.discTitle) }
    }

    /** Writes the file now if anything changed since it was last written. */
    @Synchronized
    fun flush() {
        if (changes > 0) save()
    }

    private fun deletePictures(entry: Entry) {
        for (picture in entry.pictures.values) File(dir, picture.file).delete()
        entry.pictures.clear()
    }

    private fun totalBytes(): Long = entries.values.sumOf { e -> e.pictures.values.sumOf { it.bytes } }

    private fun changed(count: Int = 1) {
        if (count == 0) return
        changes += count
        if (changes >= SAVE_EVERY) save()
    }

    private fun publish() {
        val boxes = HashMap<GameKey, File>()
        var pictures = 0
        var bytes = 0L
        for (entry in entries.values) {
            entry.pictures[ArtKind.BOX]?.let { boxes[entry.key] = File(dir, it.file) }
            pictures += entry.pictures.size
            bytes += entry.pictures.values.sumOf { it.bytes }
        }
        _boxes.value = boxes
        _stats.value = ArtStats(pictures, bytes)
    }

    private fun save() {
        if (!writable) return
        changes = 0
        val games = JSONArray()
        for (entry in entries.values.sortedWith(compareBy<Entry> { it.key.system }.thenBy { it.key.name })) {
            val game = JSONObject()
                .put("system", entry.key.system)
                .put("name", entry.key.name)
                .put("seen", entry.seen)
            entry.discTitle?.let { game.put("discTitle", it) }
            for (kind in ArtKind.entries) {
                val picture = entry.pictures[kind]
                val retryAt = entry.retryAt[kind]
                when {
                    picture != null -> game.put(
                        kind.name.lowercase(), JSONObject().put("file", picture.file).put("bytes", picture.bytes),
                    )
                    retryAt != null -> game.put(kind.name.lowercase(), JSONObject().put("retryAt", retryAt))
                }
            }
            games.put(game)
        }
        val text = JSONObject().put("version", VERSION).put("games", games).toString()
        try {
            dir.mkdirs()
            val temp = File(dir, "$FILE_NAME.tmp")
            FileOutputStream(temp).use { out ->
                out.write(text.toByteArray())
                out.fd.sync()
            }
            if (!temp.renameTo(stateFile)) throw IOException("Couldn't replace ${stateFile.name}")
        } catch (e: IOException) {
            log("Artwork: ${stateFile.name} couldn't be written (${e.message})")
        }
    }

    companion object {
        const val FILE_NAME = "art.json"
        const val VERSION = 1
        private const val SAVE_EVERY = 20
        private const val TEMP_FOLDER = "incoming"
    }
}

/**
 * The server's folder listings, kept in [dir] for [ART_LISTING_MS]: one JSON file per folder and kind with the names
 * and when they were fetched. A folder the server doesn't have is kept as a listing of nothing, for as long. Android
 * may clear [dir] (it's in the cache folder); listings are then fetched again.
 */
class ArtListingCache(private val dir: File, private val now: () -> Long = System::currentTimeMillis) {
    private fun file(folder: String, kind: ArtKind) = File(dir, "${sha1("$folder\n${kind.folder}")}.json")

    /** [folder]'s [kind] listing if one fetched in the last two weeks is kept, else null. */
    @Synchronized
    fun get(folder: String, kind: ArtKind): ArtListing? = try {
        val root = JSONObject(file(folder, kind).readText())
        val fetched = root.getLong("fetched")
        if (root.getString("folder") != folder || now() - fetched !in 0L until ART_LISTING_MS) {
            null
        } else {
            val names = root.getJSONArray("names")
            ArtListing(List(names.length()) { names.getString(it) })
        }
    } catch (_: IOException) {
        null
    } catch (_: JSONException) {
        null
    }

    /** Keeps [names] as [folder]'s [kind] listing, fetched now. */
    @Synchronized
    fun put(folder: String, kind: ArtKind, names: List<String>) {
        try {
            dir.mkdirs()
            val text = JSONObject()
                .put("folder", folder)
                .put("kind", kind.folder)
                .put("fetched", now())
                .put("names", JSONArray(names))
                .toString()
            val temp = File(dir, "${file(folder, kind).name}.tmp")
            temp.writeText(text)
            temp.renameTo(file(folder, kind))
        } catch (_: IOException) {
            // Not kept: it's fetched again next time.
        }
    }

    /** Forgets [folders]' listings, every kind. */
    @Synchronized
    fun drop(folders: Collection<String>) {
        for (folder in folders) for (kind in ArtKind.entries) file(folder, kind).delete()
    }

    /** Forgets every listing. */
    @Synchronized
    fun dropAll() {
        dir.listFiles()?.forEach { it.delete() }
    }
}
