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
 * on the GPU (with the driver Settings' Driver Manager chose, or the system's): the GPU transforms and lights 3D itself,
 * gives 16-bit pictures the shader's own reads, filters above Native with its own linear filter and blends with its own
 * units — the fastest, very close, a little less exact than the software renderer. (The core's older accurate Vulkan
 * mode, the CPU transforming and lighting and the shader blending, is still there for the tests and for a developer
 * property, `debug.phobos.psp.renderer 1`; Settings no longer offers it.) A saved 1 meant accurate before this and a
 * saved 2 fast: both are taken as the Vulkan renderer now. The core checks the GPU's pictures against the software
 * renderer's as the game starts, and draws with the software renderer, saying so once, where they're wrong or the GPU
 * stops answering.
 */
object PspRenderer {
    const val SOFTWARE = 0
    /** The Vulkan renderer (the core's fast mode: the GPU transforms, lights and blends). */
    const val VULKAN = 1
    /** A saved choice from when Settings offered the core's accurate Vulkan mode as its own entry; taken as [VULKAN]. */
    const val VULKAN_FAST = 2

    /** The choices Settings offers: the software renderer, or Vulkan. */
    val choices = listOf(SOFTWARE, VULKAN)

    fun label(renderer: Int): String = when (renderer) {
        SOFTWARE -> "Software (exact)"
        else -> "Vulkan"
    }

    /** What each choice does, for Settings. */
    fun description(renderer: Int): String = when (renderer) {
        SOFTWARE -> "Software (exact) draws every pixel as the PSP does."
        else -> "Vulkan draws on the GPU: the fastest, very close, a little less exact than Software."
    }

    /** What the core is told: the software renderer's 0, or Vulkan's 2 (the core's fast mode); anything else Software.
     * The core's accurate mode's 1 is not offered by Settings; a developer property asks for it. */
    fun forCore(renderer: Int): Int = when (renderer) {
        VULKAN, VULKAN_FAST -> 2
        else -> SOFTWARE
    }

    /** A saved choice as Settings shows it: one of [choices] (a save from the accurate mode's day shows as Vulkan; a
     * value from elsewhere as Software, as the core would be told). */
    fun forDisplay(renderer: Int): Int = when (renderer) {
        VULKAN, VULKAN_FAST -> VULKAN
        else -> SOFTWARE
    }
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
 * The PSP's "Graphics overlap" setting, handed to the core as its option "GE Thread" as a game starts: Off, the
 * default, draws a frame's pictures in turn with the game's own work, as the PSP's GE and CPU share one thread; On
 * draws them on a thread of their own while the game goes on with the next frame, so the two overlap. The pictures and
 * everything the game sees are the same either way; whether On is faster depends on the game (it's slower in some that
 * write a lot to the PSP's video memory each frame). What On costs: each picture reaches the screen a frame later (at
 * a frame's end the screen shows the frame before, the next still being drawn), about 17 ms more at 60 frames a second
 * between pressing a button and seeing what it did. It works with either GE timing: with the PSP's (the default), the
 * game's thread still gets the GE's times exactly as without the overlap.
 */
object PspGraphicsOverlap {
    const val OFF = 0
    const val ON = 1

    /** The choices Settings offers. */
    val choices = listOf(OFF, ON)

    fun label(mode: Int): String = when (mode) {
        ON -> "On"
        else -> "Off"
    }

    /** What each choice does, for Settings. */
    fun description(mode: Int): String = when (mode) {
        ON -> "On draws them on a thread of their own while the game goes on: faster in some games, slower in others."
        else -> "Off draws them in turn with the game's own work, as before."
    }

    /** What the core is told: one of [choices], anything else taken as Off. */
    fun forCore(mode: Int): Int = if (mode in choices) mode else OFF

    /** The core's option "GE Thread" for a choice. */
    fun option(mode: Int): String = if (forCore(mode) == ON) "On" else "Off"
}

/**
 * The PSP's settings a game takes as it starts, as the core is told them: kept for the running game. [geTiming]: the
 * GE timed as a real PSP's (the core's "GE Timing"), which paces the game as a PSP does, not by Sony's peak rates.
 * [fastFileIo]: file requests at the host's speed rather than a PSP's devices' (the core's "File I/O" "Fast",
 * experimental), which shortens loads, saves and installs.
 */
data class PspStartSettings(
    val renderer: Int,
    val resolution: Int,
    val drawingThreads: Int,
    val geTiming: Boolean = true,
    val graphicsOverlap: Int = PspGraphicsOverlap.OFF,
    val fastFileIo: Boolean = false,
) {
    /** Whether a game started with these runs as one started with [other] would: Resolution is the Vulkan renderer's alone. */
    fun runsAs(other: PspStartSettings): Boolean =
        renderer == other.renderer && drawingThreads == other.drawingThreads && geTiming == other.geTiming &&
            graphicsOverlap == other.graphicsOverlap && fastFileIo == other.fastFileIo &&
            (renderer == PspRenderer.SOFTWARE || resolution == other.resolution)

    companion object {
        fun of(
            renderer: Int,
            resolution: Int,
            drawingThreads: Int,
            geTiming: Boolean = true,
            graphicsOverlap: Int = PspGraphicsOverlap.OFF,
            fastFileIo: Boolean = false,
        ) = PspStartSettings(
            PspRenderer.forCore(renderer), PspResolution.forCore(resolution), PspDrawingThreads.forCore(drawingThreads),
            geTiming, graphicsOverlap = PspGraphicsOverlap.forCore(graphicsOverlap), fastFileIo = fastFileIo,
        )
    }
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
