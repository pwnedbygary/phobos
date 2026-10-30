package com.phobos.emulator.ui.carts

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/**
 * The Library card's cart picture as a cached PNG file, or null while it renders (the card keeps showing the box
 * until then) and when Filament cannot start or carts are off. Uses the screen's shared [CartThumbnailer] and
 * [CartBitmapCache]; rendering happens on IO. Keys include the label source, look, wear, theme and face, so any
 * change re-renders once and no card re-renders per frame.
 */
@Composable
fun rememberCartCardFile(
    system: String,
    name: String,
    title: String?,
    labelFile: File?,
    box: File?,
    looksJson: String,
    specialEditions: Boolean,
    wearOn: Boolean,
    playTimeMs: Long,
    themeKey: String,
    faceKey: String,
    engraving: String,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    enabled: Boolean,
): File? {
    val context: Context = LocalContext.current
    val shape = CartShapes.cartShape(system) ?: return null
    if (!enabled || cache == null) return null
    val disc = CartShapes.isDisc(system)
    val engravingFor = if (CartShapes.entry(system)?.id == "gba") engraving else "none"
    val renderShape = if (engravingFor != "none") "gba-$engravingFor" else shape
    val look = LookResolver.resolve(
        LookResolver.GameInput(CartLooks.gameId(system, name), name),
        system,
        CartLooks.parse(looksJson),
        specialEditions,
    )
    val wear = if (wearOn) LookResolver.wearLevel(playTimeMs / 1000, "on") else 0
    val labelDesc = labelFile?.let { "${it.lastModified()}:${it.length()}" }
        ?: "gen:${title ?: name}:$themeKey:${box?.lastModified() ?: 0}-${box?.length() ?: 0}"
    val key = remember(system, name, renderShape, look, wear, labelDesc, themeKey, faceKey) {
        CartBitmapCache.Key(
            gameId = CartLooks.gameId(system, name),
            kind = (if (disc) "disc:" else "cart:") + renderShape,
            theme = themeKey,
            face = faceKey,
            finish = look?.finish ?: "",
            color = look?.color ?: "",
            special = look?.special ?: false,
            wear = wear,
            width = 256,
            height = 256,
            label = labelDesc,
        )
    }
    var file by remember(key) { mutableStateOf<File?>(null) }
    LaunchedEffect(key) {
        file = withContext(Dispatchers.IO) {
            val thumb = thumbnailer
            if (thumb == null) {
                // No engine (a game is resident) but carts are on: a cached picture is still worth showing.
                return@withContext cacheOnlyPicture(cache, key)
            }
            runCatching {
                cache.get(key) { target ->
                    val label = CartLabelSource.label(context, system, title ?: name, box, labelFile, themeKey)
                    val bmp = thumb.render(renderShape, disc, 256, 256, look, wear, label)
                    label?.recycle()
                    if (bmp == null) throw IllegalStateException("cart render failed")
                    try {
                        FileOutputStream(target).use { out ->
                            if (!bmp.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out)) {
                                throw IllegalStateException("not written")
                            }
                        }
                    } finally {
                        bmp.recycle()
                    }
                }
            }.getOrNull()
        }
    }
    return file
}

/**
 * The cache-only fallback used while no engine may run (a game is resident): a stored picture for [key], or null
 * so the card shows box art. Kept a plain function so the branch is testable without Compose or Filament.
 */
internal fun cacheOnlyPicture(cache: CartBitmapCache?, key: CartBitmapCache.Key?): File? =
    if (cache == null || key == null) null else cache.cached(key)
