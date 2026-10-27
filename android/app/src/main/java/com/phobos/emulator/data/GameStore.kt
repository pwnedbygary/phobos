package com.phobos.emulator.data

import android.content.Context
import android.util.Log
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

/**
 * What Phobos remembers about a game it has run.
 *
 * [uri] is where the game was last started from or found by a scan. [plays] counts starts; a reload isn't one.
 * [playTimeMs] adds up the HUD's play clock ([com.phobos.emulator.PerformanceStats.playTimeMs], unpaused time) over
 * every session, and [lastPlayed] is when the game last started or stopped, in wall-clock milliseconds (0 for never).
 * [favourite] and [hiddenFromHistory] have no controls yet; the file keeps them so the Library's rows need no new
 * format.
 */
data class GameRecord(
    val key: GameKey,
    val uri: String? = null,
    val plays: Int = 0,
    val playTimeMs: Long = 0,
    val lastPlayed: Long = 0,
    val favourite: Boolean = false,
    val hiddenFromHistory: Boolean = false,
)

/**
 * A reading of the core's play clock, the HUD's ([com.phobos.emulator.PerformanceStats]): [ms] of unpaused play counted
 * by the emulation thread [thread] (its id). Each new emulation thread starts the clock again from 0, as when a game
 * loads or a 64DD disk goes in.
 */
data class PlayClock(val thread: Int, val ms: Long)

/**
 * The games Phobos has run, kept in [file] as one JSON object, `{"version": 1, "games": [...]}`.
 *
 * Every call runs on [executor], one at a time and in order, and each change rewrites the whole file atomically:
 * written beside it, synced, then renamed over it. The file is read by the first call. A missing file is an empty
 * store. A file that can't be parsed is renamed to `<name>.corrupt` and the store starts empty. A file from a newer
 * format version, or one that can't be read at all, is left as it is, and nothing is written over it until the app
 * starts again.
 *
 * A record is made when a game first starts. Past [maxRecords] the record played longest ago is dropped, never a
 * favourite. Until then a deleted game's record stays: a scan can't tell a deleted file from a folder that is only out
 * of reach for now (an SD card taken out, a permission lost).
 */
class GameStore(
    private val file: File,
    private val executor: Executor,
    private val now: () -> Long = System::currentTimeMillis,
    private val maxRecords: Int = MAX_RECORDS,
    private val log: (String) -> Unit = {},
) {
    private val records = HashMap<GameKey, GameRecord>()
    private var loaded = false
    private var writable = true
    private var session: Session? = null

    /**
     * The running game, and the play clock reading its time was last counted at: [start], the one taken as it started,
     * or with none, a clock at 0.
     */
    private class Session(val key: GameKey, start: PlayClock?) {
        var thread: Int? = start?.thread
        var countedMs = start?.ms ?: 0L
    }

    /**
     * [key] has started from [uri], with [clock] the play clock's reading then (null when it couldn't be read).
     * [newPlay] is false for a reload, which doesn't count as playing it again.
     */
    fun gameStarted(key: GameKey, uri: String?, newPlay: Boolean, clock: PlayClock? = null) = queue {
        ensureLoaded()
        val known = records[key]
        val record = known ?: GameRecord(key)
        records[key] = record.copy(
            uri = uri ?: record.uri,
            plays = if (newPlay) record.plays + 1 else record.plays,
            lastPlayed = now(),
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
     * A scan of [system]'s folders found [found] (ROM names and URIs). A known game's URI follows its file when the
     * file's URI has changed (a folder added again from another level, a record restored on another device). A scan
     * makes no records.
     */
    fun refreshUris(system: String, found: List<Pair<String, String>>) = queue {
        ensureLoaded()
        var changed = false
        for ((name, uris) in found.groupBy({ it.first }, { it.second })) {
            val key = GameKey(system, name)
            val record = records[key] ?: continue
            if (record.uri !in uris) {
                records[key] = record.copy(uri = uris.first())
                changed = true
            }
        }
        if (changed) save()
    }

    /** Runs [task] on [executor]. A failure is logged: thrown on the executor's thread, it would end the app. */
    private fun queue(task: () -> Unit) = executor.execute {
        try {
            task()
        } catch (e: Exception) {
            log("Game records: ${e.message}")
        }
    }

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
        uri = if (game.isNull("uri")) null else game.getString("uri"),
        plays = game.optInt("plays").coerceAtLeast(0),
        playTimeMs = game.optLong("playTimeMs").coerceAtLeast(0),
        lastPlayed = game.optLong("lastPlayed").coerceAtLeast(0),
        favourite = game.optBoolean("favourite"),
        hiddenFromHistory = game.optBoolean("hiddenFromHistory"),
    )

    private fun save() {
        if (!writable) return
        val games = JSONArray()
        records.values.sortedWith(compareBy({ it.key.system }, { it.key.name })).forEach { record ->
            games.put(
                JSONObject()
                    .put("system", record.key.system)
                    .put("name", record.key.name)
                    .put("uri", record.uri)
                    .put("plays", record.plays)
                    .put("playTimeMs", record.playTimeMs)
                    .put("lastPlayed", record.lastPlayed)
                    .put("favourite", record.favourite)
                    .put("hiddenFromHistory", record.hiddenFromHistory),
            )
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

    companion object {
        const val FILE_NAME = "games.json"
        const val VERSION = 1
        const val MAX_RECORDS = 1000

        @Volatile private var instance: GameStore? = null

        /**
         * The app's one store, in its files folder: a game one activity's view model started and another's stops is
         * still one session.
         */
        fun get(context: Context): GameStore = instance ?: synchronized(this) {
            instance ?: GameStore(
                File(context.applicationContext.filesDir, FILE_NAME),
                Executors.newSingleThreadExecutor { Thread(it, "GameStore").apply { isDaemon = true } },
                log = { Log.w("Phobos", it) },
            ).also { instance = it }
        }
    }
}
