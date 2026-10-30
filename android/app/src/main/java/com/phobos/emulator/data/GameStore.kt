package com.phobos.emulator.data

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.Executor
import java.util.concurrent.Executors

/**
 * A game as Phobos names it everywhere else (its saves, states, control overrides and last disc): its system and ROM
 * name. Its content URI isn't stable enough to name it: that changes when a folder is added again from another level.
 */
data class GameKey(val system: String, val name: String)

/** One disc of a multi-disc game: its file's [name], its [uri], and the folder [parentUri] it's in. */
data class GameDisc(val name: String, val uri: String, val parentUri: String? = null)

/**
 * Where a game's files are, as a start or a scan found them: its file [uri] (a multi-disc game's first disc), the
 * folder [parentUri] it's in (the Neo Geo and Aleck64 BIOS beside a game and a `.cue` sheet's tracks are looked up
 * there), a multi-disc game's [discs], and a PSP disc's own [title].
 */
data class GameFiles(
    val uri: String,
    val parentUri: String? = null,
    val discs: List<GameDisc> = emptyList(),
    val title: String? = null,
)

/**
 * What Phobos remembers about a game it has run or that was made a favourite.
 *
 * [uri], [parentUri], [discs] and [title] are where the game was last started from or found by a scan (see
 * [GameFiles]). [plays] counts starts; a reload isn't one. [playTimeMs] adds up the HUD's play clock
 * ([com.phobos.emulator.PerformanceStats.playTimeMs], unpaused time) over every session, and [lastPlayed] is when the
 * game last started or stopped, in wall-clock milliseconds (0 for never). [favourite] puts it in the Library's
 * Favorites, and [hiddenFromHistory] keeps it out of Continue Playing until it next starts. [missing] says its file
 * didn't open when a Library row last tried to start it, until a start or a scan of its system finds it.
 */
data class GameRecord(
    val key: GameKey,
    val uri: String? = null,
    val plays: Int = 0,
    val playTimeMs: Long = 0,
    val lastPlayed: Long = 0,
    val favourite: Boolean = false,
    val hiddenFromHistory: Boolean = false,
    val parentUri: String? = null,
    val discs: List<GameDisc> = emptyList(),
    val title: String? = null,
    val missing: Boolean = false,
)

/**
 * A reading of the core's play clock, the HUD's ([com.phobos.emulator.PerformanceStats]): [ms] of unpaused play counted
 * by the emulation thread [thread] (its id). Each new emulation thread starts the clock again from 0, as when a game
 * loads or a 64DD disk goes in.
 */
data class PlayClock(val thread: Int, val ms: Long)

/** How the store weighs a start's URI against the one a record has (see [GameStore.gameStarted]). */
interface GameUris {
    /** Whether [uri] keeps opening after the app that sent it gives its grant up. */
    fun lasting(uri: String): Boolean

    /** Whether [uri] opens now. */
    fun opens(uri: String): Boolean

    companion object {
        /** Every URI lasts, so a start's always replaces the record's. */
        val LASTING = object : GameUris {
            override fun lasting(uri: String) = true
            override fun opens(uri: String) = true
        }
    }
}

/**
 * URIs as Android has them. A file's path lasts, and so does a document reached through a folder Phobos holds a
 * lasting grant for, as the Library scans its ROM folders. A frontend's document comes with a grant that can end with
 * that app.
 */
internal class ContentGameUris(private val resolver: ContentResolver) : GameUris {
    override fun lasting(uri: String): Boolean {
        val parsed = Uri.parse(uri)
        return when (parsed.scheme) {
            "file" -> true
            "content" -> runCatching {
                DocumentsContract.isTreeUri(parsed) && DocumentsContract.getTreeDocumentId(parsed).let { tree ->
                    resolver.persistedUriPermissions.any { held ->
                        held.isReadPermission && held.uri.authority == parsed.authority &&
                            DocumentsContract.isTreeUri(held.uri) &&
                            DocumentsContract.getTreeDocumentId(held.uri) == tree
                    }
                }
            }.getOrDefault(false)
            else -> false
        }
    }

