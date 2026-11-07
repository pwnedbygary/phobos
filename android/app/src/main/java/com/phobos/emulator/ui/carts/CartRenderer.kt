package com.phobos.emulator.ui.carts

import android.content.Context
import android.view.Choreographer
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.google.android.filament.Camera
import com.google.android.filament.EntityManager
import com.google.android.filament.IndirectLight
import com.google.android.filament.LightManager
import com.google.android.filament.Renderer
import com.google.android.filament.Skybox
import com.google.android.filament.SwapChain
import com.google.android.filament.View
import com.google.android.filament.Texture
import com.google.android.filament.Viewport
import com.google.android.filament.utils.KTX1Loader
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The single live cart view: one SurfaceView, one Filament view, the focused game's shell turning slowly with the
 * studio environment (the 4a IBL and skybox). Cards and the grid use cached bitmaps; this surface is the only
 * per-frame work, and only while no game is loaded (see [CartsEngine]).
 */
class CartRenderer(
    private val context: Context,
    private val surfaceView: SurfaceView,
) : SurfaceHolder.Callback, Choreographer.FrameCallback {

    private var engineOwner: CartsEngine? = null
    private var engineId = 0L
    private var renderer: Renderer? = null
    private var view: View? = null
    private var camera: Camera? = null
    private var swapChain: SwapChain? = null
    private var shown: CartsEngine.LoadedCart? = null
    private val lookTextures = mutableListOf<Texture>()
    private var ibl: IndirectLight? = null
    private var iblCubemap: Texture? = null
    private var skybox: Skybox? = null
    private var skyboxCubemap: Texture? = null
    private var sun = 0
    private var running = false
    private var startNanos = 0L
    /** Camera framing of the shown shell, from its world bounds (see [CartFraming]). */
    private var fitDistance = 420.0
    /** The shown shell's world half-extents; the fit distance depends on the surface's aspect, so it is recomputed. */
    private var fitHalf = floatArrayOf(60f, 60f, 20f)
    private var centerX = 0.0
    private var centerY = 0.0
    private var centerZ = 0.0
    private var aspect = 1.0

    /** Starts the lifecycle; the surface itself drives creation and teardown. */
    fun attach() {
        surfaceView.holder.addCallback(this)
    }

    fun detach() {
        surfaceView.holder.removeCallback(this)
        stopLoop()
        destroyFilament()
    }

    /** Shows [shape] (a cart GLB name) or, when [disc] is set, a disc GLB name; null clears the view. */
    /** Shows [shape] with [look] and [wear], and the label picture when one is ready. */
    fun show(
        shape: String?,
        disc: Boolean = false,
        look: LookResolver.Look? = null,
        wear: Int = 0,
        label: android.graphics.Bitmap? = null,
    ) {
        val owner = engineOwner ?: return
        if (!CartsEngine.isRunning() || CartsEngine.currentEngineId() != engineId) {
            // The engine went between focus and load: never touch its scene or materials.
            dropReferences()
            return
        }
        shown?.let { owner.scene.removeEntities(it.entities) }
        shown = null
        if (shape == null) return
        val cart = if (disc) owner.disc(shape) else owner.cart(shape)
        if (cart != null) {
            owner.scene.addEntities(cart.entities)
            val previous = lookTextures.toList()
            lookTextures.clear()
            lookTextures += CartMaterials.apply(owner.engine, cart.entities, look, wear, label)
            // The new apply has set every map (the placeholder when there is no label), so the old textures are
            // no longer sampled and can go now instead of accumulating one set per focused cart.
            previous.forEach { runCatching { owner.engine.destroyTexture(it) } }
            shown = cart
            CartFraming.bounds(owner.engine, cart.entities)?.let { b ->
                fitHalf = floatArrayOf(b[3], b[4], b[5])
                fitDistance = CartFraming.distance(fitHalf, aspect)
                centerX = b[0].toDouble()
                centerY = b[1].toDouble()
                centerZ = b[2].toDouble()
                applyProjection()
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (CartsEngine.gameResident) return
        val owner = CartsEngine.get(context) ?: return
        engineOwner = owner
        engineId = owner.id
        val engine = owner.engine
        renderer = engine.createRenderer()
        view = engine.createView()
        camera = engine.createCamera(EntityManager.get().create())
        setupEnvironment(engine)
        view?.scene = owner.scene
        view?.camera = camera
        // The swap chain waits for surfaceChanged: a surface's size is only known there, and a chain built on the
        // zero-sized surface of surfaceCreated presents black frames.
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (!CartsEngine.isRunning() || CartsEngine.currentEngineId() != engineId) return
        aspect = width.toDouble() / height.coerceAtLeast(1)
        applyProjection()
        // Filament renders nothing (not even the clear) when the view's viewport is empty; it is not derived from the
        // swap chain, so the viewport is stated with every surface size.
        view?.viewport = Viewport(0, 0, width.coerceAtLeast(1), height.coerceAtLeast(1))
        val engine = engineOwner?.engine ?: return
        if (width <= 0 || height <= 0) return
        swapChain?.let { runCatching { engine.destroySwapChain(it) } }
        swapChain = runCatching { engine.createSwapChain(holder.surface) }.getOrNull()
        if (!running) {
            running = true
            startNanos = System.nanoTime()
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        stopLoop()
        destroyFilament()
    }

    override fun doFrame(frameTimeNanos: Long) {
        val owner = engineOwner ?: return
        if (!CartsEngine.isRunning() || CartsEngine.currentEngineId() != engineId) {
            stopLoop()
            dropReferences()
            return
        }
        val engine = owner.engine
        if (!running) return
        val r = renderer ?: return
        val v = view ?: return
        val sc = swapChain ?: return
        val t = (frameTimeNanos - startNanos) / 1_000_000_000.0
        spin(t)
        if (r.beginFrame(sc, frameTimeNanos)) {
            // The SurfaceView is opaque, so clear to the app's dark backdrop rather than a photo studio.
            r.clearOptions = Renderer.ClearOptions().apply {
                clear = true
                clearColor = doubleArrayOf(0.045, 0.04, 0.055, 1.0)
            }
            r.render(v)
            r.endFrame()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** The camera projection for the current surface aspect and the shown shell's framed distance. */
    private fun applyProjection() {
        fitDistance = CartFraming.distance(fitHalf, aspect)
        camera?.setProjection(
            CartFraming.FOV_DEGREES,
            aspect,
            CartFraming.near(fitDistance),
            CartFraming.far(fitDistance),
        )
    }

    /** The slow turn: the camera orbits the shell about the glTF up axis (Y) so its front and sides both read. */
    private fun spin(t: Double) {
        // Shared with the thumbnailer: one orbit definition, no second copy of the elevation/angle formula.
        val eye = CartFraming.orbitEye(floatArrayOf(centerX.toFloat(), centerY.toFloat(), centerZ.toFloat()), fitDistance, t * 0.5)
        camera?.lookAt(
            eye[0],
            eye[1],
            eye[2],
            centerX,
            centerY,
            centerZ,
            0.0,
            1.0,
            0.0,
        )
    }

    private fun setupEnvironment(engine: com.google.android.filament.Engine) {
        val owner = engineOwner ?: return
        ibl = runCatching {
            val bundle = KTX1Loader.createIndirectLight(engine, readAsset("carts/ibl/ibl_ibl.ktx"))
            iblCubemap = bundle.cubemap
            bundle.indirectLight
        }.getOrNull()
        skybox = runCatching {
            val bundle = KTX1Loader.createSkybox(engine, readAsset("carts/ibl/ibl_skybox.ktx"))
            skyboxCubemap = bundle.cubemap
            bundle.skybox
        }.getOrNull()
        owner.scene.indirectLight = ibl
        // No skybox in the live view either: the Library background must stay visible behind the shell.
        owner.scene.skybox = null

        sun = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.DIRECTIONAL)
            .color(1.0f, 0.98f, 0.94f)
            .intensity(60_000f)
            .direction(-0.4f, -0.8f, -0.45f)
            .castShadows(false)
            .build(engine, sun)
        owner.scene.addEntity(sun)
    }

    private fun readAsset(path: String): ByteBuffer {
        val bytes = context.assets.open(path).use { it.readBytes() }
        return ByteBuffer.allocateDirect(bytes.size).order(ByteOrder.nativeOrder()).apply {
            put(bytes)
            rewind()
        }
    }

    private fun stopLoop() {
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    private fun destroyFilament() {
        val owner = engineOwner
        val engine = owner?.engine
        if (owner != null && engine != null && CartsEngine.isRunning() && CartsEngine.currentEngineId() == engineId) {
            shown?.let { owner.scene.removeEntities(it.entities) }
            shown = null
            runCatching { swapChain?.let { engine.destroySwapChain(it) } }
            runCatching { renderer?.let { engine.destroyRenderer(it) } }
            runCatching { view?.let { engine.destroyView(it) } }
            runCatching { camera?.let { engine.destroyCameraComponent(it.entity) } }
            runCatching { ibl?.let { engine.destroyIndirectLight(it) } }
            runCatching { skybox?.let { engine.destroySkybox(it) } }
            runCatching { iblCubemap?.let { engine.destroyTexture(it) } }
            runCatching { skyboxCubemap?.let { engine.destroyTexture(it) } }
            lookTextures.forEach { runCatching { engine.destroyTexture(it) } }
            runCatching { EntityManager.get().destroy(sun) }
        }
        // The look textures are destroyed when the next show() replaces them (CartMaterials always sets a live map,
        // the placeholder when unlabelled); this path is the final teardown only.
        dropReferences()
    }

    /** Drops our references when the engine they belong to is already gone (or was recreated): nothing to destroy. */
    private fun dropReferences() {
        engineOwner = null
        engineId = 0L
        shown = null
        lookTextures.clear()
        swapChain = null
        renderer = null
        view = null
        camera = null
        ibl = null
        skybox = null
        iblCubemap = null
        skyboxCubemap = null
        sun = 0
    }
}
