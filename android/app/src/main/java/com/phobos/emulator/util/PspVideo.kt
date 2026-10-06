package com.phobos.emulator.util

import kotlin.math.roundToInt

/**
 * The PSP's "Drawing threads" setting: how many threads draw a PSP game's pictures, handed to the core as its option
 * "GE Threads" as a game starts. Auto, the default (the owner's choice), is all the device's cores but one, as the
 * core counts them; a count of 1 draws on the emulation thread alone. Every choice draws the very same pictures:
 * more threads only draw them sooner.
 */
object PspDrawingThreads {
    const val AUTO = 0

    /** The choices Settings offers. */
    val choices = listOf(AUTO, 1, 2, 4, 6, 8)

    fun label(threads: Int): String = when (threads) {
        AUTO -> "Auto (all cores but one)"
        1 -> "1 (the emulation thread)"
        else -> "$threads"
    }

    /** What the core is told: one of [choices], anything else (a setting from elsewhere, say) taken as Auto. */
    fun forCore(threads: Int): Int = if (threads in choices) threads else AUTO
}

/**
 * The whole multiple of a picture of [width] x [height] that native code draws it at for a view of [viewWidth] x
 * [viewHeight] pixels: the one nearest to how much the view enlarges it, 1 to 4 (each pixel repeated, nearest-
 * neighbour), so that the compositor's own scaling (bilinear) is slight: "sharp bilinear". A PSP's 480x272 shown
 * 1080 rows high is drawn 4 times over (1920x1088, which the compositor makes 0.993 as big), its one-pixel lines sharp
 * and even all down the screen; scaled 3.97 times by the compositor alone, they were soft bands, some fainter.
 */
fun pictureMultiple(width: Float, height: Float, viewWidth: Float, viewHeight: Float): Int {
    if (width <= 0f || height <= 0f || viewWidth <= 0f || viewHeight <= 0f) return 1
    return minOf(viewWidth / width, viewHeight / height).roundToInt().coerceIn(1, 4)
}
