// After Cartridge 0.4.9.3 (MIT; docs/third-party/cartridge.md): electron/scraper.js's run, index, loadIndex and
// download (a listing per folder and kind, what counts as gone or failed). Phobos's own: the queue, the pause while a
// game runs, the cap, and fetching with HttpURLConnection.
package com.phobos.emulator.art

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import com.phobos.emulator.BuildConfig
import com.phobos.emulator.data.GameKey
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.yield
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** libretro's thumbnail server, for box art, screenshots and logos; label scans come from the LaunchBox host ([ArtKind.LABEL]). */
const val ART_SERVER = "https://thumbnails.libretro.com"

/** How many pictures download at once. */
const val ART_DOWNLOADS_AT_ONCE = 4

/** A picture or a listing that isn't there to have: a 404 or 410, not an image, or too big. Not tried for a week. */
class ArtGone(message: String) : IOException(message)

/**
 * Fetches from the server. Any other exception than [ArtGone] is a failure, tried again in an hour — except a lookup
 * timeout ([ArtLookupException]), which is transient: nothing is stamped and the job is re-queued shortly.
 */
interface ArtFetcher {
    /** The listing page at [url]. */
    fun listing(url: String): String

    /** The picture at [url], scaled to [kind]'s width and written to [file] in its format. */
    fun picture(url: String, kind: ArtKind, file: File)

    /**
     * The label scan at [url], cropped to [shape] by [LabelPicture] and written to [file]; the format the label was
     * written in, or null when the scan isn't a usable label. Goes through the same bounded resolver and timeouts
     * as the other fetches.
     */
    fun label(url: String, shape: String, file: File): LabelPicture.Format?
}

/** What the label path needs to place a game: its Cartridge system and media fields, and the region code. */
data class LabelTarget(val systemId: String, val mediaType: String?, val mediaShape: String?, val region: String)

/** One game waiting for its pictures; [since] is when it was asked for, for [ArtStore.makeRoom]. */
private class ArtJob(val game: ArtGame, var force: Boolean, var retry: Boolean, val since: Long)

/**
 * Downloads games' pictures from [ART_SERVER] into [store]: a game at a time, [ART_DOWNLOADS_AT_ONCE] at once, each
 * kind looked up in the game's folders' listings (fetched once per folder and kind, and kept in [listings]), matched
 * by [matchArt]. A request names only a folder or a matched game; names with no match never leave the device. No
 * request starts while [setPaused] holds it (a game is loading or loaded); requests already going finish.
 */
