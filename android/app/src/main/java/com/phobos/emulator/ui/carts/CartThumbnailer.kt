package com.phobos.emulator.ui.carts

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import com.google.android.filament.Camera
import com.google.android.filament.EntityManager
import com.google.android.filament.IndirectLight
import com.google.android.filament.LightManager
import com.google.android.filament.PixelBufferDescriptor
import com.google.android.filament.RenderTarget
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.Texture
import com.google.android.filament.View
import com.google.android.filament.Viewport
import com.google.android.filament.utils.KTX1Loader
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Offscreen cart thumbnails for the grid and the Library cards: the focused cart keeps the live surface, every other
 * card is one RGBA render to a texture plus one asynchronous readback, cached as a PNG by [CartBitmapCache].
 *
 * Call it from a background thread, and only ever block that thread: every Filament call is marshalled to the engine's
 * own thread ([CartsEngine.engineLooper]) because Filament's JobSystem adopts exactly the thread that built the engine
 * and aborts ("This thread has not been adopted.") on any other thread that renders. The readback wait and the pixel
 * copy stay on the calling thread, so the engine thread is never blocked. The thumbnailer keeps its own scene, view,
 * camera and asset instances: a Filament entity lives in one scene at a time, so the live surface's cart must not be
 * pulled out from under it. One render target per card size; everything is destroyed with the engine, and a readback
 * that outlives its render is waited for before `release()` destroys the target it still reads.
 */
