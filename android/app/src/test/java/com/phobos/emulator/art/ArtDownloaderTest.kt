package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
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
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

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

        val labels = HashMap<String, (File) -> LabelPicture.Format>()

        override fun label(url: String, shape: String, file: File): LabelPicture.Format? {
            asked += url
            return (labels[url] ?: throw IOException("offline"))(file)
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

    /** A scope on real threads for the tests that need a fetch to be in flight when the test acts on it. */
    private fun threadedScope() = CoroutineScope(
        SupervisorJob() + Dispatchers.Default + CoroutineExceptionHandler { _, _ -> /* asserted in the store */ },
    )

    private fun threadedDownloader(
        scope: CoroutineScope,
        kinds: List<ArtKind> = listOf(ArtKind.BOX),
        requeueMs: Long = ART_LOOKUP_REQUEUE_MS,
        labelTarget: (GameKey) -> LabelTarget? = { null },
        labelIndex: () -> LabelMatch.Index? = { null },
    ) = ArtDownloader(
        store, ArtListingCache(File(folder.root, "listings"), now = { clock }), server, { emptyMap() }, { clock },
        scope, {}, requeueMs, labelTarget, labelIndex,
    ).also { it.configure(kinds, us) }

    @Test fun aPlainLookupFailureKeepsTheFailureMappingAndTheKindsAfterItStillGo() {
        // Coverage of the pre-existing mapping, not the timeout path: a lookup that FAILS (not times out) is an
        // hourly failure. Lookup TIMEOUTS are transient - the tests below.
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { throw IOException("lookup of thumbnails.libretro.com failed") }
        server.pages[listingUrl(gameBoy, ArtKind.SNAP)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.SNAP, "Tetris (World)")] = {}
        val art = downloader(kinds = listOf(ArtKind.BOX, ArtKind.SNAP))
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertEquals("the kind after the failed lookup was still fetched", 1, store.stats.value.pictures)
        clock += 60_000
        assertFalse("the failed lookup waits an hour, not a week", store.wanted(tetris, ArtKind.BOX))
        clock += hour
        assertTrue("then it's tried again", store.wanted(tetris, ArtKind.BOX))
        assertFalse("the picture it has isn't asked for again", store.wanted(tetris, ArtKind.SNAP))
    }

    @Test fun aCancelledListingFetchRecordsNoMissAndReleasesItsWorkers() {
        val started = CountDownLatch(1)
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = {
            started.countDown()
            Thread.sleep(10_000)
            page("Tetris (World)")
        }
        val scope = threadedScope()
        val art = threadedDownloader(scope)
        art.request(listOf(ArtGame(tetris)))
        assertTrue("the listing fetch started", started.await(5, TimeUnit.SECONDS))
        assertTrue("the store knew the game before the cancel", tetris in store.known().map { it.key })
        scope.cancel()
        runBlocking { withTimeout(5_000) { art.awaitIdle() } }
        clock += 60_000
        assertTrue("a cancellation records no miss", store.wanted(tetris, ArtKind.BOX))
        assertFalse("and no picture", tetris in store.boxes.value)
    }

    @Test fun anErrorStampsTheEntryAndTheNextRequestStillDownloads() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = { throw OutOfMemoryError("too big") }
        val scope = threadedScope()
        val art = threadedDownloader(scope)
        art.request(listOf(ArtGame(tetris)))
        runBlocking { withTimeout(5_000) { art.awaitIdle() } }
        clock += 60_000
        assertFalse("an Error is stamped as a failure", store.wanted(tetris, ArtKind.BOX))
        clock += hour
        assertTrue("tried again in an hour", store.wanted(tetris, ArtKind.BOX))
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {}
        art.request(listOf(ArtGame(tetris)), force = true)
        runBlocking { withTimeout(5_000) { art.awaitIdle() } }
        assertTrue("the worker count came back so the next request downloads", tetris in store.boxes.value)
    }

    /** Polls [cond] until it holds or [ms] passes: a requeue's delay is time, not an event to await. */
    private fun waitUntil(ms: Long, what: String, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + ms
        while (System.currentTimeMillis() < end) {
            if (cond()) return
            Thread.sleep(10)
        }
        assertTrue("timed out waiting for $what", cond())
    }

    @Test fun aLookupTimeoutStampsNeitherMissNorFailure() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { throw ArtLookupException("lookup of x timed out") }
        val scope = threadedScope()
        val art = threadedDownloader(scope, requeueMs = hour)
        art.request(listOf(ArtGame(tetris)))
        runBlocking { withTimeout(5_000) { art.awaitIdle() } }
        assertTrue("still wanted: no miss and no failure", store.wanted(tetris, ArtKind.BOX))
        assertEquals("nothing fetched", 0, store.stats.value.pictures)
        scope.cancel()
    }

    // ---- the label path: same bounded resolver and transient-failure semantics as the libretro kinds ----

    private val labelsJson =
        """{"base":"https://images.launchbox-app.com/","index":{"gb":{"tetris":[["tetris.png","US","c"]]}}}"""

    @Test fun aLabelLookupTimeoutStampsNothing() {
        server.labels["https://images.launchbox-app.com/tetris.png"] = {
            throw ArtLookupException("lookup of images.launchbox-app.com timed out")
        }
        val scope = threadedScope()
        val art = threadedDownloader(
            scope, kinds = listOf(ArtKind.LABEL), requeueMs = hour,
            labelTarget = { LabelTarget("gb", "cart", "gb", "US") },
            labelIndex = { LabelMatch.parse(labelsJson) },
        )
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertTrue("still wanted: a label lookup timeout stamps nothing", store.wanted(tetris, ArtKind.LABEL))
        assertEquals("nothing stored", 0, store.stats.value.pictures)
        scope.cancel()
    }

    @Test fun aLabelLookupTimeoutIsRequeuedAndThenDownloads() {
        var calls = 0
        server.labels["https://images.launchbox-app.com/tetris.png"] = {
            calls++
            if (calls == 1) throw ArtLookupException("lookup of images.launchbox-app.com timed out")
            LabelPicture.Format.JPEG
        }
        val scope = threadedScope()
        val art = threadedDownloader(
            scope, kinds = listOf(ArtKind.LABEL), requeueMs = 200,
            labelTarget = { LabelTarget("gb", "cart", "gb", "US") },
            labelIndex = { LabelMatch.parse(labelsJson) },
        )
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertTrue("nothing stamped after the timeout", store.wanted(tetris, ArtKind.LABEL))
        waitUntil(5_000, "the re-queued label downloads") { store.stats.value.pictures == 1 }
        assertEquals("tried twice: the timeout, then the requeue", 2, calls)
        assertFalse("stored after the requeue", store.wanted(tetris, ArtKind.LABEL))
        scope.cancel()
    }

    @Test fun aLabelMatchUsesTheBundledIndexUrls() {
        server.labels["https://images.launchbox-app.com/tetris.png"] = { LabelPicture.Format.JPEG }
        val scope = threadedScope()
        val art = threadedDownloader(
            scope, kinds = listOf(ArtKind.LABEL),
            labelTarget = { LabelTarget("gb", "cart", "gb", "US") },
            labelIndex = { LabelMatch.parse(labelsJson) },
        )
        art.request(listOf(ArtGame(tetris)))
        waitUntil(5_000, "the label downloads") { store.stats.value.pictures == 1 }
        assertEquals(listOf("https://images.launchbox-app.com/tetris.png"), server.asked)
        scope.cancel()
    }

    @Test fun aLabelRespectsTheArtCapAndNeverOvershoots() {
        val capped = ArtStore(File(folder.root, "capped"), now = { clock }, capBytes = 1_000)
        val a = GameKey("Game Boy", "A (USA).gb")
        val b = GameKey("Game Boy", "B (USA).gb")
        capped.seen(listOf(ArtGame(a), ArtGame(b)))
        for (k in listOf(a, b)) {
            capped.stored(k, ArtKind.BOX, File(folder.root, "fill-${k.name}.bin").apply { writeBytes(ByteArray(600)) })
        }
        server.labels["https://images.launchbox-app.com/tetris.png"] = { LabelPicture.Format.JPEG }
        val art = ArtDownloader(
            capped, ArtListingCache(File(folder.root, "capped-listings"), now = { clock }), server, { emptyMap() },
            { clock }, CoroutineScope(Dispatchers.Unconfined), {}, ART_LOOKUP_REQUEUE_MS,
            { LabelTarget("gb", "cart", "gb", "US") }, { LabelMatch.parse(labelsJson) },
        ).also { it.configure(listOf(ArtKind.LABEL), us) }

        art.setKeep(setOf(a, b))
        art.request(listOf(ArtGame(tetris)))
        art.idle()
        assertTrue("no room, nothing asked", server.asked.isEmpty())
        assertTrue("the label stays wanted", capped.wanted(tetris, ArtKind.LABEL))
        assertEquals("the kept pictures are untouched", 2, capped.stats.value.pictures)

        art.setKeep(emptySet())
        clock += 10_000
        art.request(listOf(ArtGame(tetris)))
        // makeRoom stops evicting as soon as the total is under the cap, so one picture may stay and the label joins it.
        waitUntil(5_000, "the label downloads after making room") { capped.labels.value.containsKey(tetris) }
        assertFalse("stored after making room", capped.wanted(tetris, ArtKind.LABEL))
        assertTrue("never over the cap", capped.stats.value.bytes <= 1_000)
        assertTrue("room was made by evicting at most both kept pictures", capped.boxes.value.size <= 1)
    }

    @Test fun aTimedOutListingDoesNotBarTheFoldersOtherGames() {
        val boxes = listingUrl(gameBoy, ArtKind.BOX)
        var calls = 0
        server.pages[boxes] = {
            calls++
            if (calls == 1) {
                throw ArtLookupException("lookup of x timed out")
            } else {
                page("Tetris (World)", "My Secret Homebrew")
            }
        }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {}
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "My Secret Homebrew")] = {}
        val scope = threadedScope()
        val art = threadedDownloader(scope, requeueMs = 200)
        art.request(listOf(ArtGame(tetris), ArtGame(secret)))
        waitUntil(5_000, "both pictures after the listing was fetched again") { store.stats.value.pictures == 2 }
        assertEquals("the listing was fetched again, not barred", 2, calls)
        assertTrue("the other game got its picture", secret in store.boxes.value)
        scope.cancel()
    }

    @Test fun aLookupTimeoutIsRequeuedAndDownloadsAfterwards() {
        var attempts = 0
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {
            attempts++
            if (attempts == 1) throw ArtLookupException("timed out")
        }
        val scope = threadedScope()
        val art = threadedDownloader(scope, requeueMs = 200)
        art.request(listOf(ArtGame(tetris)))
        waitUntil(5_000, "the retry's picture") { tetris in store.boxes.value }
        assertEquals("the first attempt timed out, the retry fetched it", 2, attempts)
        scope.cancel()
    }

    @Test fun theQueueKeepsMovingWhileAJobWaitsForItsLookupRetry() {
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)", "My Secret Homebrew") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "My Secret Homebrew")] = {}
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {
            throw ArtLookupException("timed out")
        }
        val scope = threadedScope()
        val art = threadedDownloader(scope, requeueMs = hour)
        art.request(listOf(ArtGame(tetris), ArtGame(secret)))
        waitUntil(5_000, "the other game's picture while the first waits") { secret in store.boxes.value }
        assertTrue("the timed-out game is still wanted", store.wanted(tetris, ArtKind.BOX))
        scope.cancel()
    }

    @Test fun aCancelledPictureFetchRecordsNoMiss() {
        val started = CountDownLatch(1)
        val release = CountDownLatch(1)
        server.pages[listingUrl(gameBoy, ArtKind.BOX)] = { page("Tetris (World)") }
        server.pictures[pictureUrl(gameBoy, ArtKind.BOX, "Tetris (World)")] = {
            started.countDown()
            release.await(10, TimeUnit.SECONDS)
            throw kotlinx.coroutines.CancellationException("cancelled mid-picture")
        }
        val scope = threadedScope()
        val art = threadedDownloader(scope)
        art.request(listOf(ArtGame(tetris)))
        assertTrue("the picture fetch started", started.await(5, TimeUnit.SECONDS))
        scope.cancel()
        release.countDown()
        runBlocking { withTimeout(5_000) { art.awaitIdle() } }
        clock += 60_000
        assertTrue("a cancelled picture records no miss", store.wanted(tetris, ArtKind.BOX))
        assertFalse("and no picture", tetris in store.boxes.value)
    }
}
