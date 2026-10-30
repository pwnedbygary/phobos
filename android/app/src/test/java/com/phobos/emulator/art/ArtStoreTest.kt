package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class ArtStoreTest {
    @get:Rule
    val folder = TemporaryFolder()

    private var clock = 1_000L
    private val hour = 3_600_000L
    private val day = 24 * hour
    private val tetris = GameKey("Game Boy", "Tetris (World).gb")
    private val contra = GameKey("Famicom", "Contra (USA).nes")
    private val zelda = GameKey("Super Famicom", "Zelda (USA).sfc")

    private fun store(cap: Long = ART_CAP_BYTES) = ArtStore(folder.root.resolve("art"), now = { clock }, capBytes = cap)

    /** A downloaded picture of [bytes] bytes, as the downloader hands it over. */
    private fun ArtStore.give(key: GameKey, kind: ArtKind, bytes: Int) {
        val file = newTempFile()
        file.writeBytes(ByteArray(bytes))
        stored(key, kind, file)
    }

    @Test fun aGameIsWantedOnceSeenAndNotOnceItHasItsPicture() {
        val store = store()
        assertFalse("unknown games aren't downloaded", store.wanted(tetris, ArtKind.BOX))
        store.seen(listOf(ArtGame(tetris)))
        assertTrue(store.wanted(tetris, ArtKind.BOX))
        store.give(tetris, ArtKind.BOX, 10)
        assertFalse(store.wanted(tetris, ArtKind.BOX))
        assertTrue("force downloads it again", store.wanted(tetris, ArtKind.BOX, force = true))
        assertTrue(store.boxes.value.getValue(tetris).isFile)
        assertEquals(ArtStats(1, 10), store.stats.value)
    }

    @Test fun aMissWaitsAWeekAndAFailureAnHour() {
        val store = store()
        store.seen(listOf(ArtGame(tetris), ArtGame(contra)))
        store.missed(tetris, ArtKind.BOX, failed = false)
        store.missed(contra, ArtKind.BOX, failed = true)
        clock += hour - 1
        assertFalse(store.wanted(contra, ArtKind.BOX))
        clock += 1
        assertTrue(store.wanted(contra, ArtKind.BOX))
        assertFalse(store.wanted(tetris, ArtKind.BOX))
        assertTrue("retry looks again now", store.wanted(tetris, ArtKind.BOX, retry = true))
        clock += 7 * day
        assertTrue(store.wanted(tetris, ArtKind.BOX))
    }

    @Test fun forgettingTheMissesLooksAgainForEveryGame() {
        val store = store()
        store.seen(listOf(ArtGame(tetris)))
        store.missed(tetris, ArtKind.SNAP, failed = false)
        store.forgetMisses()
        assertTrue(store.wanted(tetris, ArtKind.SNAP))
    }

    @Test fun everythingSurvivesTheAppStartingAgain() {
        val first = store()
        first.seen(listOf(ArtGame(tetris, "TETRIS")))
        first.give(tetris, ArtKind.BOX, 20)
        first.missed(tetris, ArtKind.SNAP, failed = true)
        first.flush()
        val second = store()
        second.load()
        assertTrue(second.boxes.value.getValue(tetris).isFile)
        assertFalse(second.wanted(tetris, ArtKind.SNAP))
        assertEquals(listOf(ArtGame(tetris, "TETRIS")), second.known())
        clock += hour
        assertTrue(second.wanted(tetris, ArtKind.SNAP))
    }

    @Test fun roomIsMadeFromTheGamesSeenLongestAgoButNeverTheKeptOnes() {
        val store = store(cap = 100)
        store.seen(listOf(ArtGame(tetris)))
        clock += 1
        store.seen(listOf(ArtGame(contra)))
        store.give(tetris, ArtKind.BOX, 60)
        store.give(contra, ArtKind.BOX, 60)
        clock += 1
        assertTrue(store.makeRoom(keep = setOf(tetris), since = clock))
        assertTrue("the kept game's picture stays", tetris in store.boxes.value)
        assertFalse(contra in store.boxes.value)
        assertTrue("its game is looked for again", store.wanted(contra, ArtKind.BOX))
    }

    @Test fun roomIsntMadeFromTheGamesBeingDownloadedNow() {
        val store = store(cap = 100)
        store.seen(listOf(ArtGame(tetris), ArtGame(contra)))
        store.give(tetris, ArtKind.BOX, 60)
        store.give(contra, ArtKind.BOX, 60)
        assertFalse("both were seen at the time asked for", store.makeRoom(emptySet(), since = clock))
        assertEquals(2, store.boxes.value.size)
    }

    @Test fun underTheCapNothingGoes() {
        val store = store(cap = 1000)
        store.seen(listOf(ArtGame(tetris)))
        store.give(tetris, ArtKind.BOX, 60)
        clock += 1
        assertTrue(store.makeRoom(emptySet(), since = clock))
        assertEquals(1, store.boxes.value.size)
    }

    @Test fun anUnreadableFileIsMovedAsideAndItsPicturesDeleted() {
        val dir = folder.root.resolve("art")
        File(dir, "box").mkdirs()
        val orphan = File(dir, "box/old.jpg").apply { writeBytes(ByteArray(5)) }
        File(dir, ArtStore.FILE_NAME).writeText("{not json")
        val store = store()
        store.load()
        assertTrue(File(dir, "${ArtStore.FILE_NAME}.corrupt").isFile)
        assertFalse(orphan.exists())
        assertTrue(store.known().isEmpty())
        store.seen(listOf(ArtGame(zelda)))
        assertTrue("downloads go on", store.wanted(zelda, ArtKind.BOX))
    }

    @Test fun aFileFromANewerPhobosIsLeftAloneAndNothingIsDownloaded() {
        val file = folder.root.resolve("art").apply { mkdirs() }.resolve(ArtStore.FILE_NAME)
        val text = """{"version":2,"games":[]}"""
        file.writeText(text)
        val store = store()
        assertFalse(store.writable())
        store.seen(listOf(ArtGame(zelda)))
        assertFalse(store.wanted(zelda, ArtKind.BOX))
        store.flush()
        assertEquals(text, file.readText())
    }

    @Test fun forgettingAllDeletesThePicturesButKeepsTheGames() {
        val store = store()
        store.seen(listOf(ArtGame(tetris)))
        store.give(tetris, ArtKind.LOGO, 8)
        val logo = folder.root.resolve("art/logo").listFiles()!!.single()
        store.forgetAll()
        assertFalse(logo.exists())
        assertEquals(ArtStats(0, 0), store.stats.value)
        assertTrue(store.wanted(tetris, ArtKind.LOGO))
        store.forget(tetris)
        assertEquals(listOf(ArtGame(tetris)), store.known())
    }

    @Test fun listingsAreKeptTwoWeeks() {
        val cache = ArtListingCache(folder.root.resolve("listings"), now = { clock })
        assertNull(cache.get("Nintendo - Game Boy", ArtKind.BOX))
        cache.put("Nintendo - Game Boy", ArtKind.BOX, listOf("Tetris (World)"))
        assertEquals(listOf("Tetris (World)"), cache.get("Nintendo - Game Boy", ArtKind.BOX)!!.names)
        assertNull("another kind is another listing", cache.get("Nintendo - Game Boy", ArtKind.SNAP))
        clock += 14 * day
        assertNull(cache.get("Nintendo - Game Boy", ArtKind.BOX))
        cache.put("Nintendo - Game Boy", ArtKind.BOX, emptyList())
        assertEquals(emptyList<String>(), cache.get("Nintendo - Game Boy", ArtKind.BOX)!!.names)
        cache.dropAll()
        assertNull(cache.get("Nintendo - Game Boy", ArtKind.BOX))
    }
}
