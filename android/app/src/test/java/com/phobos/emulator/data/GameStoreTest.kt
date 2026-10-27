package com.phobos.emulator.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.util.concurrent.Executor

class GameStoreTest {
    @get:Rule
    val folder = TemporaryFolder()

    private val file get() = folder.root.resolve("games.json")
    private var clock = 1_000L
    private val logged = mutableListOf<String>()

    private val tetris = GameKey("Game Boy", "Tetris (World).gb")
    private val mario = GameKey("Nintendo 64", "Super Mario 64 (USA).z64")

    /** A play-clock reading of [ms] from emulation thread [thread]. */
    private fun played(ms: Long, thread: Int = 1) = PlayClock(thread, ms)

    /** A store that runs each call on the test's thread, in order, as the app's runs them on its own thread. */
    private fun store(maxRecords: Int = GameStore.MAX_RECORDS) =
        GameStore(file, Executor { it.run() }, now = { clock }, maxRecords = maxRecords, log = { logged += it })

    /** The games the file holds, read with org.json rather than the store. */
    private fun saved(): Map<GameKey, JSONObject> {
        val root = JSONObject(file.readText())
        assertEquals(1, root.getInt("version"))
        val games = root.getJSONArray("games")
        return (0 until games.length()).map { games.getJSONObject(it) }
            .associateBy { GameKey(it.getString("system"), it.getString("name")) }
    }

    @Test
    fun aMissingFileIsAnEmptyStoreAndOnlyAStartWritesIt() {
        val store = store()
        store.refreshUris("Game Boy", listOf("Tetris (World).gb" to "content://tree/a/document/1"))
        store.checkpoint(played(5_000))
        store.gameEnded(tetris, played(5_000))
        assertFalse(file.exists())
    }

    @Test
    fun aStartedGameIsRecordedWithItsUriOnePlayAndTheTime() {
        clock = 1_793_000_000_000
        store().gameStarted(tetris, "content://tree/a/document/1", newPlay = true)
        val game = saved().getValue(tetris)
        assertEquals("content://tree/a/document/1", game.getString("uri"))
        assertEquals(1, game.getInt("plays"))
        assertEquals(0L, game.getLong("playTimeMs"))
        assertEquals(1_793_000_000_000L, game.getLong("lastPlayed"))
        assertFalse(game.getBoolean("favourite"))
        assertFalse(game.getBoolean("hiddenFromHistory"))
    }

    @Test
    fun theSameRomNameUnderTwoSystemsIsTwoGames() {
        val store = store()
        store.gameStarted(GameKey("Game Boy", "Tetris.zip"), "content://a", newPlay = true)
        store.gameEnded(GameKey("Game Boy", "Tetris.zip"), played(1_000))
        store.gameStarted(GameKey("Game Boy Color", "Tetris.zip"), "content://b", newPlay = true)
        val games = saved()
        assertEquals(2, games.size)
        assertEquals(1_000L, games.getValue(GameKey("Game Boy", "Tetris.zip")).getLong("playTimeMs"))
        assertEquals(0L, games.getValue(GameKey("Game Boy Color", "Tetris.zip")).getLong("playTimeMs"))
    }

    @Test
    fun aReloadIsNotAnotherPlayButItsTimeCounts() {
        val store = store()
        store.gameStarted(tetris, "content://a", newPlay = true)
        // The Reload hotkey closes the session, then loads the same game again.
        store.gameEnded(tetris, played(60_000))
        store.gameStarted(tetris, "content://a", newPlay = false)
        store.gameEnded(tetris, played(30_000, thread = 2))
        val game = saved().getValue(tetris)
        assertEquals(1, game.getInt("plays"))
        assertEquals(90_000L, game.getLong("playTimeMs"))
    }

    @Test
    fun checkpointsCountEachStretchOfPlayOnce() {
        val store = store()
        store.gameStarted(tetris, "content://a", newPlay = true)
        store.checkpoint(played(1_000))
        store.checkpoint(played(1_000))
        store.checkpoint(played(2_500))
        store.gameEnded(tetris, played(4_000))
        assertEquals(4_000L, saved().getValue(tetris).getLong("playTimeMs"))
    }

