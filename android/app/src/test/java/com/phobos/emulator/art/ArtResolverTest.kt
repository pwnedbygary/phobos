package com.phobos.emulator.art

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.net.InetAddress
import java.net.UnknownHostException
import java.util.concurrent.atomic.AtomicBoolean

/** The lookup bound: a hanging platform lookup is cut at the deadline and can't hold a caller. */
class ArtResolverTest {
    private val loopback = arrayOf(InetAddress.getLoopbackAddress())

    private inline fun <reified T : Throwable> expect(noinline block: () -> Unit): T {
        try {
            block()
        } catch (t: Throwable) {
            if (t is T) return t
            throw t
        }
        throw AssertionError("expected ${T::class.java.simpleName}")
    }

    @Test fun aLookupThatHangsIsCutAtTheBound() {
        val resolver = BoundedHostResolver(timeoutMs = 200, lookup = { Thread.sleep(60_000); loopback })
        val started = System.currentTimeMillis()
        val e = expect<ArtLookupException> { resolver.resolve("thumbnails.libretro.com") }
        val took = System.currentTimeMillis() - started
        assertTrue("cut near the 200 ms bound, took $took ms", took < 2_000)
        assertTrue(e.message!!.contains("timed out"))
    }

    @Test fun aLookupThatFinishesInTimeIsReturned() {
        val resolver = BoundedHostResolver(timeoutMs = 5_000, lookup = { loopback })
        assertEquals(loopback.toList(), resolver.resolve("thumbnails.libretro.com").toList())
    }

    @Test fun aLookupThatFailsIsAnIOException() {
        val resolver = BoundedHostResolver(timeoutMs = 5_000, lookup = { throw UnknownHostException("nope") })
        expect<IOException> { resolver.resolve("nope.invalid") }
    }

    @Test fun aLookupThatIgnoresInterruptsStillBoundsEachCall() {
        // A blocked getaddrinfo doesn't return on cancel(true): model one that refuses to die and check each caller is
        // still cut at the deadline. The thread it spends is the pool's, bounded at two in production.
        val stop = AtomicBoolean(false)
        val resolver = BoundedHostResolver(timeoutMs = 120, threads = 1, lookup = {
            while (!stop.get()) {
                try {
                    Thread.sleep(50)
                } catch (_: InterruptedException) {
                    // Ignored on purpose: a real lookup doesn't return when cancelled.
                }
            }
            loopback
        })
        val took = mutableListOf<Long>()
        repeat(2) {
            val started = System.nanoTime()
            expect<ArtLookupException> { resolver.resolve("thumbnails.libretro.com") }
            took += (System.nanoTime() - started) / 1_000_000
        }
        stop.set(true)
        assertTrue("both calls cut near the 120 ms bound, took $took ms", took.all { it < 2_000 })
    }
}
