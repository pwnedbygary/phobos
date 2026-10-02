package com.phobos.emulator.ui.theme

/** The space backdrop's colors as ARGB, derived from the theme (see [pixelSceneColors]). */
internal data class PixelSceneColors(
    val skyTop: Int,
    val skyBottom: Int,
    val star: Int,
    val planetLit: Int,
    val planetShade: Int,
    val crater: Int,
    val rim: Int,
    val glow: Int,
)

/**
 * The space backdrop's still picture as a small ARGB image, one element per art pixel, meant to be
 * scaled up without filtering: a sky in hard bands with ordered dithering between them, stars in the
 * upper sky, and the limb of a planet along the bottom with a lit side, a shaded side, craters and a
 * bright rim with a dithered glow ([SpaceArt] animates it). The same size, colors and seed always give
 * the same image.
 */
internal object PixelScene {
    /** Where the planet's top is, as a fraction of the height. */
    const val HORIZON = 0.74f

    fun render(cols: Int, rows: Int, colors: PixelSceneColors, seed: Int = 1977): IntArray {
        require(cols > 0 && rows > 0)
        return IntArray(cols * rows).also { SpaceArt(cols, rows, colors, seed).compose(0L, it, live = false) }
    }

    /** Linear mix of two opaque ARGB colors, [t] toward [b]. */
    fun mix(a: Int, b: Int, t: Float): Int = PixelKit.mix(a, b, t)
}
