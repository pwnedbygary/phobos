package com.phobos.emulator.ui.diorama

import kotlin.math.roundToInt

/**
 * What the CRT shows: the box art of the page's last-played (else first) game, or a dark neutral glow when the
 * system has no art. The picture is a game's cover, never a captured frame: the repository is public and ships no
 * third-party game art, and a gameplay frame on the CRT is a possible follow-up from the user's own on-device
 * capture at runtime, never a committed asset.
 *
 * The TV face is 4:3 and covers are not, so [fit] letterboxes the cover into the target, centred on the screen's
 * dark background. Cropping a portrait cover to 4:3 would cut it in half; fitting keeps the whole cover legible.
 */
internal enum class DioramaScreenSource {
    BOX_ART,
    NEUTRAL,
}

/** Where a fitted source image is drawn inside the CRT texture, in target pixels. */
internal data class DioramaFit(val x: Int, val y: Int, val width: Int, val height: Int)

internal object DioramaScreenArt {

    /** Texture size uploaded for the CRT face; the face is small on screen, so this stays cheap. */
    const val WIDTH = 512
    const val HEIGHT = 384

    /** The source: a game's cover when one is available, otherwise the neutral screen. */
    fun choose(hasBoxArt: Boolean): DioramaScreenSource =
        if (hasBoxArt) DioramaScreenSource.BOX_ART else DioramaScreenSource.NEUTRAL

    /**
     * The centred, aspect-preserving destination of a [sourceWidth] x [sourceHeight] picture inside a
     * [targetWidth] x [targetHeight] texture: the whole picture fits, with the leftover bars left to the dark
     * background. A zero/negative source yields an empty rect (the caller then draws the neutral screen only).
     */
    fun fit(
        sourceWidth: Int,
        sourceHeight: Int,
        targetWidth: Int = WIDTH,
        targetHeight: Int = HEIGHT,
    ): DioramaFit {
        if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
            return DioramaFit(0, 0, 0, 0)
        }
        val scale = minOf(
            targetWidth.toDouble() / sourceWidth.toDouble(),
            targetHeight.toDouble() / sourceHeight.toDouble(),
        )
        val width = (sourceWidth * scale).roundToInt().coerceIn(1, targetWidth)
        val height = (sourceHeight * scale).roundToInt().coerceIn(1, targetHeight)
        return DioramaFit(
            x = (targetWidth - width) / 2,
            y = (targetHeight - height) / 2,
            width = width,
            height = height,
        )
    }
}
