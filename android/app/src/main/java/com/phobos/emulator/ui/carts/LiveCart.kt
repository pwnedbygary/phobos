package com.phobos.emulator.ui.carts

import android.graphics.BitmapFactory
import android.view.SurfaceView
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.ui.RomFile
import java.io.File

/**
 * The one live cart: the focused game's shell, slowly turning, on its own surface. It is the only per-frame work in
 * a system's list, and it only exists while no game is loaded - the engine is released before a game starts and
 * created again after it closes (MainViewModel). Cards in the grid stay offscreen bitmaps.
 */
@Composable
fun LiveCartView(
    system: String,
    rom: RomFile?,
    labels: Map<GameKey, File>,
    boxes: Map<GameKey, File>,
    themeKey: String,
    looks: Map<GameKey, LookResolver.Look>,
    specialEditions: Boolean,
    wearOn: Boolean,
    engraving: String,
    playtimeSeconds: (RomFile) -> Long,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val shape = CartShapes.cartShape(system) ?: return
    val disc = CartShapes.isDisc(system)
    val renderShape = if (CartShapes.entry(system)?.id == "gba" && engraving != "none") "gba-$engraving" else shape
    val key = rom?.let { GameKey(system, it.name) }
    val look = key?.let { looks[it] } ?: rom?.let { if (specialEditions) LookResolver.findSpecialEdition(it.name, system) else null }
    val wear = if (wearOn && rom != null) LookResolver.wearLevel(playtimeSeconds(rom), "on") else 0
    val label = key?.let { labels[it] }
    val box = key?.let { boxes[it] }
    val title = rom?.let { com.phobos.emulator.util.displayTitle(it.name, it.title) } ?: ""

    var renderer by remember { mutableStateOf<CartRenderer?>(null) }
    DisposableEffect(Unit) {
        onDispose {
            renderer?.detach()
            renderer = null
        }
    }
    AndroidView(
        factory = { ctx ->
            SurfaceView(ctx).also { surface ->
                renderer = CartRenderer(ctx, surface).also { it.attach() }
            }
        },
        modifier = modifier.fillMaxSize(),
    )
    LaunchedEffect(renderShape, disc, look, wear, label?.lastModified(), label?.length(), box?.lastModified(), box?.length(), title, themeKey, renderer) {
        val r = renderer ?: return@LaunchedEffect
        val bitmap = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            CartLabelSource.label(context, system, title, box, label, themeKey)
        }
        r.show(renderShape, disc, look, wear, bitmap)
        // The material keeps the texture, not the bitmap; CartMaterials copied it into GPU memory.
        bitmap?.recycle()
    }
}
