package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

/** The downloader against a server that's a map of pages: nothing leaves the test. */
class ArtDownloaderTest {
    @get:Rule
    val folder = TemporaryFolder()

    private var clock = 1_000L
    private val hour = 3_600_000L
    private val gameBoy = "Nintendo - Game Boy"
    private val tetris = GameKey("Game Boy", "Tetris (World).gb")
    private val secret = GameKey("Game Boy", "My Secret Homebrew.gb")
    private val us = listOf("US", "W", "JP", "EU")

    /** The server: listing pages and pictures by URL; anything else fails as no network would. */
    private inner class Server : ArtFetcher {
        val asked = mutableListOf<String>()
        val pages = HashMap<String, () -> String>()
        val pictures = HashMap<String, () -> Unit>()

        override fun listing(url: String): String {
            asked += url
            return (pages[url] ?: throw IOException("offline"))()
        }

        override fun picture(url: String, kind: ArtKind, file: File) {
            asked += url
            (pictures[url] ?: throw IOException("offline"))()
            file.writeBytes(ByteArray(10))
        }
    }

    private fun page(vararg names: String) =
        names.joinToString("\n") { "<a href=\"${encodeUriComponent(it)}.png\">$it</a>" }

    private val server = Server()
    // The folder exists only once the test has started.
    private val store by lazy { ArtStore(File(folder.root, "art"), now = { clock }) }
    private fun downloader(arcade: Map<String, String> = emptyMap(), kinds: List<ArtKind> = listOf(ArtKind.BOX)) =
        ArtDownloader(
            store, ArtListingCache(File(folder.root, "listings"), now = { clock }), server, { arcade }, { clock },
            CoroutineScope(Dispatchers.Unconfined), {},
        ).also { it.configure(kinds, us) }

    private fun ArtDownloader.idle() = runBlocking { withTimeout(5_000) { awaitIdle() } }