    override fun opens(uri: String): Boolean =
        runCatching { resolver.openFileDescriptor(Uri.parse(uri), "r")?.use { true } ?: false }.getOrDefault(false)
}

/**
 * The games Phobos has run or that were made favourites, kept in [file] as one JSON object,
 * `{"version": 1, "games": [...]}`. The fields after `hiddenFromHistory` are optional, so a build that doesn't know
 * them still reads the file (and drops them when it rewrites it).
 *
 * Every call runs on [executor], one at a time and in order, and each change rewrites the whole file atomically:
 * written beside it, synced, then renamed over it. The file is read by the first call; [games] has every record from
 * then on. A missing file is an empty store. A file that can't be parsed is renamed to `<name>.corrupt` and the store
 * starts empty. A file from a newer format version, or one that can't be read at all, is left as it is, and nothing is
 * written over it until the app starts again.
 *
 * A record is made when a game first starts or is made a favourite. Past [maxRecords] the record played longest ago is
 * dropped, never a favourite. Until then a deleted game's record stays: a scan can't tell a deleted file from a folder
 * that is only out of reach for now (an SD card taken out, a permission lost).
 */
class GameStore(
    private val file: File,
    private val executor: Executor,
    private val now: () -> Long = System::currentTimeMillis,
    private val maxRecords: Int = MAX_RECORDS,
    private val uris: GameUris = GameUris.LASTING,
    private val log: (String) -> Unit = {},
) {
    private val records = HashMap<GameKey, GameRecord>()
    private var loaded = false
    private var writable = true
    private var session: Session? = null
    private val _games = MutableStateFlow<List<GameRecord>?>(null)

    /** Every record, by system and name, as of the store's last change; null until the file has been read. */
    val games: StateFlow<List<GameRecord>?> = _games.asStateFlow()

    /**
     * The running game, and the play clock reading its time was last counted at: [start], the one taken as it started,
     * or with none, a clock at 0.
     */
    private class Session(val key: GameKey, start: PlayClock?) {
        var thread: Int? = start?.thread
        var countedMs = start?.ms ?: 0L
    }

    /** Reads the file, if no call has yet, so that [games] has the records. */
    fun load() = queue { ensureLoaded() }

    /**
     * [key] has started from [files], with [clock] the play clock's reading then (null when it couldn't be read).
     * [newPlay] is false for a reload, which doesn't count as playing it again, nor brings it back to Continue Playing.
     * The record keeps the files it has when they still open and the start's might not last (a frontend's grant).
     */
    fun gameStarted(key: GameKey, files: GameFiles?, newPlay: Boolean, clock: PlayClock? = null) = queue {
        ensureLoaded()
        val known = records[key]
        val record = known ?: GameRecord(key)
        val located = if (files == null || keepsItsFiles(record, files)) record else record.withFiles(files)
        records[key] = located.copy(
            plays = if (newPlay) record.plays + 1 else record.plays,
            lastPlayed = now(),
            hiddenFromHistory = record.hiddenFromHistory && !newPlay,
            missing = false,
        )
        // Its time counts from the clock as it started. Read before the load's emulation thread has started, that's the
        // last thread's reading, which the new thread's first reading shows to be over.
        session = Session(key, clock?.takeIf { it.ms >= 0 })
        if (known == null) makeRoom(keep = key)
        save()
    }

    /**
     * Counts the running game's time since the play clock was last read (pausing, a disc swap). A clock started again
     * (another emulation thread, or a reading lower than the last) counts all of its reading as new time. [clock] is
     * null when the clock couldn't be read, and then nothing is counted.
     */
    fun checkpoint(clock: PlayClock?) = queue {
        val running = session ?: return@queue
        if (count(running, clock)) save()
    }

    /** [key] has stopped, with [clock] the play clock's last reading (null when it couldn't be read). */
    fun gameEnded(key: GameKey, clock: PlayClock?) = queue {
        val running = session?.takeIf { it.key == key } ?: return@queue
        count(running, clock)
        records[key]?.let { records[key] = it.copy(lastPlayed = now()) }
        session = null
        save()
    }

    /**
     * A scan of [system]'s folders found [found] (ROM names and where their files are). A known game's files follow it
     * when its URI has changed (a folder added again from another level, a record restored on another device), and a
     * scan fills in what an older record lacks, such as a multi-disc game's discs. A game found isn't missing. A scan
     * makes no records.
     */
    fun refreshFiles(system: String, found: List<Pair<String, GameFiles>>) = queue {
        ensureLoaded()
        var changed = false
        for ((name, located) in found.groupBy({ it.first }, { it.second })) {
            val key = GameKey(system, name)
            val record = records[key] ?: continue
            val refreshed = record.withFiles(located.firstOrNull { it.uri == record.uri } ?: located.first())
                .copy(missing = false)
            if (refreshed != record) {
                records[key] = refreshed
                changed = true
            }
        }
        if (changed) save()
    }

    /** Makes [key] a favourite, or not. A game with no record gets one, found at [files], with no plays. */
    fun setFavourite(key: GameKey, favourite: Boolean, files: GameFiles?) = queue {
        ensureLoaded()
        val known = records[key]
        if (known?.favourite == favourite || (known == null && !favourite)) return@queue
        val record = known ?: files?.let { GameRecord(key).withFiles(it) } ?: GameRecord(key)
        records[key] = record.copy(favourite = favourite)
        if (known == null) makeRoom(keep = key)
        save()
    }

    /** Keeps [key] out of Continue Playing until it next starts. */
    fun hideFromHistory(key: GameKey) = queue {
        ensureLoaded()
        val record = records[key]?.takeIf { !it.hiddenFromHistory } ?: return@queue
        records[key] = record.copy(hiddenFromHistory = true)
        save()
    }

    /** Marks [key]'s file as not opening ([missing]), or as found again. */
    fun setMissing(key: GameKey, missing: Boolean) = queue {
        ensureLoaded()
        val record = records[key]?.takeIf { it.missing != missing } ?: return@queue
        records[key] = record.copy(missing = missing)
        save()
    }

    /** Runs [task] on [executor]. A failure is logged: thrown on the executor's thread, it would end the app. */
    private fun queue(task: () -> Unit) = executor.execute {
        try {
            task()
        } catch (e: Exception) {
            log("Game records: ${e.message}")
        }
    }

    /**
     * Whether [record] keeps its own files over a start's [files]: theirs might stop opening (a frontend's grant), and
     * its own still open.
     */
    private fun keepsItsFiles(record: GameRecord, files: GameFiles): Boolean {
        val own = record.uri ?: return false
        return own != files.uri && !uris.lasting(files.uri) && uris.opens(own)
    }

    /** [this] record found at [files]; a disc title it had stays when [files] has none. */
    private fun GameRecord.withFiles(files: GameFiles) =
        copy(uri = files.uri, parentUri = files.parentUri, discs = files.discs, title = files.title ?: title)

    private fun count(running: Session, clock: PlayClock?): Boolean {
        if (clock == null || clock.ms < 0) return false
        val restarted = running.thread.let { it != null && it != clock.thread } || clock.ms < running.countedMs
        val added = if (restarted) clock.ms else clock.ms - running.countedMs
        running.thread = clock.thread
        running.countedMs = clock.ms
        val record = records[running.key] ?: return false
        if (added == 0L) return false
        records[running.key] = record.copy(playTimeMs = record.playTimeMs + added)
        return true
    }

    /** Drops the records played longest ago, never a favourite or [keep], until there are at most [maxRecords]. */
    private fun makeRoom(keep: GameKey) {
        while (records.size > maxRecords) {
            val oldest = records.values
                .filter { !it.favourite && it.key != keep }
                .minWithOrNull(compareBy<GameRecord>({ it.lastPlayed }, { it.key.system }, { it.key.name }))
                ?: return
            records.remove(oldest.key)
        }
    }

    private fun ensureLoaded() {
        if (loaded) return
        loaded = true
        read()
        publish()
    }

    private fun read() {
        if (!file.exists()) return
        val text = try {
            file.readText()
        } catch (e: IOException) {
            writable = false
            log("Game records: ${file.name} couldn't be read (${e.message}); it's kept, and nothing is saved")
            return
        }
        try {
            val root = JSONObject(text)
            val version = root.getInt("version")
            if (version > VERSION) {
                writable = false
                log("Game records: ${file.name} is format $version, newer than this app's $VERSION; it's left as it is")
                return
            }
            if (version < 1) throw JSONException("format $version")
            val games = root.getJSONArray("games")
            for (i in 0 until games.length()) parse(games.getJSONObject(i)).let { records[it.key] = it }
        } catch (e: JSONException) {
            records.clear()
            val aside = File(file.path + ".corrupt")
            val moved = file.renameTo(aside)
            if (!moved) writable = false
            log("Game records: ${file.name} couldn't be parsed (${e.message}); " +
                if (moved) "moved to ${aside.name}" else "it's left as it is, and nothing is saved")
        }
    }

    private fun parse(game: JSONObject) = GameRecord(
        key = GameKey(game.getString("system"), game.getString("name")),
        uri = game.optStringOrNull("uri"),
        plays = game.optInt("plays").coerceAtLeast(0),
        playTimeMs = game.optLong("playTimeMs").coerceAtLeast(0),
        lastPlayed = game.optLong("lastPlayed").coerceAtLeast(0),
        favourite = game.optBoolean("favourite"),
        hiddenFromHistory = game.optBoolean("hiddenFromHistory"),
        parentUri = game.optStringOrNull("parentUri"),
        discs = game.optJSONArray("discs")?.let { discs ->
            (0 until discs.length()).map { i ->
                val disc = discs.getJSONObject(i)
                GameDisc(disc.getString("name"), disc.getString("uri"), disc.optStringOrNull("parentUri"))
            }
        }.orEmpty(),
        title = game.optStringOrNull("title"),
        missing = game.optBoolean("missing"),
    )

    private fun JSONObject.optStringOrNull(name: String): String? = if (isNull(name)) null else getString(name)

    private fun save() {
        publish()
        if (!writable) return
        val games = JSONArray()
        records.values.sortedWith(BY_KEY).forEach { record ->
            val game = JSONObject()
                .put("system", record.key.system)
                .put("name", record.key.name)
                .put("uri", record.uri)
                .put("plays", record.plays)
                .put("playTimeMs", record.playTimeMs)
                .put("lastPlayed", record.lastPlayed)
                .put("favourite", record.favourite)
                .put("hiddenFromHistory", record.hiddenFromHistory)
                .put("parentUri", record.parentUri)
                .put("title", record.title)
                .put("missing", record.missing)
            if (record.discs.isNotEmpty()) {
                game.put("discs", JSONArray(record.discs.map { disc ->
                    JSONObject().put("name", disc.name).put("uri", disc.uri).put("parentUri", disc.parentUri)
                }))
            }
            games.put(game)
        }
        val text = JSONObject().put("version", VERSION).put("games", games).toString()
        val partial = File(file.path + ".tmp")
        try {
            FileOutputStream(partial).use { out ->
                out.write(text.toByteArray(Charsets.UTF_8))
                out.fd.sync()
            }
            if (!partial.renameTo(file)) throw IOException("${partial.name} couldn't replace ${file.name}")
        } catch (e: IOException) {
            partial.delete()
            log("Game records: ${file.name} couldn't be saved (${e.message})")
        }
    }

    private fun publish() {
        _games.value = records.values.sortedWith(BY_KEY)
    }

    companion object {
        const val FILE_NAME = "games.json"
        const val VERSION = 1
        const val MAX_RECORDS = 1000

        private val BY_KEY = compareBy<GameRecord>({ it.key.system }, { it.key.name })

        @Volatile private var instance: GameStore? = null

        /**
         * The app's one store, in its files folder: a game one activity's view model started and another's stops is
         * still one session.
         */
        fun get(context: Context): GameStore = instance ?: synchronized(this) {
            instance ?: GameStore(
                File(context.applicationContext.filesDir, FILE_NAME),
                Executors.newSingleThreadExecutor { Thread(it, "GameStore").apply { isDaemon = true } },
                uris = ContentGameUris(context.applicationContext.contentResolver),
                log = { Log.w("Phobos", it) },
            ).also { instance = it }
        }
    }
}
