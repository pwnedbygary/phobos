package com.phobos.emulator.art

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicInteger

/**
 * HttpArtFetcher's seam: the host is resolved before the connection is opened, a fetch over a second is one line with
 * its phases, and a lookup that times out logs both the resolver's bound and the failed lookup's phase.
 */
class ArtFetcherTest {
    private val loopback = arrayOf(InetAddress.getLoopbackAddress())

    private class FakeResolver(private val answer: () -> Array<InetAddress>) : ArtHostResolver {
        val calls = AtomicInteger()
        override fun resolve(host: String): Array<InetAddress> {
            calls.incrementAndGet()
            return answer()
        }
    }

    @Test fun theHostIsResolvedBeforeTheConnectionAndASlowFetchKeepsItsPhases() {
        val server = com.sun.net.httpserver.HttpServer.create(java.net.InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/page") { ex ->
            Thread.sleep(1_050)
            val body = "ok".toByteArray()
            ex.sendResponseHeaders(200, body.size.toLong())
            ex.responseBody.use { it.write(body) }
        }
        server.start()
        try {
            val lines = mutableListOf<String>()
            val resolver = FakeResolver { loopback }
            val fetcher = HttpArtFetcher("test", log = { lines += it }, resolver = resolver)
            assertEquals("ok", fetcher.listing("http://127.0.0.1:${server.address.port}/page"))
            assertEquals("the resolver was used; the connection alone wouldn't call it", 1, resolver.calls.get())
            assertTrue(
                "a fetch over a second is one line with all phases: $lines",
                lines.any { "took" in it && "lookup" in it && "connect" in it && "read" in it },
            )
        } finally {
            server.stop(0)
        }
    }

    @Test fun aLookupTimeoutLogsTheResolverBoundAndTheFailedLookupPhase() {
        val lines = mutableListOf<String>()
        val resolver = BoundedHostResolver(
            timeoutMs = 1_050,
            lookup = { Thread.sleep(5_000); loopback },
            log = { lines += it },
        )
        val fetcher = HttpArtFetcher("test", log = { lines += it }, resolver = resolver)
        try {
            fetcher.listing("http://thumbnails.libretro.com/page")
            throw AssertionError("expected a lookup timeout")
        } catch (_: ArtLookupException) {
            // expected: transient
        }
        assertTrue("the resolver's timeout is wired to a log line: $lines", lines.any { "didn't finish" in it })
        assertTrue(
            "a failed lookup keeps its phase in the over-a-second line: $lines",
            lines.any { "took" in it && "connect -1" in it && "read -1" in it },
        )
    }
}