    @Test fun aListingIsFetchedOnceAndOnlyMatchedGamesAreAskedFor() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World) (Rev 1)", "Tetris (Japan)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World) (Rev 1)")] = {}
        val art = downloader()
        art.request(listOf(ArtGame(tetris), ArtGame(secret)))
        art.idle()
        assertEquals(
            listOf(listingUrl(gameBoy, ArtKind.BOX), pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World) (Rev 1)")),
            server.asked,
        )
        assertTrue("an unmatched name never leaves the device", server.asked.none { "Secret" in it })
        assertTrue(store.boxes.value.getValue(tetris).isFile)
        assertFalse("a miss isn't asked for again soon", store.wanted(secret, ArtKind.BOX))
        clock += hour
        assertFalse("for a week", store.wanted(secret, ArtKind.BOX))
    }

    @Test fun aPageListingNoPicturesIsAFailureNotAMiss() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { "<html><body>Bad gateway</body></html>" }
        val art = downloader()
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertFalse(store.wanted(tetris, ArtKind.BOX))
        clock += hour
        assertTrue("tried again in an hour", store.wanted(tetris, ArtKind.BOX))
    }

    @Test fun aFolderTheServerHasntIsAListingOfNothing() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { throw ArtGone("HTTP 404") }
        val art = downloader()
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        clock += hour
        assertFalse("a miss, not a failure", store.wanted(tetris, ArtKind.BOX))
    }

    @Test fun aPictureThatsGoneIsAMissAndOneThatFailedIsTriedAgainInAnHour() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)", "My Secret Homebrew") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = { throw ArtGone("HTTP 404") }
        val art = downloader()
        art.request(listOf(ArtGame(tetris), ArtGame(secret)))
        art.idle()
        clock += hour
        assertFalse(store.wanted(tetris, ArtKind.BOX))
        assertTrue(store.wanted(secret, ArtKind.BOX))
    }

    @Test fun withDownloadingOffNothingIsAskedFor() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        val art = downloader(kinds = emptyList())
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertTrue(server.asked.isEmpty())
    }

    @Test fun nothingStartsWhileAGameIsLoaded() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {}
        val art = downloader()
        art.setPaused(true)
        art.request(listOf(ArtGame(tetris)))
        assertTrue(server.asked.isEmpty())
        art.setPaused(false)
        art.idle()
        assertTrue(tetris in store.boxes.value)
    }

    @Test fun eachKindHasItsOwnFolder() {
        for (kind in ArtKind.entries) {
            server.pages[listingUrl(gameBoy, kind)] = { page("Tetris (World)") }
            server.pictures[pictureUrl(gameBoy, kind, "Tetris (World)")] = {}
        }
        val art = downloader(kinds = ArtKind.entries)
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertEquals(ArtStats(3, 30), store.stats.value)
        val logo = "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Logos/Tetris%20(World).png"
        assertTrue(server.asked.contains(logo))
    }

    @Test fun anArcadeGameIsAskedForByItsTitle() {
        val boxing = GameKey("Arcade", "chboxing.zip")
        server.pages[listingUrl("MAME", ArtKind.BOX)] = { page("Champion Boxing") }
        server.pictures[pictureUrl("MAME", ArtKind.BOX, "Champion Boxing")] = {}
        val art = downloader(arcade = mapOf("chboxing" to "Champion Boxing"))
        art.request(listOf(ArtGame(boxing)))
        art.idle()
        assertTrue(boxing in store.boxes.value)
    }

    @Test fun anArcadeGameArcadeBmlDoesntKnowAsksForNothing() {
        val unknown = GameKey("Arcade", "notalisted.zip")
        val art = downloader(arcade = mapOf("chboxing" to "Champion Boxing"))
        art.request(listOf(ArtGame(unknown)))
        art.idle()
        assertTrue("not even the listing", server.asked.isEmpty())
        assertFalse("a miss", store.wanted(unknown, ArtKind.BOX))
    }

    @Test fun theGamesBeingDownloadedDontMakeRoomForEachOtherWhileTheClockTicks() {
        // Every reading of the clock is a millisecond later, as a real one can be between two calls.
        var ticking = 1_000L
        val tick = { ticking++ }
        val full = ArtStore(File(folder.root, "full"), now = tick, capBytes = 10)
        val contra = GameKey("Game Boy", "Contra - The Alien Wars (USA).gb")
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)", "Contra - The Alien Wars (USA)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {}
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Contra - The Alien Wars (USA)")] = {}
        val art = ArtDownloader(
            full, ArtListingCache(File(folder.root, "listings"), now = tick), server, { emptyMap() }, tick,
            CoroutineScope(Dispatchers.Unconfined), {},
        ).also { it.configure(listOf(ArtKind.BOX), us) }
        art.request(listOf(ArtGame(tetris), ArtGame(contra)))
        art.idle()
        assertTrue("the first game's picture isn't given up for the second's", tetris in full.boxes.value)
        assertFalse("the second waits for room", contra in full.boxes.value)
    }

    @Test fun noRequestStartsOnceAGameIsLoadingNotEvenForTheGameBeingDownloaded() {
        val boxes = listingUrl(gameBoy, ArtKind.BOX)
        val snaps = listingUrl(gameBoy, ArtKind.SNAP)
        val box = pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")
        val snap = pictureUrl(gameBoy, ArtKind.SNAP, "Tetris (World)")
        lateinit var art: ArtDownloader
        // A game starts loading while the box art listing is being fetched, and again while the box art is.
        server.pages[boxes] = { art.setPaused(true); page("Tetris (World)") }
        server.pictures[box] = { art.setPaused(true) }
        server.pages[snaps] = { page("Tetris (World)") }
        server.pictures[snap] = {}
        art = downloader(kinds = listOf(ArtKind.BOX, ArtKind.SNAP))
        art.request(listOf(ArtGame(tetris)))
        assertEquals("the listing fetched as the game started finishes; its picture waits", listOf(boxes), server.asked)
        art.setPaused(false)
        assertEquals(
            "the picture fetched as the next game started finishes; the next kind waits",
            listOf(boxes, box),
            server.asked,
        )
        art.setPaused(false)
        art.idle()
        assertEquals(listOf(boxes, box, snaps, snap), server.asked)
    }

    @Test fun aPictureIsDecodedSmallEnoughOrRefused() {
        assertEquals("box art at most halved to its width", 4, artDecodeSample(4096, 4096, ArtKind.BOX))
        assertEquals("a small one as it is", 1, artDecodeSample(400, 560, ArtKind.BOX))
        assertEquals("a tall one halved for its pixels, not its width", 2, artDecodeSample(600, 8000, ArtKind.BOX))
        assertEquals("a wide logo", 4, artDecodeSample(3200, 200, ArtKind.LOGO))
        assertEquals("over 4096 x 4096", null, artDecodeSample(4097, 4096, ArtKind.BOX))
        assertEquals("one that would take 400 MB, small as a file", null, artDecodeSample(1000, 100_000, ArtKind.BOX))
        assertEquals("one side over 16 times the other", null, artDecodeSample(4000, 200, ArtKind.LOGO))
        assertEquals("no size: not an image", null, artDecodeSample(0, 10, ArtKind.BOX))
        assertEquals(null, artDecodeSample(-1, -1, ArtKind.BOX))
    }

    @Test fun reDownloadingAGameFetchesItsFolderListingAgain() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {}
        val art = downloader()
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertEquals("nothing asked for a game that has its picture", 2, server.asked.size)
        art.redownload(ArtGame(tetris))
        art.idle()
        assertEquals(2, server.asked.count { it == listingUrl(gameBoy, ArtKind.BOX) })
        assertTrue(tetris in store.boxes.value)
    }
}
