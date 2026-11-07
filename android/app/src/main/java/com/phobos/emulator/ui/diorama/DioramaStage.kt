package com.phobos.emulator.ui.diorama

import android.graphics.Bitmap
import android.view.TextureView
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * The handle a page keeps so it can destroy the stage's engine synchronously before a game load, matching the
 * carts engine's release-before-load contract. The Library page should hold one and call [releaseNow] before
 * starting a game.
 */
/**
 * The pure rendering gate the stage and its tests share: frames only while the page is the settled/active one and
 * the view is shown. Keeping it pure makes the "inactive must not burn the GPU" rule testable off-device.
 */
internal object DioramaStagePolicy {
    fun shouldRender(active: Boolean, shown: Boolean): Boolean = active && shown
}

internal class DioramaHandle {
    @Volatile internal var renderer: DioramaRenderer? = null

    fun releaseNow() {
        renderer?.releaseNow()
    }
}

/**
 * The release seam for pages that cannot pass a handle through their composition: the last composed stage's engine
 * can be released synchronously before a game load. The handle above is preferred when the page can hold one.
 */
internal object DioramaStageController {
    @Volatile private var renderer: DioramaRenderer? = null

    internal fun attach(r: DioramaRenderer) {
        renderer = r
    }

    internal fun detach(r: DioramaRenderer) {
        if (renderer === r) renderer = null
    }

    /** Destroys the active stage's engine (bounded, on the render thread); no-op when no stage is composed. */
    fun releaseNow() {
        renderer?.releaseNow()
    }
}

/**
 * The Library's live diorama page: one system's console + CRT turning in place, transparent over the page.
 *
 * [active] is the Library's "this is the settled page" flag. Inactive stages emit nothing, so the previous one is
 * disposed and its engine is released off the main thread; the pages that are not settled are drawn by
 * [DioramaStill] instead, so a flip never keeps a Filament engine per system. A stage also skips frames while its
 * TextureView is not shown (the renderer gates on `isShown`), and it has no band height or page/scroll assumptions:
 * it fills whatever Box/aspect the Library gives it and frames from the GLB's own world bounds.
 *
 * The CRT shows the box art of the page's last-played (else first) game; the Library passes it through the second
 * overload. Without it the screen is the dark neutral glow. The picture is never a captured game frame: the public
 * repo ships no third-party game art.
 */
@Composable
internal fun DioramaStage(systemName: String, active: Boolean, modifier: Modifier = Modifier) {
    DioramaStage(systemName, active, artFile = null, modifier = modifier)
}

/** The same stage with the page's box art for the CRT; see the note on the primary overload. */
@Composable
internal fun DioramaStage(
    systemName: String,
    active: Boolean,
    artFile: File?,
    modifier: Modifier = Modifier,
) {
    if (!active) return
    val variant = remember(systemName) { DioramaSystems.variantFor(systemName, null) } ?: return
    var failed by remember(systemName, variant.id) { mutableStateOf(false) }
    if (failed) return
    val context = LocalContext.current
    val renderer = remember(systemName, variant.id) {
        DioramaRenderer(context, systemName, variant.asset) { failed = true }
    }
    val handle = remember(renderer) { DioramaHandle() }

    DisposableEffect(renderer, handle) {
        handle.renderer = renderer
        DioramaStageController.attach(renderer)
        onDispose {
            if (handle.renderer === renderer) handle.renderer = null
            DioramaStageController.detach(renderer)
            renderer.detach()
        }
    }

    LaunchedEffect(renderer, variant, artFile) {
        val bitmap = withContext(Dispatchers.IO) {
            val hasBoxArt = artFile?.isFile == true
            when (DioramaScreenArt.choose(hasBoxArt)) {
                DioramaScreenSource.BOX_ART -> artFile?.let { DioramaArtLoader.loadFile(it) }
                DioramaScreenSource.NEUTRAL -> null
            } ?: DioramaArtLoader.neutral()
        }
        renderer.setScreenArt(bitmap)
    }

    Box(modifier = modifier) {
        AndroidView(
            factory = { ctx ->
                TextureView(ctx).also {
                    it.isOpaque = false
                    renderer.attach(it)
                }
            },
            modifier = Modifier.fillMaxSize(),
        )
        // Transparent gesture layer: the TextureView does not consume touches itself.
        Box(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(renderer) {
                    detectHorizontalDragGestures(
                        onDragStart = { renderer.dragStart() },
                        onDragEnd = { renderer.dragEnd() },
                        onDragCancel = { renderer.dragEnd() },
                        onHorizontalDrag = { change, dragAmount ->
                            change.consume()
                            renderer.drag(dragAmount)
                        },
                    )
                },
        )
    }
}
