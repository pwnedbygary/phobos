package com.phobos.emulator.ui.carts

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.ui.focusRing
import androidx.compose.ui.focus.onFocusChanged
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.romTitle
import java.io.File

/**
 * A system's games as carts (or discs) in a grid, each card the game's own shell and label.
 *
 * Cards render offscreen once per look (see CartThumbnailer and CartBitmapCache) and are cached as PNGs; the grid
 * never renders per frame. A system without a shape, or a device where Filament cannot start, keeps the list -
 * [SystemDetailScreen] decides, this composable assumes a shape exists.
 */
@Composable
fun CartGrid(
    system: String,
    roms: List<RomFile>,
    boxes: Map<GameKey, File>,
    labels: Map<GameKey, File>,
    favourites: Set<String>,
    looks: Map<GameKey, LookResolver.Look>,
    specialEditions: Boolean,
    wearOn: Boolean,
    engraving: String,
    playtimeSeconds: (RomFile) -> Long,
    themeKey: String,
    faceKey: String,
    onStart: (RomFile) -> Unit,
    onMenu: (RomFile) -> Unit,
    onFavourite: (RomFile) -> Unit,
    onFocus: (RomFile) -> Unit = {},
) {
    val context = LocalContext.current
    val shape = CartShapes.cartShape(system)
    val disc = CartShapes.isDisc(system)
    val engine = remember { CartsEngine.get(context) }
    var focusedRom by remember { mutableStateOf(roms.firstOrNull()) }
    val cache = remember { CartBitmapCache(File(context.cacheDir, "cart-render"), 64L * 1024 * 1024) }
    val thumbnailer = remember(engine) { engine?.let { CartThumbnailer(context, it) } }
    DisposableEffect(thumbnailer) {
        onDispose { thumbnailer?.release() }
    }

    LazyVerticalGrid(
        columns = GridCells.Adaptive(minSize = 132.dp),
        contentPadding = PaddingValues(16.dp, 8.dp, 16.dp, 80.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize(),
    ) {
        item(span = { GridItemSpan(maxLineSpan) }) {
            LiveCartView(
                system = system,
                rom = focusedRom,
                labels = labels,
                boxes = boxes,
                themeKey = themeKey,
                looks = looks,
                specialEditions = specialEditions,
                wearOn = wearOn,
                engraving = engraving,
                playtimeSeconds = playtimeSeconds,
                modifier = Modifier.fillMaxWidth().height(180.dp),
            )
        }
        items(roms, key = { it.name }) { rom ->
            val key = GameKey(system, rom.name)
            val title = if (rom.discs.isNotEmpty()) romTitle(rom.name)
                else com.phobos.emulator.util.displayTitle(rom.name, rom.title)
            val look = looks[key]
                ?: if (specialEditions) LookResolver.findSpecialEdition(rom.name, system) else null
            val wear = if (wearOn) LookResolver.wearLevel(playtimeSeconds(rom), "on") else 0
            val engravingFor = if (CartShapes.entry(system)?.id == "gba") engraving else "none"
            val renderShape = if (engravingFor != "none") "gba-$engravingFor" else shape
            val labelFile = labels[key]
            val cacheKey = CartBitmapCache.Key(
                gameId = (system + "/" + rom.name).hashCode().toLong(),
                kind = (if (disc) "disc:" else "cart:") + (renderShape ?: ""),
                theme = themeKey,
                face = faceKey,
                finish = look?.finish ?: "",
                color = look?.color ?: "",
                special = look?.special == true,
                wear = wear,
                width = 384,
                height = 384,
                label = labelFile?.let { "${it.lastModified()}:${it.length()}" }
                    ?: "gen:${title.hashCode()}:$themeKey:${boxes[key]?.lastModified() ?: 0}-${boxes[key]?.length() ?: 0}",
            )
            var bitmap by remember(cacheKey) { mutableStateOf<Bitmap?>(null) }
            LaunchedEffect(cacheKey) {
                if (renderShape == null || thumbnailer == null) return@LaunchedEffect
                val file = runCatching {
                    kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                        val labelBmp = CartLabelSource.label(context, system, title, boxes[key], labelFile, themeKey)
                        try {
                            cache.get(cacheKey) { target ->
                                val bmp = thumbnailer.render(renderShape, disc, 384, 384, look, wear, labelBmp)
                                    ?: throw IllegalStateException("no thumbnail")
                                try {
                                    java.io.FileOutputStream(target).use { out ->
                                        if (!bmp.compress(Bitmap.CompressFormat.PNG, 100, out)) {
                                            throw java.io.IOException("not written")
                                        }
                                    }
                                } finally {
                                    bmp.recycle()
                                }
                            }
                        } finally {
                            labelBmp?.recycle()
                        }
                    }
                }.getOrNull()
                if (file != null) {
                    bitmap = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                        runCatching { BitmapFactory.decodeFile(file.absolutePath) }.getOrNull()
                    }
                }
            }
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier = Modifier
                    .fillMaxWidth()
                    .onFocusChanged { if (it.isFocused) { focusedRom = rom; onFocus(rom) } }
                    .combineClick(onStart = { onStart(rom) }, onMenu = { onMenu(rom) })
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(1f)
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f), pillShape()),
                    contentAlignment = Alignment.Center,
                ) {
                    val shown = bitmap
                    if (shown != null) {
                        Image(
                            bitmap = shown.asImageBitmap(),
                            contentDescription = title,
                            contentScale = ContentScale.Fit,
                            modifier = Modifier.fillMaxSize().padding(6.dp),
                        )
                    } else {
                        boxes[key]?.let { box ->
                            coil.compose.AsyncImage(
                                model = box,
                                contentDescription = title,
                                contentScale = ContentScale.Fit,
                                modifier = Modifier.fillMaxSize().padding(6.dp),
                            )
                        }
                    }
                    if (favourites.contains(rom.name)) {
                        Icon(
                            Icons.Filled.Star,
                            contentDescription = "Favorite",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.align(Alignment.TopEnd).padding(4.dp),
                        )
                    }
                }
                Text(
                    title,
                    style = MaterialTheme.typography.labelMedium,
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.fillMaxWidth().height(34.dp).padding(top = 4.dp),
                )
            }
        }
    }
}

/** Click opens the game menu; a long press opens the game menu too (the list's held-A equivalent). */
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
private fun Modifier.combineClick(onStart: () -> Unit, onMenu: () -> Unit): Modifier {
    val interaction = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
    return this
        .focusRing(MaterialTheme.colorScheme.primary)
        .combinedClickable(
            interactionSource = interaction,
            indication = androidx.compose.foundation.LocalIndication.current,
            onClick = onStart,
            onLongClick = onMenu,
        )
}