    @Test
    fun aNewEmulationThreadsClockCountsFromZero() {
        val store = store()
        store.gameStarted(mario, "content://a", newPlay = true)
        store.checkpoint(played(5_000))
        // A 64DD disk starts another emulation thread, whose clock counts from 0 again and soon passes the old reading.
        store.checkpoint(played(9_000, thread = 2))
        store.gameEnded(mario, played(9_500, thread = 2))
        assertEquals(14_500L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun aReadingBelowTheLastIsAClockStartedAgain() {
        val store = store()
        store.gameStarted(mario, "content://a", newPlay = true)
        store.checkpoint(played(5_000))
        store.checkpoint(played(5_200))
        store.checkpoint(played(10))
        store.gameEnded(mario, played(1_000))
        assertEquals(6_200L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun aClockReadBeforeTheNewEmulationThreadStartsIsNotCountedAgain() {
        val store = store()
        store.gameStarted(tetris, "content://a", newPlay = true, clock = played(0, thread = 0))
        store.gameEnded(tetris, played(60_000))
        // The next load succeeds before its emulation thread has started: the reading taken then, and a pause's just
        // after, still show the last thread's id and clock.
        store.gameStarted(mario, "content://m", newPlay = true, clock = played(60_000))
        store.checkpoint(played(60_000))
        store.gameEnded(mario, played(2_000, thread = 2))
        assertEquals(60_000L, saved().getValue(tetris).getLong("playTimeMs"))
        assertEquals(2_000L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun playTimeAddsUpOverSessionsAndAnUnreadClockAddsNothing() {
        val store = store()
        store.gameStarted(tetris, "content://a", newPlay = true)
        store.gameEnded(tetris, played(120_000))
        store.gameStarted(tetris, "content://a", newPlay = true)
        store.checkpoint(null)
        store.gameEnded(tetris, null)
        store.gameStarted(tetris, "content://a", newPlay = true)
        store.gameEnded(tetris, played(45_000, thread = 3))
        val game = saved().getValue(tetris)
        assertEquals(3, game.getInt("plays"))
        assertEquals(165_000L, game.getLong("playTimeMs"))
    }

    @Test
    fun lastPlayedMovesToTheStopAndAnotherGamesStopIsIgnored() {
        val store = store()
        clock = 10_000
        store.gameStarted(tetris, "content://a", newPlay = true)
        clock = 70_000
        store.gameEnded(mario, played(50_000))
        assertEquals(10_000L, saved().getValue(tetris).getLong("lastPlayed"))
        assertEquals(0L, saved().getValue(tetris).getLong("playTimeMs"))
        store.gameEnded(tetris, played(60_000))
        assertEquals(70_000L, saved().getValue(tetris).getLong("lastPlayed"))
        assertEquals(60_000L, saved().getValue(tetris).getLong("playTimeMs"))
    }

    @Test
    fun aScanMovesAKnownGamesUriAndMakesNoRecords() {
        val store = store()
        store.gameStarted(tetris, "content://tree/old/document/1", newPlay = true)
        store.gameEnded(tetris, played(1_000))
        store.refreshUris(
            "Game Boy",
            listOf(
                "Tetris (World).gb" to "content://tree/new/document/1",
                "Dr. Mario (World).gb" to "content://tree/new/document/2",
            ),
        )
        store.refreshUris("Game Boy Color", listOf("Tetris (World).gb" to "content://tree/gbc/document/1"))
        val games = saved()
        assertEquals(setOf(tetris), games.keys)
        assertEquals("content://tree/new/document/1", games.getValue(tetris).getString("uri"))
        assertEquals(1_000L, games.getValue(tetris).getLong("playTimeMs"))
    }

    @Test
    fun aScanKeepsTheUriItHasWhenTheSameNameIsInTwoFolders() {
        val store = store()
        store.gameStarted(tetris, "content://tree/b/document/1", newPlay = true)
        store.refreshUris(
            "Game Boy",
            listOf(
                "Tetris (World).gb" to "content://tree/a/document/1",
                "Tetris (World).gb" to "content://tree/b/document/1",
            ),
        )
        assertEquals("content://tree/b/document/1", saved().getValue(tetris).getString("uri"))
    }

    @Test
    fun recordsSurviveTheAppStartingAgain() {
        store().apply {
            gameStarted(tetris, "content://a", newPlay = true)
            gameEnded(tetris, played(30_000))
        }
        store().apply {
            gameStarted(tetris, "content://a", newPlay = true)
            gameEnded(tetris, played(15_000))
        }
        val game = saved().getValue(tetris)
        assertEquals(2, game.getInt("plays"))
        assertEquals(45_000L, game.getLong("playTimeMs"))
    }

    @Test
    fun aVersionOneFileIsReadAndEveryFieldSurvivesARewrite() {
        file.writeText(
            """
            {"version": 1, "games": [
              {"system": "Game Boy", "name": "Tetris (World).gb", "uri": "content://a", "plays": 4,
               "playTimeMs": 3600000, "lastPlayed": 1792000000000, "favourite": true, "hiddenFromHistory": true},
              {"system": "Nintendo 64", "name": "Super Mario 64 (USA).z64", "plays": 1, "playTimeMs": 5000,
               "lastPlayed": 5}
            ]}
            """.trimIndent(),
        )
        clock = 1_793_000_000_000
        val store = store()
        store.gameStarted(mario, "content://m", newPlay = true)
        store.gameEnded(mario, played(7_000))
        val games = saved()
        val tetrisSaved = games.getValue(tetris)
        assertEquals("content://a", tetrisSaved.getString("uri"))
        assertEquals(4, tetrisSaved.getInt("plays"))
        assertEquals(3_600_000L, tetrisSaved.getLong("playTimeMs"))
        assertEquals(1_792_000_000_000L, tetrisSaved.getLong("lastPlayed"))
        assertTrue(tetrisSaved.getBoolean("favourite"))
        assertTrue(tetrisSaved.getBoolean("hiddenFromHistory"))
        val marioSaved = games.getValue(mario)
        assertEquals("content://m", marioSaved.getString("uri"))
        assertEquals(2, marioSaved.getInt("plays"))
        assertEquals(12_000L, marioSaved.getLong("playTimeMs"))
        assertEquals(1_793_000_000_000L, marioSaved.getLong("lastPlayed"))
        assertFalse(marioSaved.getBoolean("favourite"))
    }

    @Test
    fun anUnparsableFileIsMovedAsideAndTheStoreStartsEmpty() {
        val cutOff = """{"version":1,"games":[{"system":"Game Boy""""
        file.writeText(cutOff)
        store().gameStarted(mario, "content://m", newPlay = true)
        assertEquals(cutOff, folder.root.resolve("games.json.corrupt").readText())
        assertEquals(setOf(mario), saved().keys)
        assertTrue(logged.any { "couldn't be parsed" in it })
    }

    @Test
    fun aRecordWithoutANameMakesTheFileUnparsable() {
        file.writeText("""{"version":1,"games":[{"system":"Game Boy","plays":3}]}""")
        store().gameStarted(tetris, "content://a", newPlay = true)
        assertTrue(folder.root.resolve("games.json.corrupt").exists())
        assertEquals(1, saved().getValue(tetris).getInt("plays"))
    }

    @Test
    fun aFileFromANewerFormatIsNeverWrittenOver() {
        val newer = """{"version":2,"games":[],"shelves":[]}"""
        file.writeText(newer)
        val store = store()
        store.gameStarted(tetris, "content://a", newPlay = true)
        store.checkpoint(played(1_000))
        store.gameEnded(tetris, played(2_000))
        store.refreshUris("Game Boy", listOf("Tetris (World).gb" to "content://b"))
        assertEquals(newer, file.readText())
        assertFalse(folder.root.resolve("games.json.tmp").exists())
        assertTrue(logged.any { "newer" in it })
    }

    @Test
    fun aPartFileLeftByACrashIsIgnoredAndReplaced() {
        val partial = folder.root.resolve("games.json.tmp")
        partial.writeText("""{"version":1,"ga""")
        store().gameStarted(tetris, "content://a", newPlay = true)
        assertFalse(partial.exists())
        assertEquals(setOf(tetris), saved().keys)
    }

    @Test
    fun pastTheCapTheGamePlayedLongestAgoGoesButNeverAFavourite() {
        file.writeText(
            """
            {"version": 1, "games": [
              {"system": "Game Boy", "name": "Oldest favourite.gb", "plays": 1, "lastPlayed": 100, "favourite": true},
              {"system": "Game Boy", "name": "Old.gb", "plays": 1, "lastPlayed": 200},
              {"system": "Game Boy", "name": "Newer.gb", "plays": 1, "lastPlayed": 300}
            ]}
            """.trimIndent(),
        )
        clock = 400
        store(maxRecords = 3).gameStarted(tetris, "content://a", newPlay = true)
        assertEquals(
            setOf("Oldest favourite.gb", "Newer.gb", "Tetris (World).gb"),
            saved().keys.map { it.name }.toSet(),
        )
    }
}
