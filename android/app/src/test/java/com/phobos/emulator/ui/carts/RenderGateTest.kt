package com.phobos.emulator.ui.carts

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The render gate: no overlap, a detach leaves it open, only an engine close shuts it, and nothing draws after. */
class RenderGateTest {

    @Test
    fun twoConcurrentRendersNeverOverlap() {
        val gate = RenderGate()
        val inside = AtomicInteger()
        val maxInside = AtomicInteger()
        val start = CountDownLatch(1)
        val done = CountDownLatch(2)
        for (n in 1..2) {
            Thread {
                start.await()
                gate.render {
                    val now = inside.incrementAndGet()
                    maxInside.updateAndGet { maxOf(it, now) }
                    Thread.sleep(60)
                    inside.decrementAndGet()
                }
                done.countDown()
            }.start()
        }
        start.countDown()
        assertTrue("both renders finished", done.await(5, TimeUnit.SECONDS))
        assertEquals("renders were serialised", 1, maxInside.get())
    }

    @Test
    fun aRenderAfterCloseIsRefused() {
        val gate = RenderGate()
        gate.closeAndAwait()
        assertNull(gate.render { "drawn" })
        assertTrue(gate.isReleasing)
    }

    @Test
    fun awaitIdleLeavesTheGateOpenForTheNextThumbnailer() {
        // Two successive thumbnailers share one engine: the first detaching must not close the shared gate.
        val gate = RenderGate()
        assertEquals("first", gate.render { "first" })
        gate.awaitIdle()
        assertFalse("a detach does not close the gate", gate.isReleasing)
        assertEquals("second", gate.render { "second" })
    }

    @Test
    fun closeReturnsOnlyAfterTheRenderInFlight() {
        val gate = RenderGate()
        val entered = CountDownLatch(1)
        val renderFinished = AtomicBoolean(false)
        val render = Thread {
            gate.render {
                entered.countDown()
                Thread.sleep(250)
                renderFinished.set(true)
            }
        }
        render.start()
        assertTrue(entered.await(2, TimeUnit.SECONDS))
        gate.closeAndAwait()
        assertTrue("the barrier returned after the draw, not during it", renderFinished.get())
        assertTrue(gate.isReleasing)
    }

    @Test
    fun aQueuedRenderDoesNotDrawAfterCloseStarts() {
        val gate = RenderGate()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val drew = AtomicBoolean(false)
        val holder = Thread {
            gate.render {
                entered.countDown()
                release.await()
            }
        }
        holder.start()
        assertTrue(entered.await(2, TimeUnit.SECONDS))
        val queued = Thread {
            gate.render { drew.set(true) }
        }
        queued.start()
        Thread.sleep(50) // let the queued render ask for the lock first
        val closer = Thread { gate.closeAndAwait() }
        closer.start()
        val deadline = System.currentTimeMillis() + 2_000
        while (!gate.isReleasing && System.currentTimeMillis() < deadline) Thread.sleep(5)
        assertTrue("the close began", gate.isReleasing)
        release.countDown()
        holder.join(2_000)
        queued.join(2_000)
        closer.join(2_000)
        assertFalse("the queued render returned without drawing", drew.get())
    }

    @Test
    fun theThumbnailerOnlyTouchesTheLiveEngineInstance() {
        assertTrue(ThumbnailRelease.destroyAllowed(running = true, thumbnailerEngineId = 7, currentEngineId = 7))
        assertFalse(ThumbnailRelease.destroyAllowed(running = false, thumbnailerEngineId = 7, currentEngineId = 7))
        assertFalse(ThumbnailRelease.destroyAllowed(running = true, thumbnailerEngineId = 7, currentEngineId = 8))
        assertFalse(ThumbnailRelease.destroyAllowed(running = true, thumbnailerEngineId = 0, currentEngineId = 0))
    }

    @Test
    fun theCartEngineIsAllowedOnlyWithNoGameAndNoLoadStarting() {
        // D3-1's two windows, as a state table: a failed fresh load frees the carts; a paused game or a load in
        // flight blocks them even while _isLoaded is false.
        assertTrue(CartLifetime.engineAllowed(gameLoaded = false, loadsStarting = 0))
        assertFalse(CartLifetime.engineAllowed(gameLoaded = true, loadsStarting = 0))
        assertFalse(CartLifetime.engineAllowed(gameLoaded = false, loadsStarting = 1))
        assertFalse(CartLifetime.engineAllowed(gameLoaded = true, loadsStarting = 0))
    }

    @Test
    fun aRenderQueuedWhenDetachBeginsSeesTheClosedFlag() {
        // Mirrors CartThumbnailer.release(): the object publishes released=true BEFORE awaitIdle(), a queued render
        // waits for the gate, then reads the flag at its entry check and never touches the objects.
        val gate = RenderGate()
        val entered = CountDownLatch(1)
        val letGo = CountDownLatch(1)
        val detached = CountDownLatch(1)
        val released = AtomicBoolean(false)
        val drew = AtomicBoolean(false)
        val holder = Thread { gate.render { entered.countDown(); letGo.await() } }
        holder.start()
        assertTrue(entered.await(2, TimeUnit.SECONDS))
        val queued = Thread {
            gate.render {
                if (!released.get()) drew.set(true)
            }
        }
        queued.start()
        Thread.sleep(50)
        val flagSet = CountDownLatch(1)
        val detacher = Thread {
            released.set(true)
            flagSet.countDown()
            gate.awaitIdle()
            detached.countDown()
        }
        detacher.start()
        assertTrue("the flag was published", flagSet.await(2, TimeUnit.SECONDS))
        letGo.countDown()
        holder.join(2_000)
        queued.join(2_000)
        assertTrue("the detach finished", detached.await(2, TimeUnit.SECONDS))
        assertFalse("the queued render saw the closed flag and drew nothing", drew.get())
    }
}
