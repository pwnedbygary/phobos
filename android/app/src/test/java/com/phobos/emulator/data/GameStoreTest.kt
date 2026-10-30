package com.phobos.emulator.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
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

    /** A game's files at [uri] alone. */
    private fun at(uri: String) = GameFiles(uri)

    /** A store that runs each call on the test's thread, in order, as the app's runs them on its own thread. */
    private fun store(maxRecords: Int = GameStore.MAX_RECORDS, uris: GameUris = GameUris.LASTING) =
        GameStore(
            file, Executor { it.run() }, now = { clock }, maxRecords = maxRecords, uris = uris, log = { logged += it },
        )

    /** URIs of which only [lasting] last and only [opening] open. */
    private class SomeUris(lasting: Set<String>, opening: Set<String>) : GameUris {
        private val lastingUris = lasting
        private val openingUris = opening

        override fun lasting(uri: String) = uri in lastingUris
        override fun opens(uri: String) = uri in openingUris
    }

    /** The games the file holds, read with org.json rather than the store. */
    private fun saved(): Map<GameKey, JSONObject> {
        val root = JSONObject(file.readText())
        assertEquals(1, root.getInt("version"))
        val games = root.getJSONArray("games")
        return (0 until games.length()).map { games.getJSONObject(it) }
            .associateBy { GameKey(it.getString("system"), it.getString("name")) }
    }

    /** The record of [key] in [store]'s published games. */
    private fun shown(store: GameStore, key: GameKey) = store.games.value.orEmpty().single { it.key == key }

    @Test
    fun aMissingFileIsWrittenOnlyWhenARecordIsMade() {
        val store = store()
        store.refreshFiles("Game Boy", listOf("Tetris (World).gb" to at("content://tree/a/document/1")))
        store.checkpoint(played(5_000))
        store.gameEnded(tetris, played(5_000))
        store.setFavourite(tetris, false, at("content://a"))
        store.hideFromHistory(tetris)
        store.setMissing(tetris, true)
        assertFalse(file.exists())
    }

    @Test
    fun aStartedGameIsRecordedWithItsUriOnePlayAndTheTime() {
        clock = 1_793_000_000_000
        store().gameStarted(tetris, at("content://tree/a/document/1"), newPlay = true)
        val game = saved().getValue(tetris)
        assertEquals("content://tree/a/document/1", game.getString("uri"))
        assertEquals(1, game.getInt("plays"))
        assertEquals(0L, game.getLong("playTimeMs"))
        assertEquals(1_793_000_000_000L, game.getLong("lastPlayed"))
        assertFalse(game.getBoolean("favourite"))
        assertFalse(game.getBoolean("hiddenFromHistory"))
        assertFalse(game.getBoolean("missing"))
        assertFalse(game.has("parentUri"))
        assertFalse(game.has("discs"))
    }

    @Test
    fun theSameRomNameUnderTwoSystemsIsTwoGames() {
        val store = store()
        store.gameStarted(GameKey("Game Boy", "Tetris.zip"), at("content://a"), newPlay = true)
        store.gameEnded(GameKey("Game Boy", "Tetris.zip"), played(1_000))
        store.gameStarted(GameKey("Game Boy Color", "Tetris.zip"), at("content://b"), newPlay = true)
        val games = saved()
        assertEquals(2, games.size)
        assertEquals(1_000L, games.getValue(GameKey("Game Boy", "Tetris.zip")).getLong("playTimeMs"))
        assertEquals(0L, games.getValue(GameKey("Game Boy Color", "Tetris.zip")).getLong("playTimeMs"))
    }

    @Test
    fun aReloadIsNotAnotherPlayButItsTimeCounts() {
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        // The Reload hotkey closes the session, then loads the same game again.
        store.gameEnded(tetris, played(60_000))
        store.gameStarted(tetris, at("content://a"), newPlay = false)
        store.gameEnded(tetris, played(30_000, thread = 2))
        val game = saved().getValue(tetris)
        assertEquals(1, game.getInt("plays"))
        assertEquals(90_000L, game.getLong("playTimeMs"))
    }

    @Test
    fun checkpointsCountEachStretchOfPlayOnce() {
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.checkpoint(played(1_000))
        store.checkpoint(played(1_000))
        store.checkpoint(played(2_500))
        store.gameEnded(tetris, played(4_000))
        assertEquals(4_000L, saved().getValue(tetris).getLong("playTimeMs"))
    }

    @Test
    fun aNewEmulationThreadsClockCountsFromZero() {
        val store = store()
        store.gameStarted(mario, at("content://a"), newPlay = true)
        store.checkpoint(played(5_000))
        // A 64DD disk starts another emulation thread, whose clock counts from 0 again and soon passes the old reading.
        store.checkpoint(played(9_000, thread = 2))
        store.gameEnded(mario, played(9_500, thread = 2))
        assertEquals(14_500L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun aReadingBelowTheLastIsAClockStartedAgain() {
        val store = store()
        store.gameStarted(mario, at("content://a"), newPlay = true)
        store.checkpoint(played(5_000))
        store.checkpoint(played(5_200))
        store.checkpoint(played(10))
        store.gameEnded(mario, played(1_000))
        assertEquals(6_200L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun aClockReadBeforeTheNewEmulationThreadStartsIsNotCountedAgain() {
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true, clock = played(0, thread = 0))
        store.gameEnded(tetris, played(60_000))
        // The next load succeeds before its emulation thread has started: the reading taken then, and a pause's just
        // after, still show the last thread's id and clock.
        store.gameStarted(mario, at("content://m"), newPlay = true, clock = played(60_000))
        store.checkpoint(played(60_000))
        store.gameEnded(mario, played(2_000, thread = 2))
        assertEquals(60_000L, saved().getValue(tetris).getLong("playTimeMs"))
        assertEquals(2_000L, saved().getValue(mario).getLong("playTimeMs"))
    }

    @Test
    fun playTimeAddsUpOverSessionsAndAnUnreadClockAddsNothing() {
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.gameEnded(tetris, played(120_000))
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.checkpoint(null)
        store.gameEnded(tetris, null)
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.gameEnded(tetris, played(45_000, thread = 3))
        val game = saved().getValue(tetris)
        assertEquals(3, game.getInt("plays"))
        assertEquals(165_000L, game.getLong("playTimeMs"))
    }

    @Test
    fun lastPlayedMovesToTheStopAndAnotherGamesStopIsIgnored() {
        val store = store()
        clock = 10_000
        store.gameStarted(tetris, at("content://a"), newPlay = true)
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
        store.gameStarted(tetris, at("content://tree/old/document/1"), newPlay = true)
        store.gameEnded(tetris, played(1_000))
        store.refreshFiles(
            "Game Boy",
            listOf(
                "Tetris (World).gb" to at("content://tree/new/document/1"),
                "Dr. Mario (World).gb" to at("content://tree/new/document/2"),
            ),
        )
        store.refreshFiles("Game Boy Color", listOf("Tetris (World).gb" to at("content://tree/gbc/document/1")))
        val games = saved()
        assertEquals(setOf(tetris), games.keys)
        assertEquals("content://tree/new/document/1", games.getValue(tetris).getString("uri"))
        assertEquals(1_000L, games.getValue(tetris).getLong("playTimeMs"))
    }

    @Test
    fun aScanKeepsTheUriItHasWhenTheSameNameIsInTwoFolders() {
        val store = store()
        store.gameStarted(tetris, at("content://tree/b/document/1"), newPlay = true)
        store.refreshFiles(
            "Game Boy",
            listOf(
                "Tetris (World).gb" to at("content://tree/a/document/1"),
                "Tetris (World).gb" to at("content://tree/b/document/1"),
            ),
        )
        assertEquals("content://tree/b/document/1", saved().getValue(tetris).getString("uri"))
    }

    @Test
    fun recordsSurviveTheAppStartingAgain() {
        store().apply {
            gameStarted(tetris, at("content://a"), newPlay = true)
            gameEnded(tetris, played(30_000))
        }
        store().apply {
            gameStarted(tetris, at("content://a"), newPlay = true)
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
        store.gameStarted(mario, at("content://m"), newPlay = true)
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
    fun aFileFromTheFirstReleaseLoadsUnchanged() {
        // Exactly as the first release (#317) wrote it: none of the optional fields.
        val first = """{"version":1,"games":[{"system":"PlayStation","name":"Final Fantasy VII (USA)",""" +
            """"uri":"content://tree/psx/document/disc1","plays":2,"playTimeMs":7200000,"lastPlayed":1793000000000,""" +
            """"favourite":false,"hiddenFromHistory":false}]}"""
        file.writeText(first)
        val store = store()
        store.load()
        assertEquals(
            listOf(
                GameRecord(
                    GameKey("PlayStation", "Final Fantasy VII (USA)"), uri = "content://tree/psx/document/disc1",
                    plays = 2, playTimeMs = 7_200_000, lastPlayed = 1_793_000_000_000,
                ),
            ),
            store.games.value,
        )
        assertEquals(first, file.readText())
    }

    @Test
    fun everyNewFieldSurvivesTheAppStartingAgain() {
        val ff7 = GameKey("PlayStation", "Final Fantasy VII (USA).m3u")
        val files = GameFiles(
            uri = "content://tree/psx/document/disc1",
            parentUri = "content://tree/psx/document/ff7",
            discs = listOf(
                GameDisc("FF7 (Disc 1).cue", "content://tree/psx/document/disc1", "content://tree/psx/document/ff7"),
                GameDisc("FF7 (Disc 2).cue", "content://tree/psx/document/disc2", "content://tree/psx/document/ff7/2"),
                GameDisc("FF7 (Disc 3).cue", "content://tree/psx/document/disc3"),
            ),
            title = "FINAL FANTASY VII",
        )
        clock = 5_000
        store().apply {
            gameStarted(ff7, files, newPlay = true)
            gameEnded(ff7, played(1_000))
            setFavourite(ff7, true, null)
            hideFromHistory(ff7)
            setMissing(ff7, true)
        }
        val again = store()
        again.load()
        assertEquals(
            GameRecord(
                ff7, uri = "content://tree/psx/document/disc1", plays = 1, playTimeMs = 1_000, lastPlayed = 5_000,
                favourite = true, hiddenFromHistory = true, parentUri = "content://tree/psx/document/ff7",
                discs = files.discs, title = "FINAL FANTASY VII", missing = true,
            ),
            shown(again, ff7),
        )
        assertFalse(saved().getValue(ff7).getJSONArray("discs").getJSONObject(2).has("parentUri"))
    }

    @Test
    fun theFirstReleasesReaderReadsAFileWithEveryNewField() {
        val ff7 = GameKey("PlayStation", "Final Fantasy VII (USA).m3u")
        val files = GameFiles(
            uri = "content://tree/psx/document/disc1",
            parentUri = "content://tree/psx/document/ff7",
            discs = listOf(
                GameDisc("FF7 (Disc 1).cue", "content://tree/psx/document/disc1", "content://tree/psx/document/ff7"),
                GameDisc("FF7 (Disc 2).cue", "content://tree/psx/document/disc2"),
            ),
            title = "FINAL FANTASY VII",
        )
        clock = 5_000
        store().apply {
            gameStarted(ff7, files, newPlay = true)
            gameEnded(ff7, played(1_000))
            setFavourite(ff7, true, null)
            hideFromHistory(ff7)
            setMissing(ff7, true)
            // A favorite never played, found nowhere yet: a record with no URI.
            setFavourite(tetris, true, null)
        }
        val written = saved().getValue(ff7)
        for (field in listOf("parentUri", "discs", "title", "missing")) assertTrue(field, written.has(field))
        assertEquals(2, written.getJSONArray("discs").length())
        // Read as the first release reads it (GameStore.kt:182-191 at aa4cf731): format 1, then each game in turn.
        val root = JSONObject(file.readText())
        assertEquals(1, root.getInt("version"))
        val games = root.getJSONArray("games")
        assertEquals(
            listOf(
                FirstRelease.GameRecord(FirstRelease.GameKey("Game Boy", "Tetris (World).gb"), favourite = true),
                FirstRelease.GameRecord(
                    FirstRelease.GameKey("PlayStation", "Final Fantasy VII (USA).m3u"),
                    uri = "content://tree/psx/document/disc1", plays = 1, playTimeMs = 1_000, lastPlayed = 5_000,
                    favourite = true, hiddenFromHistory = true,
                ),
            ),
            (0 until games.length()).map { FirstRelease.parse(games.getJSONObject(it)) },
        )
    }

    @Test
    fun gamesIsNullUntilTheFileIsReadThenFollowsEachChange() {
        val store = store()
        assertNull(store.games.value)
        store.load()
        assertEquals(emptyList<GameRecord>(), store.games.value)
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        assertEquals(1, shown(store, tetris).plays)
        store.setFavourite(tetris, true, null)
        assertTrue(shown(store, tetris).favourite)
    }

    @Test
    fun aGameMadeAFavouriteBeforeItsPlayedHasARecordWithNoPlays() {
        val store = store()
        val files = GameFiles("content://tree/gb/document/1", parentUri = "content://tree/gb/document/gb")
        store.setFavourite(tetris, true, files)
        val game = saved().getValue(tetris)
        assertTrue(game.getBoolean("favourite"))
        assertEquals(0, game.getInt("plays"))
        assertEquals(0L, game.getLong("lastPlayed"))
        assertEquals("content://tree/gb/document/1", game.getString("uri"))
        assertEquals("content://tree/gb/document/gb", game.getString("parentUri"))
        // No longer a favourite, it keeps its record (a scan or a start finds it again).
        store.setFavourite(tetris, false, null)
        assertFalse(saved().getValue(tetris).getBoolean("favourite"))
    }

    @Test
    fun aGameHiddenFromContinuePlayingStaysHiddenUntilItsPlayedAgain() {
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.hideFromHistory(tetris)
        assertTrue(saved().getValue(tetris).getBoolean("hiddenFromHistory"))
        store.gameEnded(tetris, played(1_000))
        store.gameStarted(tetris, at("content://a"), newPlay = false)
        assertTrue(saved().getValue(tetris).getBoolean("hiddenFromHistory"))
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        assertFalse(saved().getValue(tetris).getBoolean("hiddenFromHistory"))
    }

    @Test
    fun missingIsUndoneByAStartOrByAScanThatListsTheGame() {
        val store = store()
        store.gameStarted(tetris, at("content://tree/a/document/1"), newPlay = true)
        store.setMissing(tetris, true)
        assertTrue(saved().getValue(tetris).getBoolean("missing"))
        store.gameStarted(tetris, at("content://tree/a/document/1"), newPlay = true)
        assertFalse(saved().getValue(tetris).getBoolean("missing"))
        store.setMissing(tetris, true)
        store.refreshFiles("Game Boy", listOf("Dr. Mario (World).gb" to at("content://tree/a/document/2")))
        assertTrue(saved().getValue(tetris).getBoolean("missing"))
        store.refreshFiles("Game Boy", listOf("Tetris (World).gb" to at("content://tree/a/document/1")))
        assertFalse(saved().getValue(tetris).getBoolean("missing"))
    }

    @Test
    fun aMissingGameIsNotTheFirstToGoPastTheCap() {
        file.writeText(
            """
            {"version": 1, "games": [
              {"system": "Game Boy", "name": "Old.gb", "plays": 1, "lastPlayed": 100},
              {"system": "Game Boy", "name": "Newer, missing.gb", "plays": 1, "lastPlayed": 200, "missing": true}
            ]}
            """.trimIndent(),
        )
        clock = 300
        store(maxRecords = 2).gameStarted(tetris, at("content://a"), newPlay = true)
        assertEquals(setOf("Newer, missing.gb", "Tetris (World).gb"), saved().keys.map { it.name }.toSet())
    }

    @Test
    fun aStartKeepsAStoredFileThatOpensOverOneThatMightNotLast() {
        val library = GameFiles("content://tree/gb/document/1", parentUri = "content://tree/gb/document/gb")
        val uris = SomeUris(lasting = setOf(library.uri), opening = setOf(library.uri))
        val store = store(uris = uris)
        store.gameStarted(tetris, library, newPlay = true)
        // A frontend starts it from its own document, whose grant may end with the frontend.
        store.gameStarted(tetris, at("content://frontend/document/7"), newPlay = true)
        val game = saved().getValue(tetris)
        assertEquals(library.uri, game.getString("uri"))
        assertEquals(library.parentUri, game.getString("parentUri"))
        assertEquals(2, game.getInt("plays"))
    }

    @Test
    fun aStartReplacesTheStoredFileWhenItsOwnLasts() {
        val uris = SomeUris(
            lasting = setOf("content://tree/old/document/1", "content://tree/new/document/1"),
            opening = setOf("content://tree/old/document/1", "content://tree/new/document/1"),
        )
        val store = store(uris = uris)
        store.gameStarted(tetris, at("content://tree/old/document/1"), newPlay = true)
        store.gameStarted(tetris, at("content://tree/new/document/1"), newPlay = true)
        assertEquals("content://tree/new/document/1", saved().getValue(tetris).getString("uri"))
    }

    @Test
    fun aStartReplacesAStoredFileThatNoLongerOpens() {
        val uris = SomeUris(lasting = setOf("content://tree/gb/document/1"), opening = emptySet())
        val store = store(uris = uris)
        store.gameStarted(tetris, at("content://tree/gb/document/1"), newPlay = true)
        store.gameStarted(tetris, at("content://frontend/document/7"), newPlay = true)
        assertEquals("content://frontend/document/7", saved().getValue(tetris).getString("uri"))
    }

    @Test
    fun aFirstStartRecordsAFileThatMightNotLast() {
        val store = store(uris = SomeUris(lasting = emptySet(), opening = emptySet()))
        store.gameStarted(tetris, at("content://frontend/document/7"), newPlay = true)
        assertEquals("content://frontend/document/7", saved().getValue(tetris).getString("uri"))
    }

    @Test
    fun anUnparsableFileIsMovedAsideAndTheStoreStartsEmpty() {
        val cutOff = """{"version":1,"games":[{"system":"Game Boy""""
        file.writeText(cutOff)
        store().gameStarted(mario, at("content://m"), newPlay = true)
        assertEquals(cutOff, folder.root.resolve("games.json.corrupt").readText())
        assertEquals(setOf(mario), saved().keys)
        assertTrue(logged.any { "couldn't be parsed" in it })
    }

    @Test
    fun aRecordWithoutANameMakesTheFileUnparsable() {
        file.writeText("""{"version":1,"games":[{"system":"Game Boy","plays":3}]}""")
        store().gameStarted(tetris, at("content://a"), newPlay = true)
        assertTrue(folder.root.resolve("games.json.corrupt").exists())
        assertEquals(1, saved().getValue(tetris).getInt("plays"))
    }

    @Test
    fun aFileFromANewerFormatIsNeverWrittenOver() {
        val newer = """{"version":2,"games":[],"shelves":[]}"""
        file.writeText(newer)
        val store = store()
        store.gameStarted(tetris, at("content://a"), newPlay = true)
        store.checkpoint(played(1_000))
        store.gameEnded(tetris, played(2_000))
        store.refreshFiles("Game Boy", listOf("Tetris (World).gb" to at("content://b")))
        store.setFavourite(mario, true, at("content://m"))
        assertEquals(newer, file.readText())
        assertFalse(folder.root.resolve("games.json.tmp").exists())
        assertTrue(logged.any { "newer" in it })
    }

    @Test
    fun aPartFileLeftByACrashIsIgnoredAndReplaced() {
        val partial = folder.root.resolve("games.json.tmp")
        partial.writeText("""{"version":1,"ga""")
        store().gameStarted(tetris, at("content://a"), newPlay = true)
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
        store(maxRecords = 3).gameStarted(tetris, at("content://a"), newPlay = true)
        assertEquals(
            setOf("Oldest favourite.gb", "Newer.gb", "Tetris (World).gb"),
            saved().keys.map { it.name }.toSet(),
        )
    }
}

/**
 * The first release's reader (#317: GameStore.kt at master aa4cf731), frozen here as the reference an older build reads
 * the file with: its key and record (:18, :29-37) and its parse (:202-210), word for word but for parse's `private`.
 */
private object FirstRelease {
    data class GameKey(val system: String, val name: String)

    data class GameRecord(
        val key: GameKey,
        val uri: String? = null,
        val plays: Int = 0,
        val playTimeMs: Long = 0,
        val lastPlayed: Long = 0,
        val favourite: Boolean = false,
        val hiddenFromHistory: Boolean = false,
    )

    fun parse(game: JSONObject) = GameRecord(
        key = GameKey(game.getString("system"), game.getString("name")),
        uri = if (game.isNull("uri")) null else game.getString("uri"),
        plays = game.optInt("plays").coerceAtLeast(0),
        playTimeMs = game.optLong("playTimeMs").coerceAtLeast(0),
        lastPlayed = game.optLong("lastPlayed").coerceAtLeast(0),
        favourite = game.optBoolean("favourite"),
        hiddenFromHistory = game.optBoolean("hiddenFromHistory"),
    )
}
