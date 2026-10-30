// Phobos's own: Cartridge has no lookup bound (electron/scraper.js lets the platform resolve the host with no
// deadline). A platform lookup can't be interrupted, so it's bounded here instead: the host is resolved on a small
// daemon pool with a deadline before HttpURLConnection opens. A lookup that misses the deadline is transient: the
// downloader stamps nothing and puts the job back in the queue about a minute later.
//
// The pre-resolution also warms the platform's DNS cache, so the connection's own lookup right after is normally a
// hit. On Android that cache is libcore's AddressCache, a fixed ~2 second TTL that ignores networkaddress.cache.ttl —
// the guarantee is ~2 s, not a minute: a gap >~2 s between the resolve and the connect (descheduling, Doze, a
// connectivity change) reopens the platform lookup, which the device read would show as a large connect with a small
// lookup.
package com.phobos.emulator.art

import java.io.IOException
import java.net.InetAddress
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * A host lookup that missed its deadline: transient, never a miss or a failure. A lookup that fails with the
 * platform's own error rethrows that [IOException] instead.
 */
internal class ArtLookupException(message: String) : IOException(message)

/** Resolves a URL's host before the connection is opened. */
internal fun interface ArtHostResolver {
    /** [host]'s addresses; throws [IOException] when it can't be had in time. */
    @Throws(IOException::class)
    fun resolve(host: String): Array<InetAddress>
}

/**
 * [ArtHostResolver] over [threads] daemon threads and [timeoutMs] per lookup. A lookup the platform's resolver
 * doesn't finish in time is abandoned (a blocking getaddrinfo can't be interrupted), reported as
 * [ArtLookupException], and the worker moves on: at most [threads] lookups end on their own, they hold no locks, and
 * a pool thread that's still stuck makes later lookups wait in the pool, where their own deadline still bounds them.
 */
internal class BoundedHostResolver(
    private val timeoutMs: Long = LOOKUP_TIMEOUT_MS,
    threads: Int = LOOKUP_THREADS,
    private val lookup: (String) -> Array<InetAddress> = InetAddress::getAllByName,
    private val log: (String) -> Unit = {},
) : ArtHostResolver {
    private val pool = Executors.newFixedThreadPool(threads) { runnable ->
        Thread(runnable, "art-lookup").apply { isDaemon = true }
    }

    override fun resolve(host: String): Array<InetAddress> {
        val task = pool.submit<Array<InetAddress>> { lookup(host) }
        return try {
            task.get(timeoutMs, TimeUnit.MILLISECONDS)
        } catch (e: TimeoutException) {
            task.cancel(true)
            log("Artwork: the lookup of $host didn't finish in $timeoutMs ms; it will be tried again shortly")
            throw ArtLookupException("lookup of $host timed out")
        } catch (e: ExecutionException) {
            val cause = e.cause
            throw if (cause is IOException) cause else IOException("lookup of $host failed: ${cause?.message}")
        }
    }
}

/** How long a host lookup may take: well under a minute, so a stall can't look like one. */
internal const val LOOKUP_TIMEOUT_MS = 5_000L

/** The pool's size: a listing and a picture at once, small enough to bound abandoned lookups. */
private const val LOOKUP_THREADS = 2

/** How long a job whose lookup timed out waits before it's queued again: one retry, a minute later. */
internal const val ART_LOOKUP_REQUEUE_MS = 60_000L
