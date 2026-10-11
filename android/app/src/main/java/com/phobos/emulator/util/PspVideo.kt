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
 * The PSP's "Renderer" setting: who draws a PSP game's pictures, handed to the core as its option "Renderer" as a
 * game starts. The software renderer, the default, draws them as the PSP does, exactly. The Vulkan renderer draws them
 * on the GPU (with the driver Settings' Driver Manager chose, or the system's) in one of two modes: accurate (the CPU
 * transforms and lights the vertices, and the shader blends where the driver allows: very close, most 2D exact at
 * Native), or fast (the GPU transforms and lights 3D itself and blends with its own units: very close, a little less
 * exact, and the fastest). A saved 1 is accurate, as Vulkan was before fast came. The core checks the GPU's pictures
 * against the software renderer's as the game starts, and draws with the software renderer, saying so once, where
 * they're wrong or the GPU stops answering.
 */
object PspRenderer {
    const val SOFTWARE = 0
    const val VULKAN = 1
    const val VULKAN_FAST = 2

    /** The choices Settings offers. */
    val choices = listOf(SOFTWARE, VULKAN, VULKAN_FAST)

    fun label(renderer: Int): String = when (renderer) {
        VULKAN -> "Vulkan (accurate)"
        VULKAN_FAST -> "Vulkan (fast)"
        else -> "Software (exact)"
    }

    /** What each choice does, for Settings. */
    fun description(renderer: Int): String = when (renderer) {
        VULKAN -> "Vulkan (accurate) draws on the GPU as close to the PSP as it can: very close, most 2D exact at Native."
        VULKAN_FAST -> "Vulkan (fast) also transforms and lights 3D on the GPU and blends with its own units: the fastest, very close, a little less exact."
        else -> "Software (exact) draws every pixel as the PSP does."
    }

    /** What the core is told: one of [choices], anything else taken as the software renderer. */
    fun forCore(renderer: Int): Int = if (renderer in choices) renderer else SOFTWARE
}

/**
 * The PSP's "Resolution" setting: the Vulkan renderer's internal resolution, handed to the core as its option
 * "Resolution" as a game starts. Native, the default, draws the PSP's own 480x272 pixels, each alone (the exact
 * native mode); 2x to 10x draw each of them 2 to 10 times over each way, sharper on a large screen, while what the
 * game reads back of its pictures stays the PSP's pixels. The core holds it to what the GPU takes. The software
 * renderer always draws at Native.
 */
object PspResolution {
    const val NATIVE = 1

    /** The choices Settings offers. */
    val choices = (1..10).toList()

    fun label(scale: Int): String =
        if (scale <= NATIVE) "Native (480x272)" else "${scale}x (${480 * scale}x${272 * scale})"

    /** What the core is told: one of [choices], anything else taken as Native. */
    fun forCore(scale: Int): Int = if (scale in choices) scale else NATIVE
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
