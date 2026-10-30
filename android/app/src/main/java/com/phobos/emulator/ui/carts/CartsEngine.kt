package com.phobos.emulator.ui.carts

import android.content.Context
import com.google.android.filament.Engine
import com.google.android.filament.Scene
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.UbershaderProvider
import java.nio.ByteBuffer

/**
 * The one Filament engine for the carts, owned here and nowhere else.
 *
 * Lifetime contract: the engine exists only while no game is loaded. [releaseAll] runs before every game load and
 * on every unload (on the main thread); [get] builds it again lazily when the cart UI next shows with no game
 * resident. Every user (the live renderer, the offscreen thumbnailer) checks [isRunning] before touching the engine
 * and detaches when it turns false, so nothing can call into a released engine.
 *
 * OpenGL ES backend only: Phobos has no other EGL user, and Vulkan would share the process with the emulator's
 * adrenotools driver.
 */
class CartsEngine private constructor(private val context: Context) {

    val engine: Engine = Engine.Builder().backend(Engine.Backend.OPENGL).build()
    val scene: Scene = engine.createScene()

    /** Every offscreen render runs under this gate; [release] closes it and waits for the in-flight draw. */
    internal val renderGate = RenderGate()

    /** Identity of this engine instance: a thumbnailer compares it with [currentEngineId] before touching us. */
    internal val id: Long = idSource.getAndIncrement()

    private val materials = UbershaderProvider(engine)
    val assets: AssetLoader = AssetLoader(engine, materials, com.google.android.filament.EntityManager.get())

    private val loaded = HashMap<String, LoadedCart>()

    /** Every asset created here, so release() destroys all of them (the loader alone does not). */
    private val createdAssets = AssetRegistry<FilamentAsset>()

    class LoadedCart(val key: String, val asset: FilamentAsset, val entities: IntArray)

    /** `carts/<shape>.glb` (or `discs/<shape>.glb`) from the app's assets; null when it cannot be loaded. */
    fun cart(key: String): LoadedCart? {
        loaded[key]?.let { return it }
        val asset = try {
            val bytes = context.assets.open("carts/$key.glb").use { it.readBytes() }
            val buffer = ByteBuffer.allocateDirect(bytes.size).apply { put(bytes) }
            buffer.rewind()
            createAsset(buffer)?.also { it.releaseSourceData() }
        } catch (e: Exception) {
            null
        } ?: return null
        return LoadedCart(key, asset, asset.entities).also { loaded[key] = it }
    }

    /**
     * Asset creation behind one lock: Filament's AssetLoader is not thread-safe, and the grid's thumbnailer
     * (background thread) and the live view (main thread) both load shells. Every asset is registered for release.
     */
    @Synchronized
    internal fun createAsset(buffer: ByteBuffer): FilamentAsset? = runCatching { assets.createAsset(buffer) }
        .getOrNull()
        ?.also { createdAssets.add(it) }

    /** The disc shell used for disc systems (minidisc carries PSP, as Cartridge has no UMD spec). */
    fun disc(shape: String): LoadedCart? = cart("discs/$shape")

    /** Drops everything this engine owns. Callers must have detached (see the class note). */
    fun release() {
        // Close the gate and wait for the in-flight draw; never destroy with a render still inside the engine.
        renderGate.closeAndAwait()
        for (asset in createdAssets.snapshot()) {
            runCatching { assets.destroyAsset(asset) }
        }
        createdAssets.clear()
        loaded.clear()
        runCatching { assets.destroy() }
        runCatching { materials.destroy() }
        runCatching { engine.destroyScene(scene) }
        runCatching { engine.destroy() }
    }

    companion object {
        @Volatile private var instance: CartsEngine? = null
        private val idSource = java.util.concurrent.atomic.AtomicLong(1)

        /** True while a game is resident (running or paused): no engine may be created then. */
        @Volatile internal var gameResident = false

        /** The engine, built on first use; null when Filament cannot start or a game is resident. */
        fun get(context: Context): CartsEngine? {
            if (gameResident) return null
            return instance ?: synchronized(this) {
                if (gameResident) return null
                instance ?: runCatching { CartsEngine(context.applicationContext).also { instance = it } }.getOrNull()
            }
        }

        fun isRunning(): Boolean = instance != null

        /** The identity of the live engine, 0 when none: a stale thumbnailer must not touch a replaced engine. */
        internal fun currentEngineId(): Long = instance?.id ?: 0L

        /** The lifetime's off switch: released before every game load and on every unload, on the main thread. */
        fun releaseAll() {
            val owner = synchronized(this) {
                val old = instance
                instance = null
                old
            } ?: return
            runCatching { owner.release() }
        }
    }
}