class CartThumbnailer(
    private val context: Context,
    private val owner: CartsEngine,
) {

    private val engine = owner.engine
    private val engineId = owner.id
    /** The one thread Filament lets us render on: the thread that built the engine. */
    private val engineLooper = owner.engineLooper
    /** Readback callbacks land here, never on the main looper: the load path's barrier must not wait on main. */
    private val callbackThread = android.os.HandlerThread("cart-readback").apply { start() }
    private val callbackHandler = android.os.Handler(callbackThread.looper)
    private val renderer: Renderer = engine.createRenderer()
    private val scene: Scene = engine.createScene()
    private val view: View = engine.createView()
    private val cameraEntity = EntityManager.get().create()
    private val camera: Camera = engine.createCamera(cameraEntity)
    private val lightEntity = EntityManager.get().create()
    private val targets = HashMap<Long, Target>()
    private val loaded = HashMap<String, com.google.android.filament.gltfio.FilamentAsset>()
    /** Label/look textures currently applied to each asset's materials; replaced on the next apply, freed at release. */
    private val appliedTextures = HashMap<String, List<com.google.android.filament.Texture>>()
    private val buffers = HashMap<String, ByteBuffer>()
    private var indirectLight: IndirectLight? = null
    private var indirectTexture: com.google.android.filament.Texture? = null
    private var skybox: com.google.android.filament.Skybox? = null
    private var skyboxTexture: com.google.android.filament.Texture? = null
    @Volatile private var released = false

    /** Readbacks issued but not delivered yet: release() must not destroy a render target they still read. */
    private val readbackLock = java.util.concurrent.locks.ReentrantLock()
    private val readbacksDone = readbackLock.newCondition()
    private var pendingReadbacks = 0

    private class Target(val texture: Texture, val target: RenderTarget)

    /** One draw whose readback is still on its way to [buffer], with the descriptor that keeps that buffer alive. */
    private class Pending(
        val width: Int,
        val height: Int,
        val buffer: ByteBuffer,
        val descriptor: PixelBufferDescriptor,
        val readback: CountDownLatch,
    )

    init {
        view.scene = scene
        view.camera = camera
        view.antiAliasing = View.AntiAliasing.NONE
        view.setShadowingEnabled(false)
        LightManager.Builder(LightManager.Type.DIRECTIONAL)
            .direction(-0.35f, -0.6f, -0.5f)
            .color(1f, 1f, 1f)
            .intensity(60_000f)
            .castShadows(false)
            .build(engine, lightEntity)
        runCatching { scene.addEntity(lightEntity) }
        runCatching {
            val bytes = context.assets.open("carts/ibl/ibl_ibl.ktx").use { it.readBytes() }
            val buffer = ByteBuffer.allocateDirect(bytes.size).apply { put(bytes) }
            buffer.rewind()
            val bundle = KTX1Loader.createIndirectLight(engine, buffer)
            bundle.indirectLight?.let {
                it.intensity = 30_000f
                scene.indirectLight = it
                indirectLight = it
            }
            indirectTexture = bundle.cubemap
        }
        runCatching {
            val bytes = context.assets.open("carts/ibl/ibl_skybox.ktx").use { it.readBytes() }
            val buffer = ByteBuffer.allocateDirect(bytes.size).apply { put(bytes) }
            buffer.rewind()
            val bundle = KTX1Loader.createSkybox(engine, buffer)
            scene.skybox = bundle.skybox
            skybox = bundle.skybox
            skyboxTexture = bundle.cubemap
        }
    }

    private fun entities(shape: String, disc: Boolean): IntArray? {
        val key = (if (disc) "discs/" else "carts/") + shape
        loaded[key]?.let { return it.entities }
        val asset = runCatching {
            val bytes = context.assets.open("carts/$key.glb").use { it.readBytes() }
            val buffer = ByteBuffer.allocateDirect(bytes.size).apply { put(bytes) }
            buffer.rewind()
            owner.createAsset(buffer)?.also {
                it.releaseSourceData()
                buffers[key] = buffer
            }
        }.getOrNull() ?: return null
        loaded[key] = asset
        return asset.entities
    }

    private fun target(width: Int, height: Int): Target {
        val id = (width.toLong() shl 32) or height.toLong()
        targets[id]?.let { return it }
        val texture = Texture.Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.RGBA8)
            .usage(Texture.Usage.COLOR_ATTACHMENT or Texture.Usage.SAMPLEABLE or Texture.Usage.BLIT_SRC)
            .build(engine)
        val renderTarget = RenderTarget.Builder()
            .texture(RenderTarget.AttachmentPoint.COLOR0, texture)
            .build(engine)
        return Target(texture, renderTarget).also { targets[id] = it }
    }

    /**
     * Renders one shell into a bitmap; null when the shell cannot load, the engine was released, or the readback did
     * not arrive within the timeout. Blocking - call from a background thread, never from the engine's thread.
     */
    fun render(
        shape: String,
        disc: Boolean,
        width: Int,
        height: Int,
        look: LookResolver.Look? = null,
        wear: Int = 0,
        label: android.graphics.Bitmap? = null,
    ): Bitmap? {
        // The draw, the material update and the readback submission are Filament calls: they run on the engine's
        // thread. The wait for the readback and the pixel copy run here, on the caller's thread.
        val pending = onEngineThread {
            owner.renderGate.render { renderOnEngineThread(shape, disc, width, height, look, wear, label) }
        } ?: return null
        return finish(pending)
    }

    /** Runs [block] on the engine's thread and waits for it; inline when the caller is already there. */
    private fun onEngineThread(block: () -> Pending?): Pending? {
        val looper = engineLooper ?: return block()
        if (Looper.myLooper() == looper) return block()
        val result = AtomicReference<Pending?>()
        val failure = AtomicReference<Throwable?>()
        val done = CountDownLatch(1)
        Handler(looper).post {
            try {
                result.set(block())
            } catch (t: Throwable) {
                failure.set(t)
            } finally {
                done.countDown()
            }
        }
        if (!runCatching { done.await(5, TimeUnit.SECONDS) }.getOrDefault(false)) return null
        failure.get()?.let { throw it }
        return result.get()
    }

    /** The Filament half of one thumbnail: one draw and the readback submission. Runs on the engine's thread. */
    private fun renderOnEngineThread(
        shape: String,
        disc: Boolean,
        width: Int,
        height: Int,
        look: LookResolver.Look?,
        wear: Int,
        label: android.graphics.Bitmap?,
    ): Pending? {
        if (released || !CartsEngine.isRunning() || CartsEngine.currentEngineId() != engineId ||
            width <= 0 || height <= 0
        ) return null
        val ents = entities(shape, disc) ?: return null
        val t = target(width, height)
        camera.setProjection(45.0, width.toDouble() / height.toDouble(), 0.1, 1000.0, Camera.Fov.VERTICAL)
        camera.lookAt(0.0, 60.0, 420.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
        view.setRenderTarget(t.target)
        // Filament renders nothing (not even the clear) when the view's viewport is empty; it is not derived from the
        // render target, so every draw states it.
        view.viewport = Viewport(0, 0, width, height)

        for (e in ents) runCatching { scene.addEntity(e) }
        val lookTextures = CartMaterials.apply(engine, ents, look, wear, label)
        // Offscreen: this is the beginFrame/endFrame-less entry point that flushes the driver, so the readback below
        // is actually submitted (render() expects an open beginFrame and would drop it).
        renderer.renderStandaloneView(view)
        val buffer = ByteBuffer.allocateDirect(width * height * 4).order(ByteOrder.nativeOrder())
        val latch = CountDownLatch(1)
        val descriptor = PixelBufferDescriptor(buffer, Texture.Format.RGBA, Texture.Type.UBYTE)
        descriptor.setCallback(
            callbackHandler,
            Runnable { readbackArrived(latch) },
        )
        readbackIssued()
        runCatching { renderer.readPixels(t.target, 0, 0, width, height, descriptor) }
        // The GL driver hands a readback back from a GPU fence, and Filament runs those fence callbacks only in
        // driver.tick(), which an engine with no live surface never gets. flushAndWait() finishes the GPU work and
        // runs them, so a thumbnail lands without a render loop (the callback still posts to the readback thread).
        runCatching { engine.flushAndWait() }
        for (e in ents) runCatching { scene.remove(e) }
        // Keep this apply's textures on the asset: the next apply replaces them (CartMaterials always sets a live
        // label map, the placeholder when there is no scan), so nothing is destroyed while a material samples it.
        val assetKey = (if (disc) "discs/" else "carts/") + shape
        appliedTextures[assetKey]?.forEach { runCatching { engine.destroyTexture(it) } }
        appliedTextures[assetKey] = lookTextures
        return Pending(width, height, buffer, descriptor, latch)
    }

    /** The caller-thread half: wait for the readback and turn its RGBA bytes into a bitmap. */
    private fun finish(p: Pending): Bitmap? {
        val done = runCatching { p.readback.await(5, TimeUnit.SECONDS) }.getOrDefault(false)
        if (!done || released) return null
        val buffer = p.buffer
        buffer.rewind()
        val pixels = IntArray(p.width * p.height)
        for (i in pixels.indices) {
            val r = buffer.get(i * 4).toInt() and 0xFF
            val g = buffer.get(i * 4 + 1).toInt() and 0xFF
            val b = buffer.get(i * 4 + 2).toInt() and 0xFF
            val a = buffer.get(i * 4 + 3).toInt() and 0xFF
            pixels[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        val bitmap = Bitmap.createBitmap(p.width, p.height, Bitmap.Config.ARGB_8888)
        bitmap.setPixels(pixels, 0, p.width, 0, 0, p.width, p.height)
        return bitmap
    }

    private fun readbackIssued() {
        readbackLock.lock()
        try {
            pendingReadbacks++
        } finally {
            readbackLock.unlock()
        }
    }

    /** Runs on the callback thread: count this render down and let a waiting release() know. */
    private fun readbackArrived(latch: CountDownLatch) {
        latch.countDown()
        readbackLock.lock()
        try {
            pendingReadbacks--
            readbacksDone.signalAll()
        } finally {
            readbackLock.unlock()
        }
    }

    /** Bounded: a readback is a GPU round trip, so this is normally a few milliseconds. */
    private fun awaitPendingReadbacks() {
        readbackLock.lock()
        try {
            var remaining = TimeUnit.SECONDS.toNanos(5)
            while (pendingReadbacks > 0 && remaining > 0) {
                remaining = readbacksDone.awaitNanos(remaining)
            }
        } finally {
            readbackLock.unlock()
        }
    }

    /** Destroys the thumbnail pipeline's engine resources; the cache keeps the PNGs. The shared engine stays. */
    fun release() {
        // Publish the closed state BEFORE waiting: a render already queued then sees it at the entry check and
        // never touches the objects this method destroys.
        released = true
        val alive = ThumbnailRelease.destroyAllowed(
            running = CartsEngine.isRunning(),
            thumbnailerEngineId = engineId,
            currentEngineId = CartsEngine.currentEngineId(),
        )
        owner.renderGate.awaitIdle()
        // A readback already issued still reads its render target below: let it land before destroying that
        // target. Its callback comes on the readback thread, so the engine's thread is not waited on here.
        awaitPendingReadbacks()
        // The callback thread belongs to this object, not the engine: quit it on every path.
        runCatching { callbackThread.quitSafely() }
        if (!alive) {
            // The engine went first (releaseAll): its resources died with it; just drop our references.
            targets.clear()
            appliedTextures.clear()
            loaded.clear()
            buffers.clear()
            return
        }
        for (t in targets.values) {
            runCatching { engine.destroyRenderTarget(t.target) }
            runCatching { engine.destroyTexture(t.texture) }
        }
        targets.clear()
        for (list in appliedTextures.values) list.forEach { runCatching { engine.destroyTexture(it) } }
        appliedTextures.clear()
        runCatching { scene.setIndirectLight(null) }
        runCatching { scene.setSkybox(null) }
        indirectLight?.let { runCatching { engine.destroyIndirectLight(it) } }
        skybox?.let { runCatching { engine.destroySkybox(it) } }
        indirectTexture?.let { runCatching { engine.destroyTexture(it) } }
        skyboxTexture?.let { runCatching { engine.destroyTexture(it) } }
        runCatching { scene.remove(lightEntity) }
        runCatching { engine.destroyEntity(lightEntity) }
        runCatching {
            engine.destroyCameraComponent(cameraEntity)
            engine.destroyEntity(cameraEntity)
        }
        runCatching { engine.destroyView(view) }
        runCatching { engine.destroyScene(scene) }
        runCatching { engine.destroyRenderer(renderer) }
        buffers.clear()
        loaded.clear()
    }
}
