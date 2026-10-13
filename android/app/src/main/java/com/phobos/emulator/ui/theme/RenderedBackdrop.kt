package com.phobos.emulator.ui.theme

import android.graphics.Bitmap
import android.util.LruCache
import androidx.compose.foundation.layout.Spacer
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * A still backdrop drawn off the main thread into a bitmap at the backdrop's size, up to [RENDER_PIXELS]
 * (larger screens get it scaled up), whenever that or [key] changes. [key] must cover everything [render]
 * draws from, by value, and name the backdrop, so no other's equal key finds its picture: callers key it on the
 * theme's final colors, not the cross-fade's, which would draw it again at every step. The last few pictures
 * are kept ([Pictures]), so the backdrop is back at once after a game or the screensaver. Until a new size's
 * picture is ready the one before shows, cropped to fit; until the first is, [placeholder]. [render] gives
 * width times height ARGB pixels.
 */
@Composable
internal fun RenderedBackdrop(key: Any, placeholder: Color, modifier: Modifier, render: (width: Int, height: Int) -> IntArray) {
    var size by remember { mutableStateOf(IntSize.Zero) }
    val picture by produceState<ImageBitmap?>(null, size, key) {
        if (size.width <= 0 || size.height <= 0) return@produceState
        val scale = min(1f, sqrt(RENDER_PIXELS / (size.width.toFloat() * size.height)))
        val width = (size.width * scale).roundToInt().coerceAtLeast(1)
        val height = (size.height * scale).roundToInt().coerceAtLeast(1)
        val id = Triple(key, width, height)
        Pictures.get(id)?.let {
            value = it
            return@produceState
        }
        // While a window is being resized, wait for it to settle rather than draw every size it passes.
        delay(SETTLE_MS)
        // Kept from the background, so a render that outlives the backdrop still spares the next one.
        value = withContext(Dispatchers.Default) {
            try {
                Bitmap.createBitmap(render(width, height), width, height, Bitmap.Config.ARGB_8888).asImageBitmap().also { Pictures.put(id, it) }
            } catch (e: OutOfMemoryError) {
                null
            }
        } ?: return@produceState
    }
    Spacer(
        modifier
            .onSizeChanged { size = it }
            .drawBehind {
                val image = picture
                if (image == null) {
                    drawRect(placeholder)
                    return@drawBehind
                }
                if (this.size.width < 1f || this.size.height < 1f) return@drawBehind
                // The picture's middle, at the backdrop's shape: all of it once it's drawn for this size.
                val shown = min(image.width / this.size.width, image.height / this.size.height)
                val crop = IntSize((this.size.width * shown).roundToInt().coerceIn(1, image.width), (this.size.height * shown).roundToInt().coerceIn(1, image.height))
                drawImage(
                    image,
                    srcOffset = IntOffset((image.width - crop.width) / 2, (image.height - crop.height) / 2),
                    srcSize = crop,
                    dstSize = IntSize(this.size.width.roundToInt(), this.size.height.roundToInt()),
                    filterQuality = FilterQuality.Low,
                )
            },
    )
}

/** The last rendered backdrops, by key and size, up to [BYTES]: the app's own and the screensaver's, and a theme or two before. */
private object Pictures : LruCache<Triple<Any, Int, Int>, ImageBitmap>(BYTES) {
    override fun sizeOf(key: Triple<Any, Int, Int>, value: ImageBitmap) = value.width * value.height * 4
}

/** The most pixels a rendered backdrop is drawn at: a 1080p screen's. */
private const val RENDER_PIXELS = 1920f * 1080f

/** Three 1080p pictures. */
private const val BYTES = 3 * 1920 * 1080 * 4

private const val SETTLE_MS = 150L
