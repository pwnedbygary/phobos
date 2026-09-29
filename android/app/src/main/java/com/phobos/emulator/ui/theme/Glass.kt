package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.toArgb
import com.phobos.emulator.data.GlassEffects
import androidx.compose.ui.graphics.luminance
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * A soft color glow behind the screens: [color] at [alpha] in the middle, fading out over [radius]
 * (a fraction of the screen's longer side) around ([x], [y]) (fractions of its width and height).
 */
@Immutable
data class Glow(val color: Color, val alpha: Float, val x: Float, val y: Float, val radius: Float)

/** What the glass panels sit over: soft glows, Retrowave's sunset, or the XMB waves. */
enum class GlassScene { GLOWS, SUNSET, WAVES }

/**
 * The glass look for one theme, computed once from its final colors rather than the cross-fading
 * ones. Panels fill with `surfaceContainer` at [panelAlpha], or at [accentPanelAlpha] where they
 * also show secondary, tertiary, error, success or warning text. The floating dock, which pages
 * scroll under, fills at [dockAlpha], so its labels hold up over any color. A white gloss from the
 * top left peaks at [glossAlpha] and a shade toward black at the bottom right at [shadeAlpha];
 * panels cast a black shadow at [shadowAlpha] (none over the sunset, where Retrowave's neon edge
 * takes its place, or over the XMB waves). The dock's selection pill is `primary` at
 * [indicatorAlpha]. [glows] sit behind the screens, at zero alpha over the sunset or the waves,
 * which draw their own scenes. Each value is the
 * strongest (or, for the panels, the most see-through) that keeps text at WCAG AA against every
 * backdrop it can end up over. [level] is the user's Glass effects setting: Subtle scales every
 * strength down and raises the panel floor, and Off makes panels and the dock opaque with no
 * glows, gloss, shade or shadow. [rimStrength] scales the rim highlight.
 *
 * Depth: a tight contact shadow at [contactShadowAlpha] grounds each panel under its softer
 * shadow, and a bevel along the inside of the edge, light toward the top left at [bevelLight] and
 * dark toward the bottom right at [bevelShade], gives the glass visible thickness. Both stay in the
 * band along a panel's edge that text keeps clear of, so they don't enter the contrast searches.
 * At [refraction] above zero (the Full level, Android 13 and later), the dock draws the pages and
 * backdrop behind it through a lens that bends them within that same band, where the tint thins.
 * Over the sunset or the waves, headers and other text drawn straight on the scene sit on a soft
 * plate of the background at [backdropPlateAlpha], the least that keeps them at WCAG AA over all of it.
 */
@Immutable
data class GlassStyle(
    val panelAlpha: Float,
    val accentPanelAlpha: Float,
    val dockAlpha: Float,
    val glossAlpha: Float,
    val shadeAlpha: Float,
    val shadowAlpha: Float,
    val indicatorAlpha: Float,
    val glows: List<Glow>,
    val level: GlassEffects = GlassEffects.FULL,
    val rimStrength: Float = 1f,
    val contactShadowAlpha: Float = 0f,
    val bevelLight: Float = 0f,
    val bevelShade: Float = 0f,
    val refraction: Float = 0f,
    val backdropPlateAlpha: Float = 0f,
) {
    companion object {
        fun of(
            scheme: ColorScheme,
            success: Color,
            warning: Color,
            isDark: Boolean,
            scene: GlassScene,
            level: GlassEffects = GlassEffects.FULL,
        ): GlassStyle = GlassBuilder(scheme, success, warning, isDark, scene, level).build()
    }
}

/**
 * Searches the glass values for one theme. Glow colors take the accent's hue at a strong,
 * gamut-limited chroma and about the background's luminance, so they read as jewel tones on dark
 * themes and pastels on light ones without changing how bright the backdrop is; the searches then
 * account for what compositing still shifts.
 */
internal class GlassBuilder(
    private val scheme: ColorScheme,
    success: Color,
    warning: Color,
    private val isDark: Boolean,
    private val scene: GlassScene,
    private val level: GlassEffects,
) {
    private val strength = if (level == GlassEffects.SUBTLE) Strength.SUBTLE else Strength.FULL

    /** Text on most panels: list rows, labels and links. */
    private val bodyText = Luminances(listOf(scheme.onSurface, scheme.onSurfaceVariant, scheme.primary))

    /** Text on panels that also use the other accents (the log console, the About card). */
    private val accentText = Luminances(
        listOf(
            scheme.onSurface, scheme.onSurfaceVariant, scheme.primary, scheme.secondary, scheme.tertiary,
            scheme.error, success, warning,
        ),
    )

    /** Text drawn straight on the backdrop: screen titles, section headers, subtitles, notes and empty states. */
    private val screenText = Luminances(listOf(scheme.onBackground, scheme.onSurfaceVariant, scheme.primary))

    /** The dock's tab labels. */
    private val dockText = Luminances(listOf(scheme.onSurface, scheme.onSurfaceVariant))

    private val primaryIcon = Luminances(listOf(scheme.primary))

    fun build(): GlassStyle {
        if (level == GlassEffects.OFF) {
            val glows = designedGlows().map { it.copy(alpha = 0f) }
            return GlassStyle(
                1f, 1f, 1f, 0f, 0f, 0f, indicator(dockAlpha = 1f, listOf<(Color) -> Color>({ it })), glows, level, rimStrength = 0f,
                backdropPlateAlpha = backdropPlate(),
            )
        }
        val glows = if (scene == GlassScene.GLOWS) fitGlows() else designedGlows().map { it.copy(alpha = 0f) }
        val screenBackdrops = glowBackdrops(glows)
        val shadowAlpha = if (scene != GlassScene.GLOWS) 0f else strongest(strength.shadow * if (isDark) DARK_SHADOW else LIGHT_SHADOW) { alpha ->
            screenBackdrops.all { screenText.pass(over(Color.Black, alpha * SHADOW_REACH, it), TEXT_CONTRAST) }
        }
        val panelBackdrops = when (scene) {
            GlassScene.GLOWS -> screenBackdrops + screenBackdrops.map { over(Color.Black, shadowAlpha * SHADOW_REACH, it) }
            GlassScene.SUNSET -> sunsetBackdrops(SunsetColors(scheme, isDark))
            GlassScene.WAVES -> waveScene(WaveColors(scheme, isDark))
        }
        val panelAlpha = mostSeeThrough(bodyText, panelBackdrops)
        val accentPanelAlpha = max(panelAlpha, mostSeeThrough(accentText, panelBackdrops))
        val fills = panelBackdrops.map { panel(panelAlpha, it) }
        val accentFills = panelBackdrops.map { panel(accentPanelAlpha, it) }
        fun overlayPasses(overlay: Color) =
            fills.all { bodyText.pass(over(overlay, it), TEXT_CONTRAST) } &&
                accentFills.all { accentText.pass(over(overlay, it), TEXT_CONTRAST) }
        val gloss = strongest(strength.gloss * if (isDark) DARK_GLOSS else LIGHT_GLOSS) { overlayPasses(Color.White.copy(alpha = it)) }
        val shade = strongest(strength.shade * if (isDark) DARK_SHADE else LIGHT_SHADE) { overlayPasses(Color.Black.copy(alpha = it)) }
        // The dock's fill as its labels, and the selected icon on a full-strength pill, see it:
        // plain, at the gloss's peak and at the shade's. Anything can be under it, the Retrowave
        // neon halo included.
        val sheens = listOf<(Color) -> Color>({ it }, { over(Color.White, gloss, it) }, { over(Color.Black, shade, it) })
        val dockAlpha = (strength.panelFloor..100).firstOrNull { step ->
            sheens.all { sheen ->
                passesOverAnything(dockText, TEXT_CONTRAST) { sheen(panel(step / 100f, it)) } &&
                    passesOverAnything(primaryIcon, ICON_CONTRAST) { over(scheme.primary, INDICATOR, sheen(panel(step / 100f, it))) }
            }
        }?.let { it / 100f } ?: 1f
        return GlassStyle(
            panelAlpha, accentPanelAlpha, dockAlpha, gloss, shade, shadowAlpha, indicator(dockAlpha, sheens), glows, level, strength.rim,
            contactShadowAlpha = if (scene == GlassScene.SUNSET) 0f else strength.shadow * if (isDark) DARK_CONTACT else LIGHT_CONTACT,
            bevelLight = strength.bevel * if (isDark) DARK_BEVEL_LIGHT else LIGHT_BEVEL_LIGHT,
            bevelShade = strength.bevel * if (isDark) DARK_BEVEL_SHADE else LIGHT_BEVEL_SHADE,
            refraction = if (level == GlassEffects.FULL) 1f else 0f,
            backdropPlateAlpha = backdropPlate(),
        )
    }

    /** The background's alpha behind text on the sunset or the waves: the least that keeps screen text at AA over all of it. */
    private fun backdropPlate(): Float {
        val colors = when (scene) {
            GlassScene.GLOWS -> return 0f
            GlassScene.SUNSET -> sunsetScene(SunsetColors(scheme, isDark))
            GlassScene.WAVES -> waveScene(WaveColors(scheme, isDark))
        }
        return (0..100).firstOrNull { step -> colors.all { screenText.pass(over(scheme.background, step / 100f, it), TEXT_CONTRAST) } }
            ?.let { it / 100f } ?: 1f
    }

    /**
     * The XMB scene's colors: its gradient, and across the band the ribbons reach, each point under
     * one to [WaveColors.MAX_OVERLAP] ribbons, with a crest's glow and line on top.
     */
    private fun waveScene(waves: WaveColors): List<Color> {
        fun gradient(from: Color, to: Color) = GLOW_SAMPLES.map { over(to, it, from) } + from
        val sky = gradient(waves.top, waves.middle) + gradient(waves.middle, waves.bottom)
        val band = WaveColors.BAND
        val underRibbons = (0..BAND_SAMPLES).map { Color(waves.skyAt(band.start + (band.endInclusive - band.start) * it / BAND_SAMPLES).toArgb()) }
        val lit = underRibbons.flatMap { under ->
            (1..WaveColors.MAX_OVERLAP).runningFold(under) { color, _ -> over(waves.ribbon, color) }.flatMap { ribbons ->
                val glow = over(waves.crestGlow, ribbons)
                listOf(ribbons, glow, over(waves.crest, glow))
            }
        }
        return (sky + lit).distinct()
    }

    /** The least opaque panel alpha from the floor up (in hundredths) at which [text] passes over every backdrop. */
    private fun mostSeeThrough(text: Luminances, backdrops: List<Color>): Float =
        (strength.panelFloor..100).firstOrNull { step -> backdrops.all { text.pass(panel(step / 100f, it), TEXT_CONTRAST) } }
            ?.let { it / 100f } ?: 1f

    /**
     * The strongest selection pill that keeps the selected icon visible on the dock, filled at
     * [dockAlpha] and seen through each of [sheens], whatever scrolls under it.
     */
    private fun indicator(dockAlpha: Float, sheens: List<(Color) -> Color>): Float = strongest(INDICATOR) { alpha ->
        sheens.all { sheen -> passesOverAnything(primaryIcon, ICON_CONTRAST) { over(scheme.primary, alpha, sheen(panel(dockAlpha, it))) } }
    }

    /**
     * Whether [text] passes against [surface] laid over any color at all. Compositing is monotonic
     * in each channel, so the results over black and over white bound every other: both must pass
     * and sit on the same side of each text color's luminance.
     */
    private fun passesOverAnything(text: Luminances, minimum: Float, surface: (Color) -> Color): Boolean {
        val dark = surface(Color.Black)
        val light = surface(Color.White)
        return text.pass(dark, minimum) && text.pass(light, minimum) && text.sameSide(dark, light)
    }

    private fun designedGlows(): List<Glow> {
        val peak = strength.glow * if (isDark) DARK_GLOW else LIGHT_GLOW
        return GLOW_SPOTS.map { (accent, spot) -> Glow(glowColor(accent(scheme)), peak, spot.x, spot.y, spot.radius) }
    }

    /** The glows at their design strength, scaled down until screen text passes wherever they overlap. */
    private fun fitGlows(): List<Glow> {
        val designed = designedGlows()
        val scale = (100 downTo 0).first { step ->
            val scaled = designed.map { it.copy(alpha = it.alpha * step / 100f) }
            glowBackdrops(scaled).all { screenText.pass(it, TEXT_CONTRAST) } || step == 0
        } / 100f
        return designed.map { it.copy(alpha = it.alpha * scale) }
    }

    /**
     * A color with the accent's hue near the background's luminance, as colorful as sRGB allows
     * there. Near-black backgrounds leave no room for color, so dark themes aim a little brighter;
     * light themes aim a little darker, so a white background still gets a visible pastel.
     */
    private fun glowColor(accent: Color): Color {
        val tone = Oklab.of(accent)
        val vividness = GLOW_VIVIDNESS * min(1f, tone.chroma / GREY_ACCENT)
        val background = scheme.background.luminance()
        val target = if (isDark) max(background, DARK_GLOW_LUMINANCE) else min(background, LIGHT_GLOW_LUMINANCE)
        fun at(l: Float) = Oklab.lch(l, vividness * Oklab.maxChroma(l, tone.hue), tone.hue).toColor()
        var dark = 0f
        var light = 1f
        repeat(24) {
            val mid = (dark + light) / 2f
            if (at(mid).luminance() < target) dark = mid else light = mid
        }
        return at(dark)
    }

    /**
     * Colors the backdrop takes under panels and text: the background, then every combination of
     * glows, in the order they are drawn, each at points across its falloff.
     */
    private fun glowBackdrops(glows: List<Glow>): List<Color> =
        glows.fold(listOf(scheme.background)) { layers, glow ->
            layers.flatMap { under -> listOf(under) + GLOW_SAMPLES.map { over(glow.color, glow.alpha * it, under) } }
        }.distinct()

    /** The sunset's colors (see [sunsetScene]), each also under the inner half of a card's neon halo. */
    private fun sunsetBackdrops(sunset: SunsetColors): List<Color> =
        sunsetScene(sunset).flatMap { listOf(it, over(scheme.primary, NEON_HALO / 2f, it), over(scheme.primary, NEON_HALO, it)) }.distinct()

    /** The sunset's colors, including its brightest parts: the sun, its glow, the horizon line and stars. */
    private fun sunsetScene(sunset: SunsetColors): List<Color> {
        // Gradients blend in sRGB, where brightness can dip between the ends, so they are sampled along the way.
        fun gradient(from: Color, to: Color) = GLOW_SAMPLES.map { over(to, it, from) } + from
        val sky = gradient(sunset.skyTop, sunset.skyMiddle) + gradient(sunset.skyMiddle, sunset.skyBottom)
        val glowAtSun = gradient(sunset.skyBottom, over(sunset.sunGlow.copy(alpha = 1f), sunset.sunGlow.alpha, sunset.skyBottom))
        val sun = gradient(sunset.sunTop, sunset.sunBottom).flatMap { color -> glowAtSun.map { over(color, sunset.sunAlpha, it) } }
        val floor = gradient(sunset.floorTop, sunset.floorBottom)
        val lines = floor.map { over(sunset.grid, it) } +
            listOf(sunset.skyBottom, sunset.floorTop).flatMap { listOf(over(sunset.horizonGlow, it), over(sunset.horizonLine, it)) }
        val stars = if (sunset.isDark) listOf(over(sunset.star, sunset.starMaxAlpha, sunset.skyTop)) else emptyList()
        return (sky + glowAtSun + sun + floor + lines + stars).distinct()
    }

    private fun panel(alpha: Float, backdrop: Color) = over(scheme.surfaceContainer, alpha, backdrop)

    /** [color] at [alpha] over [backdrop], rounded to 8 bits per channel as each layer is in the frame buffer. */
    private fun over(color: Color, alpha: Float, backdrop: Color) = over(color.copy(alpha = alpha), backdrop)

    private fun over(color: Color, backdrop: Color) = Color(color.compositeOver(backdrop).toArgb())

    /** The largest of [max] and the values below it (in hundredths) that passes [check], or 0. */
    private fun strongest(max: Float, check: (Float) -> Boolean): Float {
        var step = (max * 100).roundToInt()
        while (step > 0 && !check(step / 100f)) step--
        return step / 100f
    }

    /** Text colors' WCAG luminances, computed once for the many backdrops they are checked against. */
    private class Luminances(colors: List<Color>) {
        private val values = colors.map { it.luminance() }

        fun pass(background: Color, minimum: Float): Boolean {
            val b = background.luminance()
            return values.all { t -> (max(t, b) + 0.05f) / (min(t, b) + 0.05f) >= minimum + MARGIN }
        }

        /** Whether [a] and [b] are both darker, or both lighter, than each of these colors. */
        fun sameSide(a: Color, b: Color): Boolean {
            val la = a.luminance()
            val lb = b.luminance()
            return values.all { t -> (la - t) * (lb - t) > 0f }
        }
    }

    private class Spot(val x: Float, val y: Float, val radius: Float)

    /** Per-level panel floor (in hundredths) and multipliers on the design strengths below. */
    private enum class Strength(val panelFloor: Int, val glow: Float, val gloss: Float, val shade: Float, val shadow: Float, val rim: Float, val bevel: Float) {
        FULL(60, 1f, 1f, 1f, 1f, 1f, 1f),
        SUBTLE(80, 0.45f, 0.5f, 0.5f, 0.6f, 0.5f, 0.5f),
    }

    private companion object {
        const val TEXT_CONTRAST = 4.5f
        const val ICON_CONTRAST = 3f

        /** Covers rounding differences and the one-step dithering of drawn gradients. */
        const val MARGIN = 0.05f

        const val DARK_GLOSS = 0.08f
        const val LIGHT_GLOSS = 0.35f
        const val DARK_SHADE = 0.2f
        const val LIGHT_SHADE = 0.06f
        const val DARK_SHADOW = 0.4f
        const val LIGHT_SHADOW = 0.14f

        /**
         * Share of a panel's shadow that can reach text or a neighboring panel. The shadow is offset
         * 4 dp down with a 12 dp blur, and text and neighbors sit at least 8 dp from a panel's
         * edges, 12 dp from the shadow's, where the blur leaves under 5% of its peak.
         */
        const val SHADOW_REACH = 0.1f

        /** The contact shadow's peak; it is offset 1.5 dp with a 3 dp blur, so it never reaches text or a neighbor. */
        const val DARK_CONTACT = 0.35f
        const val LIGHT_CONTACT = 0.12f

        /** The bevel's peaks; it reaches about 6 dp into a panel, short of the 8 dp text keeps from the edge. */
        const val DARK_BEVEL_LIGHT = 0.16f
        const val LIGHT_BEVEL_LIGHT = 0.7f
        const val DARK_BEVEL_SHADE = 0.4f
        const val LIGHT_BEVEL_SHADE = 0.1f

        const val INDICATOR = 0.24f
        const val DARK_GLOW = 0.5f
        const val LIGHT_GLOW = 0.55f
        const val GLOW_VIVIDNESS = 0.85f
        const val DARK_GLOW_LUMINANCE = 0.03f
        const val LIGHT_GLOW_LUMINANCE = 0.78f

        /** Peak alpha of a card's neon halo (`neonGlow` at the cards' 0.45 intensity). */
        const val NEON_HALO = 0.55f * 0.45f

        /** Accents greyer than this (in Oklab chroma) give proportionally greyer glows. */
        const val GREY_ACCENT = 0.06f

        /** Points along a glow's falloff, or a gradient, at which the backdrop is checked. */
        val GLOW_SAMPLES = listOf(0.25f, 0.5f, 0.75f, 1f)

        /** Steps across the band the XMB ribbons reach at which the gradient under them is checked. */
        const val BAND_SAMPLES = 4

        val GLOW_SPOTS: List<Pair<(ColorScheme) -> Color, Spot>> = listOf(
            { s: ColorScheme -> s.primary } to Spot(0.12f, 0f, 0.75f),
            { s: ColorScheme -> s.tertiary } to Spot(1f, 0.45f, 0.6f),
            { s: ColorScheme -> s.secondary } to Spot(0.25f, 1.05f, 0.7f),
        )
    }
}
