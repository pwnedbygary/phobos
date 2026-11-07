package com.phobos.emulator.ui.diorama

import android.content.Context
import android.graphics.Bitmap
import android.graphics.SurfaceTexture
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import android.view.Surface
import android.view.TextureView
import android.view.ViewTreeObserver
import com.google.android.filament.Camera
import com.google.android.filament.Engine
import com.google.android.filament.EntityManager
import com.google.android.filament.IndirectLight
import com.google.android.filament.LightManager
import com.google.android.filament.MaterialInstance
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.SwapChain
import com.google.android.filament.Texture
import com.google.android.filament.TextureSampler
import com.google.android.filament.View
import com.google.android.filament.Viewport
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.MaterialProvider
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.UbershaderProvider
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.cos
import kotlin.math.sin

/**
 * The system page's rotating console hero: one [TextureView], one private Filament engine, one asset kept resident
 * for the life of the screen.
 *
 * Display path: a **TextureView**, deliberately not a SurfaceView. A SurfaceView is a separate window layer; with
 * `setZOrderOnTop(true)` it is composited above the app window, so it cannot be clipped to its band, the system
 * page's header cannot cover it while the page scrolls, and the screensaver overlay cannot cover it either (all
 * seen on the RP6). A TextureView is composited inside the view hierarchy: it clips to the band, scrolls with the
 * page and is covered by chrome/overlays, while `isOpaque = false` plus the transparent swap chain keeps the
 * "floats on the page backdrop" look. Do not "simplify" this back to a SurfaceView.
 *
 * Everything Filament-related runs on this renderer's own [HandlerThread], never on the UI thread: the engine is
 * built there (Filament's JobSystem adopts exactly the thread that builds the engine and aborts if another thread
 * renders), the asset is loaded and its texture uploaded there, and the frame loop is posted there. The UI thread
 * only hands over TextureView callbacks, drag deltas and the decoded screen bitmap.
 *
 * Lifetime: the engine lives while the hero is on screen and is torn down on [detach] (the system page leaving) or
 * on [releaseNow] (before a game loads, the same release-before-game-load contract the carts engine follows).
 * A failure anywhere marks the hero unavailable instead of showing a broken band; the system page then keeps its
 * cards only.
 */
