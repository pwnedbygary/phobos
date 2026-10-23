package com.phobos.emulator.ui

import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntSize
import com.phobos.emulator.PhobosCore

/** How far the intro has got: its first frame's time, and the last frame drawn and the skip (negative: none), in ms. */
private class IntroProgress {
    var start = 0L
    var drawnAt = 0
    var skippedAt = -1
}

/**
 * Phobos's intro over the Library as the app opens (the native PhobosIntro.cpp draws it, the desktop's too): pixel art
 * on a small canvas, scaled up by a whole number so each pixel stays square. A touch skips it, and so does Back, and a
 * key once [skipped] says so (MainActivity's IntroKeys); [onDone] runs once it has dissolved into the Library, which
 * is composed under it all along. Every touch is the intro's while it's here.
 */
@Composable
fun IntroSplash(skipped: Boolean, onSkip: () -> Unit, onDone: () -> Unit) {
    // Kept when the view changes size, and the canvas with it (the activity handles a rotation itself), so a new
    // canvas goes on from the same moment. Plain fields, not state: the frames below change them, and nothing redraws
    // or recomposes for that.
    val progress = remember { IntroProgress() }
    val skippedNow by rememberUpdatedState(skipped)
    val skip by rememberUpdatedState(onSkip)
    val done by rememberUpdatedState(onDone)
    // Back comes to the window as a key, which IntroKeys takes, while the manifest opts out of predictive back. Should
    // a later Android drop that opt-out, Back would go to the back callbacks first, and without this leave the app.
    BackHandler { skip() }
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        if (event.changes.any { it.pressed && !it.previousPressed }) skip()
                        event.changes.forEach { it.consume() }
                    }
                }
            },
    ) {
        val canvas = remember(constraints.maxWidth, constraints.maxHeight) {
            PhobosCore.introCanvas(constraints.maxWidth, constraints.maxHeight)
        }
        val (width, height, scale) = canvas
        val bitmap = remember(width, height) {
            if (width > 0 && height > 0) {
                // Drawn at once: a new bitmap is clear, and the Library would show through it until the next frame.
                Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                    .also { PhobosCore.renderIntro(it, progress.drawnAt, progress.skippedAt) }
            } else {
                null
            }
        }
        val image = remember(bitmap) { bitmap?.asImageBitmap() }
        var frame by remember { mutableIntStateOf(0) }
        LaunchedEffect(bitmap) {
            if (bitmap == null) return@LaunchedEffect done()
            while (true) {
                val now = withFrameNanos { it }
                if (progress.start == 0L) progress.start = now
                val ms = ((now - progress.start) / 1_000_000).toInt()
                if (skippedNow && progress.skippedAt < 0) progress.skippedAt = ms
                if (!PhobosCore.renderIntro(bitmap, ms, progress.skippedAt)) break
                progress.drawnAt = ms
                frame++
            }
            done()
        }
        Canvas(Modifier.fillMaxSize()) {
            if (frame >= 0 && image != null) {
                drawImage(image, dstSize = IntSize(width * scale, height * scale), filterQuality = FilterQuality.None)
            }
        }
    }
}
