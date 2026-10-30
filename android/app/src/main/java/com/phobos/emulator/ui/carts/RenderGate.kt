package com.phobos.emulator.ui.carts

import java.util.IdentityHashMap
import java.util.concurrent.locks.ReentrantLock

/**
 * Serialises the offscreen cart renders and gives the engine's release a barrier.
 *
 * One render at a time: a shared [CartThumbnailer]'s Filament objects are not thread-safe and the grid's cards
 * call from Dispatchers.IO.
 *
 * Two different closes:
 *  - [awaitIdle] waits for the in-flight draw but leaves the gate open: a thumbnailer detaching must not stop the
 *    shared engine from rendering for the next screen.
 *  - [closeAndAwait] sets the releasing flag BEFORE taking the lock and then blocks until the last draw ends.
 *    Only [CartsEngine.release] calls it; nothing can start after release begins, and release never returns early.
 */
internal class RenderGate {

    private val lock = ReentrantLock()

    @Volatile private var releasing = false

    fun <T> render(block: () -> T): T? {
        lock.lock()
        try {
            if (releasing) return null
            return block()
        } finally {
            lock.unlock()
        }
    }

    /** Waits for the in-flight draw only; renders may continue afterwards. */
    fun awaitIdle() {
        lock.lock()
        lock.unlock()
    }

    /** From now on renders return null; returns only after the in-flight draw has finished. */
    fun closeAndAwait() {
        releasing = true
        lock.lock()
        lock.unlock()
    }

    val isReleasing: Boolean get() = releasing
}

/**
 * Whether a [CartThumbnailer] may still touch the engine in `release()`: only while the singleton it was built
 * against is the current one. A per-instance number is not enough - with a new engine alive the old number would
 * still "match" a comparison against itself, while the identity token proves the engine was replaced.
 */
internal object ThumbnailRelease {
    fun destroyAllowed(running: Boolean, thumbnailerEngineId: Long, currentEngineId: Long): Boolean =
        running && thumbnailerEngineId != 0L && thumbnailerEngineId == currentEngineId
}

/**
 * Whether the cart engine may exist: only while no game is loaded AND no load is starting. Derived from state the
 * app already keeps, so a failed fresh load (loaded false, starting 0) frees the carts again, and a slow replacement
 * load (starting > 0) keeps them blocked even though the old session is already closed.
 */
internal object CartLifetime {
    fun engineAllowed(gameLoaded: Boolean, loadsStarting: Int): Boolean = !gameLoaded && loadsStarting == 0
}

/** Every asset created through [CartsEngine.createAsset]; the engine releases them all, so nothing leaks or
 *  outlives its loader. Thread-safe: creation comes from the main thread and the thumbnailer's IO threads. */
internal class AssetRegistry<T> {
    private val items = IdentityHashMap<T, Boolean>()

    @Synchronized fun add(item: T) {
        items[item] = true
    }

    @Synchronized fun snapshot(): List<T> = items.keys.toList()

    @Synchronized fun clear() {
        items.clear()
    }

    @Synchronized fun size(): Int = items.size
}