internal class DioramaRenderer(
    context: Context,
    private val systemName: String,
    private val assetPath: String,
    private val onUnavailable: () -> Unit,
) {

    private val appContext = context.applicationContext
    private val mainHandler = Handler(Looper.getMainLooper())
    private val unavailable = AtomicBoolean(false)
    private val thread = HandlerThread("diorama-render")
    private var handler: Handler? = null
    private var textureView: TextureView? = null
    private var mainTexture: SurfaceTexture? = null

    @Volatile private var released = false

    /** Updated on the UI thread from a pre-draw pass; the frame loop skips engine work while the hero is hidden. */
    @Volatile private var viewVisible = true

    // ---- Render-thread state: every field below is touched only on `thread`. ----

    private var engine: Engine? = null
    private var scene: Scene? = null
    private var renderer: Renderer? = null
    private var view: View? = null
    private var camera: Camera? = null
    private var cameraEntity = 0
    private var indirectLight: IndirectLight? = null
    private val lightEntities = mutableListOf<Int>()
    private var assetLoader: AssetLoader? = null
    private var resources: ResourceLoader? = null
    private var materials: UbershaderProvider? = null
    private var asset: FilamentAsset? = null
    private var screenMaterial: MaterialInstance? = null
    private val screenTextures = mutableListOf<Texture>()
    private val screenBuffers = mutableListOf<ByteBuffer>()

    private var swapChain: SwapChain? = null
    private var surface: Surface? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var viewportWidth = 0
    private var viewportHeight = 0
    private var aspect = 4.0 / 3.0
    private var fitDistance = 1.0
    private var halfExtents = floatArrayOf(1f, 1f, 1f)
    private var center = floatArrayOf(0f, 0f, 0f)
    private var rootTransformInstance = 0
    private var frameScheduled = false
    private var lastFrameNanos = 0L
    private var failed = false
    private var pendingArt: Bitmap? = null
    private val turntable = DioramaTurntable()

    /** Starts the render thread and attaches to [textureView]; call once from the UI thread. */
    fun attach(textureView: TextureView) {
        if (handler != null || released) return
        this.textureView = textureView
        // A TextureView can be transparent; the band therefore shows the page's own backdrop behind the model.
        textureView.isOpaque = false
        textureView.viewTreeObserver.addOnPreDrawListener(preDrawListener)
        thread.start()
        val renderHandler = Handler(thread.looper)
        handler = renderHandler
        renderHandler.post(::createEngine)
        textureView.surfaceTextureListener = surfaceTextureListener
    }

    /** Cheap visibility gate: the pre-draw pass runs on the UI thread and keeps the frame loop from drawing offscreen. */
    private val preDrawListener = ViewTreeObserver.OnPreDrawListener {
        viewVisible = textureView?.isShown == true
        true
    }

    /** Hands the decoded 4:3 screen picture to the render thread; safe to call from the UI thread. */
    fun setScreenArt(bitmap: Bitmap) {
        if (released) return
        val renderHandler = handler
        if (renderHandler == null) {
            pendingArt = bitmap
            return
        }
        renderHandler.post { applyScreenArt(bitmap) }
    }

    /** A drag started: the finger owns the pose until [dragEnd]. UI thread. */
    fun dragStart() {
        post { turntable.dragStart() }
    }

    /** One horizontal drag delta in pixels. UI thread. */
    fun drag(dxPixels: Float) {
        post {
            val now = System.nanoTime()
            val dt = if (lastDragNanos == 0L) 1.0 / 60.0 else ((now - lastDragNanos) / 1_000_000_000.0).coerceAtMost(0.25)
            lastDragNanos = now
            turntable.drag(dxPixels, dt)
        }
    }

    /** The finger lifted: the last drag's velocity carries on and decays. UI thread. */
    fun dragEnd() {
        post {
            lastDragNanos = 0L
            turntable.dragEnd()
        }
    }

    private var lastDragNanos = 0L

    /** Stops and destroys the engine after the render thread has drained; UI thread. */
    fun detach() {
        releaseInternal(wait = false)
    }

    /** Destroys the engine synchronously (bounded) before a game load; UI thread. */
    fun releaseNow() {
        releaseInternal(wait = true)
    }

    private fun releaseInternal(wait: Boolean) {
        if (released) return
        released = true
        runCatching { textureView?.viewTreeObserver?.removeOnPreDrawListener(preDrawListener) }
        runCatching { textureView?.surfaceTextureListener = null }
        textureView = null
        mainTexture = null
        val renderHandler = handler
        if (renderHandler == null) {
            if (thread.isAlive) runCatching { thread.quitSafely() }
            return
        }
        val latch = CountDownLatch(1)
        renderHandler.post {
            destroyAll()
            latch.countDown()
        }
        runCatching { thread.quitSafely() }
        if (wait) runCatching { latch.await(3, TimeUnit.SECONDS) }
    }

    private fun post(block: () -> Unit) {
        if (released) return
        val renderHandler = handler ?: return
        runCatching { renderHandler.post(block) }
    }

    private val surfaceTextureListener = object : TextureView.SurfaceTextureListener {
        override fun onSurfaceTextureAvailable(st: SurfaceTexture, width: Int, height: Int) {
            mainTexture = st
            runCatching { st.setDefaultBufferSize(width.coerceAtLeast(1), height.coerceAtLeast(1)) }
            val s = Surface(st)
            post { onDisplayAvailable(s, st, width, height) }
        }

        override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, width: Int, height: Int) {
            runCatching { st.setDefaultBufferSize(width.coerceAtLeast(1), height.coerceAtLeast(1)) }
            post { onDisplaySizeChanged(width, height) }
        }

        override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean {
            if (mainTexture === st) mainTexture = null
            // The render thread destroys the swap chain and then releases the Surface/SurfaceTexture; returning
            // false says we own that release, so the TextureView does not free it while a frame may still use it.
            post { onDisplayDestroyed(st) }
            return false
        }

        override fun onSurfaceTextureUpdated(st: SurfaceTexture) = Unit
    }

    // ---- Render-thread implementation ----

    private fun createEngine() {
        if (released) return
        try {
            com.google.android.filament.utils.Utils.init()
            val e = Engine.Builder().backend(Engine.Backend.OPENGL).build()
            engine = e
            val sc = e.createScene()
            scene = sc
            renderer = e.createRenderer().also { r ->
                r.clearOptions = Renderer.ClearOptions().apply {
                    clear = true
                    setClearColor(0.0, 0.0, 0.0, 0.0)
                }
            }
            val v = e.createView().apply {
                this.scene = sc
                // The hero floats over the app background: the view, like the swap chain, is translucent.
                blendMode = View.BlendMode.TRANSLUCENT
                antiAliasing = View.AntiAliasing.NONE
                isShadowingEnabled = false
            }
            view = v
            cameraEntity = EntityManager.get().create()
            camera = e.createCamera(cameraEntity)
            v.camera = camera
            setupEnvironment(e, sc)
            loadAsset(e, sc, v)
        } catch (t: Throwable) {
            fail("engine", t)
        }
    }

    private fun setupEnvironment(e: Engine, sc: Scene) {
        // Neutral ambient light only: no photo-studio skybox or IBL cubemap. Three directional lights give the
        // chassis shape from the front, the left and behind; the CRT's screen is emissive (see applyScreenArt).
        val sh = FloatArray(27)
        sh[0] = 0.55f
        sh[1] = 0.55f
        sh[2] = 0.58f
        indirectLight = IndirectLight.Builder()
            .irradiance(3, sh)
            .intensity(22_000f)
            .build(e)
        sc.indirectLight = indirectLight

        addLight(e, -0.30f, -0.75f, -0.58f, 1.0f, 0.97f, 0.93f, 75_000f) // key, upper front
        addLight(e, 0.65f, -0.45f, 0.50f, 0.76f, 0.82f, 1.0f, 26_000f) // cool fill, back left
        addLight(e, 0.10f, -0.20f, 0.90f, 1.0f, 0.92f, 0.85f, 18_000f) // warm rim from behind
    }

    private fun addLight(
        e: Engine,
        dx: Float,
        dy: Float,
        dz: Float,
        r: Float,
        g: Float,
        b: Float,
        intensity: Float,
    ) {
        val entity = EntityManager.get().create()
        runCatching {
            LightManager.Builder(LightManager.Type.DIRECTIONAL)
                .direction(dx, dy, dz)
                .color(r, g, b)
                .intensity(intensity)
                .castShadows(false)
                .build(e, entity)
            scene?.addEntity(entity)
            lightEntities += entity
        }.onFailure {
            runCatching { EntityManager.get().destroy(entity) }
        }
    }

    private fun loadAsset(e: Engine, sc: Scene, v: View) {
        val bytes = appContext.assets.open(assetPath).use { it.readBytes() }
        val buffer = ByteBuffer.allocateDirect(bytes.size).apply {
            put(bytes)
            rewind()
        }
        val provider = UbershaderProvider(e)
        materials = provider
        val loader = AssetLoader(e, provider, EntityManager.get())
        assetLoader = loader
        resources = ResourceLoader(e)
        val loaded = loader.createAsset(buffer) ?: throw IllegalStateException("gltfio could not parse $assetPath")
        asset = loaded
        // glTF normals arrive as tangent quats and textures are deferred; without loadResources nothing draws.
        resources?.loadResources(loaded)
        loaded.releaseSourceData()
        sc.addEntities(loaded.entities)

        val box = loaded.boundingBox
        center = box.center
        halfExtents = box.halfExtent
        applyProjection()
        rootTransformInstance = runCatching { e.transformManager.getInstance(loaded.root) }.getOrDefault(0)
        screenMaterial = installScreenMaterial(e, loaded)
        if (screenMaterial == null) {
            Log.w(TAG, "$systemName: no tv_screen material; the CRT stays dark")
        }
        pendingArt?.let {
            pendingArt = null
            applyScreenArt(it)
        }
        scheduleFrame()
    }

    /**
     * The baked CRT screen material has no glTF base-colour texture, so gltfio's material variant for it does not
     * sample `baseColorMap` at all: setting a map on that instance is a silent no-op (the screen just shows the
     * factor). Ask the ubershader provider for an instance whose variant *has* the base-colour and emissive maps
     * (the same instance gltfio would build if the GLB had a screen texture) and bind it to the tv_screen
     * primitive. This keeps the art out of the GLB and leaves every other material untouched.
     */
    private fun installScreenMaterial(e: Engine, loaded: FilamentAsset): MaterialInstance? = runCatching {
        val entity = loaded.getFirstEntityByName("tv_screen")
        if (entity == 0) return@runCatching null
        val rm = e.renderableManager
        if (!rm.hasComponent(entity)) return@runCatching null
        val provider = materials ?: return@runCatching null
        val key = MaterialProvider.MaterialKey().apply {
            doubleSided = false
            unlit = false
            hasVertexColors = false
            hasBaseColorTexture = true
            baseColorUV = 0
            hasEmissiveTexture = true
            emissiveUV = 0
            alphaMode = 0 // OPAQUE
        }
        // glTF UV set -> material UV set: set 0 (TEXCOORD_0) maps to material UV0.
        val uvMap = intArrayOf(1, 0, 0, 0, 0, 0, 0, 0)
        val instance = provider.createMaterialInstance(key, uvMap, "screen_art", null) ?: return@runCatching null
        rm.setMaterialInstanceAt(rm.getInstance(entity), 0, instance)
        instance
    }.getOrNull()

    private fun applyProjection() {
        val d = DioramaFraming.distance(center, halfExtents, aspect)
        fitDistance = d
        camera?.setProjection(
            DioramaFraming.FOV_DEGREES,
            aspect,
            DioramaFraming.near(d),
            DioramaFraming.far(d),
        )
        lookAtModel()
    }

    /** The fixed camera: in front of the screen (+Z), slightly above the box centre, looking down at it. */
    private fun lookAtModel() {
        val eye = DioramaFraming.eye(center, fitDistance)
        camera?.lookAt(
            eye[0],
            eye[1],
            eye[2],
            center[0].toDouble(),
            center[1].toDouble(),
            center[2].toDouble(),
            0.0,
            1.0,
            0.0,
        )
    }

    private fun applyScreenArt(bitmap: Bitmap) {
        if (released || failed) return
        val e = engine ?: return
        val mi = screenMaterial
        if (mi == null) {
            // No material instance means the model (or its screen primitive) is not usable: the hero still shows,
            // it simply has no picture.
            return
        }
        val texture = runCatching { uploadTexture(e, bitmap) }.getOrNull() ?: run {
            fail("screen texture", null)
            return
        }
        val sampler = TextureSampler(
            TextureSampler.MinFilter.LINEAR,
            TextureSampler.MagFilter.LINEAR,
            TextureSampler.WrapMode.CLAMP_TO_EDGE,
        )
        // Filament's Java getMaterialInstanceAt() wraps the native instance without a Material reference (gltfio
        // assets), so Material.hasParameter() is unavailable here; the ubershader material always declares the glTF
        // parameters, and an unsupported name is a native no-op rather than a crash.
        Log.i(TAG, "$systemName: screen art ${bitmap.width}x${bitmap.height} on '${mi.name}'")
        runCatching {
            mi.setParameter("baseColorFactor", 1f, 1f, 1f, 1f)
            mi.setParameter("baseColorMap", texture, sampler)
            // The CRT is a light source in the diorama: emit the same picture so it reads as a switched-on screen.
            mi.setParameter("emissiveFactor", 1f, 1f, 1f)
            mi.setParameter("emissiveMap", texture, sampler)
            mi.setParameter("metallicFactor", 0f)
            mi.setParameter("roughnessFactor", 0.9f)
            // Queue the one-time texture upload; the next frame samples it. flush() (not flushAndWait) avoids
            // Filament's EGL frame-timestamp query on a TextureView surface, which logs EGL_BAD_ACCESS.
            e.flush()
        }.onFailure { fail("screen material", it) }
    }

    private fun uploadTexture(e: Engine, bitmap: Bitmap): Texture {
        val width = bitmap.width
        val height = bitmap.height
        val texture = Texture.Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.RGBA8)
            .usage(Texture.Usage.SAMPLEABLE or Texture.Usage.UPLOADABLE)
            .build(e)
        val buffer = ByteBuffer.allocateDirect(width * height * 4).order(ByteOrder.nativeOrder())
        bitmap.copyPixelsToBuffer(buffer)
        buffer.rewind()
        // Filament copies the pixels asynchronously; the descriptors keep the direct buffer alive until the upload
        // completes, and both are held until teardown so an in-flight upload can never read freed memory.
        texture.setImage(e, 0, Texture.PixelBufferDescriptor(buffer, Texture.Format.RGBA, Texture.Type.UBYTE))
        screenTextures += texture
        screenBuffers += buffer
        return texture
    }

    private fun onDisplayAvailable(surface: Surface, texture: SurfaceTexture, width: Int, height: Int) {
        if (released || failed) return
        this.surface = surface
        this.surfaceTexture = texture
        configureDisplay(width, height)
    }

    private fun onDisplaySizeChanged(width: Int, height: Int) {
        if (released || failed) return
        configureDisplay(width, height)
    }

    private fun onDisplayDestroyed(texture: SurfaceTexture) {
        stopLoop()
        swapChain?.let { runCatching { engine?.destroySwapChain(it) } }
        swapChain = null
        surface?.release()
        surface = null
        if (surfaceTexture === texture) {
            runCatching { texture.release() }
            surfaceTexture = null
        }
    }

    private fun configureDisplay(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        viewportWidth = width
        viewportHeight = height
        aspect = width.toDouble() / height.toDouble()
        // Filament renders nothing (not even the clear) when the view's viewport is empty; it is not derived from
        // the swap chain, so it is set with every surface size.
        view?.viewport = Viewport(0, 0, width, height)
        applyProjection()
        swapChain?.let { runCatching { engine?.destroySwapChain(it) } }
        swapChain = null
        createSwapChain()
        scheduleFrame()
    }

    private fun createSwapChain() {
        val e = engine ?: return
        val s = surface ?: return
        if (swapChain != null || viewportWidth <= 0 || viewportHeight <= 0) return
        swapChain = runCatching {
            // A transparent chain is what makes the clear's alpha 0 show the app background through the surface.
            e.createSwapChain(s, SwapChain.CONFIG_TRANSPARENT)
        }.getOrElse {
            Log.w(TAG, "$systemName: transparent swap chain unavailable, using an opaque dark clear", it)
            view?.blendMode = View.BlendMode.OPAQUE
            renderer?.clearOptions = Renderer.ClearOptions().apply {
                clear = true
                setClearColor(0.043, 0.047, 0.058, 1.0)
            }
            runCatching { e.createSwapChain(s) }.getOrNull()
        }
    }


    private fun scheduleFrame() {
        if (released || failed || frameScheduled) return
        val renderHandler = handler ?: return
        frameScheduled = true
        val delay = if (viewVisible) FRAME_DELAY_MS else HIDDEN_FRAME_DELAY_MS
        renderHandler.postDelayed(frameRunnable, delay)
    }

    private val frameRunnable = Runnable { drawFrame() }

    private fun drawFrame() {
        frameScheduled = false
        if (released || failed) return
        if (!viewVisible) {
            scheduleFrame()
            return
        }
        if (surface == null) return
        val e = engine
        val r = renderer
        val v = view
        if (e == null || r == null || v == null) return
        if (swapChain == null) createSwapChain()
        val chain = swapChain ?: run {
            scheduleFrame()
            return
        }
        val now = System.nanoTime()
        val dt = if (lastFrameNanos == 0L) 0.0 else ((now - lastFrameNanos) / 1_000_000_000.0).coerceIn(0.0, 0.1)
        lastFrameNanos = now
        turntable.advance(dt)
        applyPose(e)
        if (runCatching { r.beginFrame(chain, now) }.getOrDefault(false)) {
            runCatching { r.render(v) }
            runCatching { r.endFrame() }
        }
        scheduleFrame()
    }

    /**
     * Turns the model about the glTF up axis through the authored footprint origin. If the root has no Transform
     * component (a malformed asset), the camera orbits the box centre instead so the hero still reads as turning.
     */
    private fun applyPose(e: Engine) {
        val angle = turntable.angle
        if (rootTransformInstance != 0) {
            runCatching { e.transformManager.setTransform(rootTransformInstance, DioramaPose.rotationY(angle)) }
            return
        }
        val d = fitDistance
        val eye = doubleArrayOf(
            center[0] + sin(angle) * d,
            center[1] + d * DioramaFraming.ELEVATION,
            center[2] + cos(angle) * d,
        )
        camera?.lookAt(
            eye[0],
            eye[1],
            eye[2],
            center[0].toDouble(),
            center[1].toDouble(),
            center[2].toDouble(),
            0.0,
            1.0,
            0.0,
        )
    }

    private fun stopLoop() {
        frameScheduled = true
        handler?.removeCallbacks(frameRunnable)
        frameScheduled = false
        lastFrameNanos = 0L
    }

    private fun fail(stage: String, t: Throwable?) {
        if (failed) return
        failed = true
        Log.w(TAG, "$systemName: diorama unavailable at $stage", t)
        if (unavailable.compareAndSet(false, true)) {
            mainHandler.post { runCatching { onUnavailable() } }
        }
    }

    private fun destroyAll() {
        stopLoop()
        val e = engine
        swapChain?.let { runCatching { e?.destroySwapChain(it) } }
        swapChain = null
        surface?.release()
        surface = null
        surfaceTexture?.let { runCatching { it.release() } }
        surfaceTexture = null
        // Order matters: take the asset's entities out of the scene, destroy the asset, then the extra
        // MaterialInstance this renderer created for the screen. A live instance whose Material is destroyed
        // aborts Engine.destroy() (SIGABRT in libfilament-jni; seen on the RP6 before this ordering was added).
        asset?.let { a ->
            scene?.let { s -> runCatching { s.removeEntities(a.entities) } }
            runCatching { assetLoader?.destroyAsset(a) }
        }
        asset = null
        screenMaterial?.let { mi -> runCatching { e?.destroyMaterialInstance(mi) } }
        screenMaterial = null
        runCatching { resources?.destroy() }
        resources = null
        runCatching { assetLoader?.destroy() }
        assetLoader = null
        runCatching { materials?.destroy() }
        materials = null
        screenTextures.forEach { runCatching { e?.destroyTexture(it) } }
        screenTextures.clear()
        screenBuffers.clear()
        indirectLight?.let { runCatching { e?.destroyIndirectLight(it) } }
        indirectLight = null
        scene?.let { s ->
            lightEntities.forEach { entity ->
                runCatching { s.remove(entity) }
                runCatching { EntityManager.get().destroy(entity) }
            }
        }
        lightEntities.clear()
        renderer?.let { r -> runCatching { e?.destroyRenderer(r) } }
        renderer = null
        view?.let { v -> runCatching { e?.destroyView(v) } }
        view = null
        if (cameraEntity != 0) {
            runCatching { e?.destroyCameraComponent(cameraEntity) }
            runCatching { EntityManager.get().destroy(cameraEntity) }
            cameraEntity = 0
        }
        camera = null
        scene?.let { s -> runCatching { e?.destroyScene(s) } }
        scene = null
        runCatching { e?.flushAndWait() }
        runCatching { e?.destroy() }
        engine = null
        pendingArt = null
    }

    companion object {
        private const val TAG = "DioramaRenderer"
        private const val FRAME_DELAY_MS = 16L
        private const val HIDDEN_FRAME_DELAY_MS = 200L
    }
}
