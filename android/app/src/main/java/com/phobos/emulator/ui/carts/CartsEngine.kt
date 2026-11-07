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
 *
 * Threading: Filament's JobSystem adopts exactly the thread that builds the engine (FEngine's constructor calls
 * JobSystem::adopt(), and the JobSystem has a single adoptable slot) and aborts in getState() with "This thread has
 * not been adopted." on any other thread that renders. [engineLooper] is that thread, and the offscreen thumbnailer
 * marshals its engine calls back to it.
 */
class CartsEngine private constructor(private val context: Context) {

    val engine: Engine = Engine.Builder().backend(Engine.Backend.OPENGL).build()
    val scene: Scene = engine.createScene()

    /** The Looper of the thread that built [engine]: the only thread Filament lets us render on ([get] runs on main). */
    internal val engineLooper: android.os.Looper? = android.os.Looper.myLooper()

    /** Every offscreen render runs under this gate; [release] closes it and waits for the in-flight draw. */
    internal val renderGate = RenderGate()

    /** Identity of this engine instance: a thumbnailer compares it with [currentEngineId] before touching us. */
    internal val id: Long = idSource.getAndIncrement()

    private val materials = UbershaderProvider(engine)
    val assets: AssetLoader = AssetLoader(engine, materials, com.google.android.filament.EntityManager.get())

    /**
     * gltfio defers the vertex data that needs the whole primitive to the resource loader: the glTF normals are
     * stored as unpopulated "tangent quats" and only [ResourceLoader.loadResources] fills that buffer (and any
     * textures). Without it every primitive is submitted with an unbound vertex buffer and draws nothing - the scene
     * and camera look right, the renderables are "visible", and no shader error is logged.
     */
    private val resources = com.google.android.filament.gltfio.ResourceLoader(engine)

    private val loaded = HashMap<String, LoadedCart>()

    /** Every asset created here, so release() destroys all of them (the loader alone does not). */
    private val createdAssets = AssetRegistry<FilamentAsset>()

    class LoadedCart(val key: String, val asset: FilamentAsset, val entities: IntArray)

    /** `carts/carts/<shape>.glb` for a cart, `carts/discs/<shape>.glb` for a disc; null when it cannot be loaded. */
    fun cart(shape: String): LoadedCart? = load("carts/$shape")

    /** The disc shell used for disc systems (minidisc carries PSP, as Cartridge has no UMD spec). */
    fun disc(shape: String): LoadedCart? = load("discs/$shape")

    private fun load(key: String): LoadedCart? {
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
    internal fun createAsset(buffer: ByteBuffer): FilamentAsset? = runCatching {
        assets.createAsset(buffer)?.also { asset ->
            // Register before the (fallible) resource load: a load that throws must not leak the asset.
            createdAssets.add(asset)
            resources.loadResources(asset)
        }
    }.getOrNull()

    /** Drops everything this engine owns. Callers must have detached (see the class note). */
    fun release() {
        // Close the gate and wait for the in-flight draw; never destroy with a render still inside the engine.
        renderGate.closeAndAwait()
        for (asset in createdAssets.snapshot()) {
            runCatching { assets.destroyAsset(asset) }
        }
        createdAssets.clear()
        loaded.clear()
        runCatching { resources.destroy() }
        runCatching { assets.destroy() }
        runCatching { materials.destroy() }
        runCatching { engine.destroyScene(scene) }
        runCatching { engine.destroy() }
    }

    companion object {
        /**
         * Filament before the first Engine: `Utils.init()` loads filament-jni, gltfio-jni and the utils JNI library
         * (utils depends on both), which the Java classes otherwise never load. Without it every Engine.Builder call
         * fails with "No implementation found" and the carts silently render nothing.
         */
        init { runCatching { com.google.android.filament.utils.Utils.init() } }

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