class ArtDownloader internal constructor(
    private val store: ArtStore,
    private val listings: ArtListingCache,
    private val fetcher: ArtFetcher,
    private val arcade: () -> Map<String, String>,
    private val now: () -> Long,
    private val scope: CoroutineScope,
    private val log: (String) -> Unit,
    private val lookupRequeueMs: Long = ART_LOOKUP_REQUEUE_MS,
    private val labelTarget: (GameKey) -> LabelTarget? = { null },
    private val labelIndex: () -> LabelMatch.Index? = { null },
) {
    /** Set after construction by the app; null keeps the constructor's defaults (the tests' seams). */
    private var labelTargetOverride: ((GameKey) -> LabelTarget?)? = null
    private var labelIndexOverride: (() -> LabelMatch.Index?)? = null

    fun configureLabels(target: (GameKey) -> LabelTarget?, index: () -> LabelMatch.Index?) {
        labelTargetOverride = target
        labelIndexOverride = index
    }
    private val lock = Any()
    private val queue = ArrayDeque<ArtJob>()
    private val queued = HashMap<GameKey, ArtJob>()
    private var workers = 0
    private var requests = 0
    private val pending = MutableStateFlow(0)
    private val paused = MutableStateFlow(false)

    @Volatile private var kinds: List<ArtKind> = emptyList()
    @Volatile private var regionOrder: List<String> = listOf("US", "W", "JP", "EU")
    @Volatile private var keep: Set<GameKey> = emptySet()

    private val listingLock = Mutex()
    private val loading = HashMap<String, Deferred<ArtListing?>>()
    private val read = HashMap<String, Pair<ArtListing, Long>>()
    private val failedAt = HashMap<String, Long>()

    /** Each game's box art file. */
    val boxes: StateFlow<Map<GameKey, File>> = store.boxes

    /** Each game's downloaded label scan, for the carts' printed labels. */
    val labels: StateFlow<Map<GameKey, File>> = store.labels

    /** How many pictures are kept and what they take. */
    val stats: StateFlow<ArtStats> = store.stats

    /** The most the pictures may take. */
    val capBytes: Long get() = store.capBytes

    /** Which [kinds] to download (none: downloading is off, and what waits is dropped), picked in [regionOrder]. */
    fun configure(kinds: List<ArtKind>, regionOrder: List<String>) {
        this.kinds = kinds
        this.regionOrder = regionOrder
        if (kinds.isEmpty()) {
            synchronized(lock) {
                queue.clear()
                queued.clear()
                updatePending()
            }
        }
    }

    /** Holds new requests while [paused] (a game is loading or loaded); those already going finish. */
    fun setPaused(paused: Boolean) {
        this.paused.value = paused
    }

    /** The games whose pictures never make room for others: the Library's rows. */
    fun setKeep(keys: Set<GameKey>) {
        keep = keys
    }

    /**
     * Downloads [games]' missing pictures: those they lack and aren't waiting on after a miss or failure, or every one
     * ([force]), or the ones waiting too ([retry]). Nothing while downloading is off.
     */
    fun request(games: Collection<ArtGame>, force: Boolean = false, retry: Boolean = false) {
        if (kinds.isEmpty()) return
        val wanted = games.filter { hasArt(it.key.system) }
        if (wanted.isEmpty()) return
        // Counted before the launch, so awaitIdle can't see nothing pending while the request is still being queued.
        synchronized(lock) {
            requests++
            updatePending()
        }
        scope.launch {
            try {
                // Taken before the games are marked seen, so they're seen at it or later: none makes room for another.
                val since = now()
                store.seen(wanted)
                synchronized(lock) {
                    for (game in wanted) {
                        val job = queued[game.key]
                        if (job != null) {
                            job.force = job.force || force
                            job.retry = job.retry || retry
                        } else {
                            val next = ArtJob(game, force, retry, since)
                            queued[game.key] = next
                            queue.addLast(next)
                        }
                    }
                    updatePending()
                    startWorkers()
                }
            } finally {
                synchronized(lock) {
                    requests--
                    updatePending()
                }
            }
        }
    }

    /** Downloads [game]'s pictures again: its pictures, its misses and its folders' listings go first. */
    fun redownload(game: ArtGame) {
        scope.launch {
            store.forget(game.key)
            dropListings(artFolders(game.key.system, game.key.name))
            request(listOf(game), force = true)
        }
    }

    /** Looks again now for every known game's missing pictures, forgetting the misses and failures. */
    fun retryMissing() {
        scope.launch {
            store.forgetMisses()
            listingLock.withLock { failedAt.clear() }
            request(store.known(), retry = true)
        }
    }

    /** Deletes every picture and listing, then downloads every known game's pictures again. */
    fun redownloadAll() {
        scope.launch {
            store.forgetAll()
            listingLock.withLock {
                read.clear()
                failedAt.clear()
            }
            listings.dropAll()
            request(store.known())
        }
    }

    /** Suspends until nothing waits or downloads. */
    suspend fun awaitIdle() {
        pending.first { it == 0 }
    }

    private fun updatePending() {
        pending.value = queue.size + workers + requests
    }

    /**
     * Starts workers up to the limit; the caller holds [lock]. Each launch starts undispatched: [work] raises
     * [workers] before this returns (so the loop here counts it) and yields to the dispatcher before any fetch, so a
     * launch that never runs raises nothing and a caller is never held during a download.
     */
    private fun startWorkers() {
        while (workers < ART_DOWNLOADS_AT_ONCE && workers < queue.size) {
            scope.launch(start = CoroutineStart.UNDISPATCHED) { work() }
        }
    }

    /** Waits while a game is loading or loaded: called before each job, each kind and each request. */
    private suspend fun unpaused() {
        paused.first { !it }
    }

    private suspend fun work() {
        try {
            // Raised inside the coroutine: a launch cancelled before it starts raises nothing. The yield runs the rest
            // on the dispatcher, never on whatever thread requested the download.
            synchronized(lock) { workers++ }
            yield()
            while (true) {
                unpaused()
                val job = synchronized(lock) {
                    queue.removeFirstOrNull()?.also { queued.remove(it.game.key) }
                } ?: break
                try {
                    download(job)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: ArtLookupException) {
                    // Transient: nothing is stamped, the job goes back in the queue shortly.
                    requeueAfterLookupTimeout(job)
                } catch (e: Exception) {
                    log("Artwork: ${job.game.key.name} (${job.game.key.system}): ${e.message}")
                } catch (t: Throwable) {
                    // A real Error: stamp the entry as failed so the Artwork page tells the truth, then rethrow.
                    for (kind in kinds) {
                        if (store.wanted(job.game.key, kind, job.force, job.retry)) {
                            store.missed(job.game.key, kind, failed = true)
                        }
                    }
                    throw t
                }
            }
        } finally {
            val idle = synchronized(lock) {
                workers--
                updatePending()
                workers == 0
            }
            if (idle) store.flush()
        }
    }

    /**
     * A lookup that missed its deadline stamps nothing: the job goes back in the queue once after [lookupRequeueMs],
     * unless a new request already queued it or the game got the kind meanwhile. Games waiting on the same listing are
     * untouched - each fails and re-queues on its own.
     */
    private fun requeueAfterLookupTimeout(job: ArtJob) {
        scope.launch {
            delay(lookupRequeueMs)
            if (kinds.none { store.wanted(job.game.key, it, job.force, job.retry) }) return@launch
            synchronized(lock) {
                if (queued.containsKey(job.game.key)) return@launch
                queued[job.game.key] = job
                queue.addLast(job)
                updatePending()
                startWorkers()
            }
        }
    }

    private suspend fun download(job: ArtJob) {
        val kinds = kinds
        if (kinds.isEmpty() || !store.writable()) return
        val key = job.game.key
        val lookups = artLookups(job.game, arcade())
        if (lookups.isEmpty()) {
            // An arcade game Arcade.bml doesn't know: no name to look for, so no listing is fetched for it.
            for (kind in kinds) {
                if (store.wanted(key, kind, job.force, job.retry)) store.missed(key, kind, failed = false)
            }
            return
        }
        val fuzzy = fuzzyArtMatch(key.system)
        for (kind in kinds) {
            unpaused()
            if (!store.wanted(key, kind, job.force, job.retry)) continue
            if (kind == ArtKind.LABEL) {
                downloadLabel(job)
                continue
            }
            var got = false
            var failed = false
            for (folder in artFolders(key.system, key.name)) {
                val listing = listing(folder, kind)
                if (listing == null) {
                    failed = true
                    continue
                }
                val name = lookups.firstNotNullOfOrNull { matchArt(listing, it, regionOrder, fuzzy) } ?: continue
                // Full of pictures of games seen since this one was asked for: none is fetched, none counted missing.
                if (!store.makeRoom(keep, job.since)) return
                unpaused()
                val file = store.newTempFile()
                try {
                    fetcher.picture(pictureUrl(folder, kind, name), kind, file)
                    store.stored(key, kind, file)
                    got = true
                    break
                } catch (e: CancellationException) {
                    throw e
                } catch (e: ArtLookupException) {
                    throw e
                } catch (e: ArtGone) {
                    log("Artwork: $folder/${kind.folder}/$name: ${e.message}")
                } catch (e: Exception) {
                    log("Artwork: $folder/${kind.folder}/$name: ${e.message}")
                    failed = true
                } finally {
                    file.delete()
                }
            }
            if (!got) store.missed(key, kind, failed)
        }
    }

    /**
     * One game's label scan: the bundled index picks up to three LaunchBox scans for its system and name, the first
     * that crops to a usable label is stored. Same failure semantics as the other kinds: [ArtLookupException]
     * rethrows untouched (transient, re-queued, nothing stamped); [ArtGone] tries the next scan; any other failure
     * is stamped hourly; nothing found is a miss.
     */
    private suspend fun downloadLabel(job: ArtJob) {
        val key = job.game.key
        val target = (labelTargetOverride ?: labelTarget)(key)
        val index = if (target == null) null else (labelIndexOverride ?: labelIndex)()
        if (target == null || index == null) {
            store.missed(key, ArtKind.LABEL, failed = target != null)
            return
        }
        val match = LabelMatch.match(index, target.systemId, target.mediaType, target.mediaShape, key.name, target.region)
        if (match == null) {
            store.missed(key, ArtKind.LABEL, failed = false)
            return
        }
        // The art store's cap applies to labels too: no room means leave it wanted and retry later.
        if (!store.makeRoom(keep, job.since)) return
        var failed = false
        for (url in match.urls) {
            unpaused()
            val file = store.newTempFile()
            try {
                val format = fetcher.label(url, match.shape, file) ?: throw ArtGone("not a usable label scan")
                store.stored(key, ArtKind.LABEL, file, png = format == LabelPicture.Format.PNG)
                return
            } catch (e: ArtLookupException) {
                throw e
            } catch (e: ArtGone) {
                log("Artwork: $url: ${e.message}")
            } catch (e: Exception) {
                log("Artwork: $url: ${e.message}")
                failed = true
            } finally {
                file.delete()
            }
        }
        store.missed(key, ArtKind.LABEL, failed)
    }

    /**
     * [folder]'s [kind] listing, null when it couldn't be had (asked again a minute later). Fetched once however many
     * games wait on it; a folder the server doesn't have is a listing of nothing, and a page that lists no pictures a
     * failure.
     */
    private suspend fun listing(folder: String, kind: ArtKind): ArtListing? {
        val id = "$folder\n${kind.name}"
        val fetch = listingLock.withLock {
            read[id]?.let { (listing, at) -> if (now() - at in 0L until ART_LISTING_MS) return listing }
            failedAt[id]?.let { if (now() - it in 0L until LISTING_FAILURE_MS) return null }
            loading.getOrPut(id) { scope.async { fetchListing(folder, kind) } }
        }
        val listing = try {
            fetch.await()
        } catch (e: ArtLookupException) {
            // Transient: no failedAt, so the next caller fetches the listing again instead of reading a dead bar.
            listingLock.withLock { loading.remove(id) }
            throw e
        }
        listingLock.withLock {
            loading.remove(id)
            if (listing == null) failedAt[id] = now() else read[id] = listing to now()
        }
        return listing
    }

    private suspend fun fetchListing(folder: String, kind: ArtKind): ArtListing? {
        listings.get(folder, kind)?.let { return it }
        unpaused()
        return try {
            val names = parseArtListing(fetcher.listing(listingUrl(folder, kind)))
            if (names.isEmpty()) {
                log("Artwork: $folder/${kind.folder} listed no pictures")
                null
            } else {
                listings.put(folder, kind, names)
                ArtListing(names)
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: ArtLookupException) {
            throw e
        } catch (e: ArtGone) {
            listings.put(folder, kind, emptyList())
            ArtListing(emptyList())
        } catch (e: Exception) {
            log("Artwork: $folder/${kind.folder}: ${e.message}")
            null
        }
    }

    private suspend fun dropListings(folders: List<String>) {
        listingLock.withLock {
            for (folder in folders) for (kind in ArtKind.entries) {
                read.remove("$folder\n${kind.name}")
                failedAt.remove("$folder\n${kind.name}")
            }
        }
        listings.drop(folders)
    }

    companion object {
        private const val LISTING_FAILURE_MS = 60_000L

        @Volatile private var instance: ArtDownloader? = null

        /** The one downloader for the app; its pictures are in the no-backup folder, its listings in the cache. */
        fun get(context: Context): ArtDownloader = instance ?: synchronized(this) {
            instance ?: create(context.applicationContext).also { instance = it }
        }

        private fun create(app: Context): ArtDownloader {
            val log: (String) -> Unit = { Log.w("Phobos", it) }
            val titles = lazy {
                try {
                    app.assets.open("Database/Arcade.bml").bufferedReader().use { arcadeTitles(it.readText()) }
                } catch (e: IOException) {
                    log("Artwork: the arcade list couldn't be read (${e.message})")
                    emptyMap()
                }
            }
            val store = ArtStore(File(app.noBackupFilesDir, "art"), log = log)
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
            scope.launch { store.load() }
            return ArtDownloader(
                store,
                ArtListingCache(File(app.cacheDir, "art-listings")),
                HttpArtFetcher("Phobos/${BuildConfig.VERSION_NAME}", log = log),
                { titles.value },
                System::currentTimeMillis,
                scope,
                log,
            )
        }
    }
}

/** The listing of [folder]'s [kind] pictures. */
fun listingUrl(folder: String, kind: ArtKind): String = "$ART_SERVER/${encodeUriComponent(folder)}/${kind.folder}/"

/** The picture [name] in [folder]'s [kind]. */
fun pictureUrl(folder: String, kind: ArtKind, name: String): String =
    "$ART_SERVER/${encodeUriComponent(folder)}/${kind.folder}/${encodeUriComponent(name)}.png"

/** JavaScript's encodeURIComponent, as Cartridge builds the server's URLs. */
internal fun encodeUriComponent(text: String): String {
    val out = StringBuilder()
    for (byte in text.toByteArray(Charsets.UTF_8)) {
        val c = byte.toInt() and 0xff
        val ch = c.toChar()
        if (ch in 'A'..'Z' || ch in 'a'..'z' || ch in '0'..'9' || ch in "-_.!~*'()") {
            out.append(ch)
        } else {
            out.append('%').append("%02X".format(c))
        }
    }
    return out.toString()
}

/** The most pixels a downloaded picture may have (4096 x 4096): a bigger one is refused as gone, not decoded. */
internal const val ART_MAX_PIXELS = 4096L * 4096

/** How many times its shorter side a picture's longer side may be; a longer one is refused as gone. */
internal const val ART_MAX_ASPECT = 16

/** The most pixels a picture is decoded at before it's scaled (2048 x 2048, 16 MB). */
internal const val ART_DECODE_PIXELS = 2048L * 2048

/**
 * How much to scale a [width] x [height] picture down as it's decoded for [kind] (Android's inSampleSize): halved
 * while it stays at least the kind's width, and while it has more than [ART_DECODE_PIXELS]. Null for one refused as
 * gone: no size, more than [ART_MAX_PIXELS], or one side over [ART_MAX_ASPECT] times the other.
 */
internal fun artDecodeSample(width: Int, height: Int, kind: ArtKind): Int? {
    if (width <= 0 || height <= 0) return null
    if (width.toLong() * height > ART_MAX_PIXELS) return null
    if (maxOf(width, height) > minOf(width, height).toLong() * ART_MAX_ASPECT) return null
    var sample = 1
    while (width / (sample * 2) >= kind.width || (width / sample).toLong() * (height / sample) > ART_DECODE_PIXELS) {
        sample *= 2
    }
    return sample
}

/**
 * Fetches with [HttpURLConnection], as Phobos's update and driver downloads do: [userAgent], 15 s to connect and 30 s
 * to read, and no redirect followed, so every request goes to [ART_SERVER] or, for label scans, the LaunchBox
 * host ([ArtKind.LABEL]). The host is resolved first through
 * [resolver], bounded, so a platform lookup that hangs can't hold a worker; a fetch over a second is logged once with
 * its phases. A picture is scaled down to its kind's width with Android's own decoder.
 */
internal class HttpArtFetcher(
    private val userAgent: String,
    private val log: (String) -> Unit = {},
    private val resolver: ArtHostResolver = BoundedHostResolver(log = log),
) : ArtFetcher {
    override fun listing(url: String): String =
        String(get(url, MAX_LISTING_BYTES, tooBigIsGone = false), Charsets.UTF_8)

    override fun picture(url: String, kind: ArtKind, file: File) {
        val bytes = get(url, MAX_PICTURE_BYTES, tooBigIsGone = true)
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        val sample = artDecodeSample(bounds.outWidth, bounds.outHeight, kind)
            ?: throw ArtGone("not an image, or ${bounds.outWidth} x ${bounds.outHeight}")
        val (decoded, scaled) = try {
            val options = BitmapFactory.Options().apply { inSampleSize = sample }
            val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options) ?: throw ArtGone("not an image")
            val fitted = if (bitmap.width > kind.width) {
                val height = (bitmap.height.toLong() * kind.width / bitmap.width).toInt().coerceAtLeast(1)
                Bitmap.createScaledBitmap(bitmap, kind.width, height, true)
            } else {
                bitmap
            }
            bitmap to fitted
        } catch (e: OutOfMemoryError) {
            throw ArtGone("too big to decode")
        }
        try {
            FileOutputStream(file).use { out ->
                val format = if (kind.png) Bitmap.CompressFormat.PNG else Bitmap.CompressFormat.JPEG
                if (!scaled.compress(format, if (kind.png) 100 else JPEG_QUALITY, out)) throw IOException("not written")
            }
        } finally {
            if (scaled !== decoded) scaled.recycle()
            decoded.recycle()
        }
    }

    override fun label(url: String, shape: String, file: File): LabelPicture.Format? =
        LabelPicture.write(get(url, MAX_PICTURE_BYTES, tooBigIsGone = true), shape, file)

    private fun get(url: String, maxBytes: Int, tooBigIsGone: Boolean): ByteArray {
        val started = System.nanoTime()
        var mark = started
        var lookupMs = -1L
        var connectMs = -1L
        var readMs = -1L
        val out = ByteArrayOutputStream()
        var conn: HttpURLConnection? = null
        try {
            resolver.resolve(URL(url).host)
            lookupMs = msSince(mark)
            mark = System.nanoTime()
            val c = (URL(url).openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                setRequestProperty("User-Agent", userAgent)
                instanceFollowRedirects = false
                connectTimeout = 15_000
                readTimeout = 30_000
            }
            conn = c
            val code = c.responseCode
            connectMs = msSince(mark)
            mark = System.nanoTime()
            if (code == HttpURLConnection.HTTP_NOT_FOUND || code == HttpURLConnection.HTTP_GONE) {
                throw ArtGone("HTTP $code")
            }
            if (code != HttpURLConnection.HTTP_OK) throw IOException("HTTP $code")
            val tooBig = { if (tooBigIsGone) ArtGone("over $maxBytes bytes") else IOException("over $maxBytes bytes") }
            if (c.contentLengthLong > maxBytes) throw tooBig()
            c.inputStream.use { input ->
                val buffer = ByteArray(16 * 1024)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    out.write(buffer, 0, read)
                    if (out.size() > maxBytes) throw tooBig()
                }
            }
            readMs = msSince(mark)
            return out.toByteArray()
        } finally {
            conn?.disconnect()
            // A phase that failed keeps its own time: the lookup, or the connect, or the read up to the failure.
            if (lookupMs < 0) lookupMs = msSince(mark)
            else if (connectMs < 0) connectMs = msSince(mark)
            else if (readMs < 0) readMs = msSince(mark)
            val total = msSince(started)
            if (total >= SLOW_FETCH_MS) {
                log("Artwork: $url took $total ms (lookup $lookupMs, connect $connectMs, read $readMs, ${out.size()} bytes)")
            }
        }
    }

    private fun msSince(nano: Long): Long = (System.nanoTime() - nano) / 1_000_000

    private companion object {
        const val MAX_PICTURE_BYTES = 4 * 1024 * 1024
        const val MAX_LISTING_BYTES = 16 * 1024 * 1024
        const val JPEG_QUALITY = 86
        const val SLOW_FETCH_MS = 1_000L
    }
}
