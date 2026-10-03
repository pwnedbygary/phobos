package com.phobos.emulator.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import com.google.android.material.color.utilities.Hct
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.XmbBackdropScene
import com.google.android.material.color.utilities.SchemeTonalSpot
import com.google.android.material.color.utilities.TonalPalette
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * WCAG checks for the glass look, measured on the composited 8-bit colors the screen shows rather
 * than on opaque roles. Covers every palette of every registered theme plus Material You schemes
 * built the way Android 12 and 13 build them (Material's tonal-spot palettes from 24 seed hues,
 * mapped to roles at the tones Compose Material 3 1.2.1 uses), each over every scene (the glows, the
 * aurora and the mesh, Retrowave's sunset and night city, and the XMB waves and deep waves), at each Glass effects level.
 * Backdrops are sampled more finely than the app's own search: every glow at every tenth of its strength in every
 * combination, and the drawn scenes across their gradients. The dock, which pages scroll under, is checked over greys and
 * hues across the whole range rather than over the backdrop alone.
 */
class GlassContrastTest {

    private class Variant(val name: String, val scheme: ColorScheme, val success: Color, val warning: Color, val isDark: Boolean) {
        fun style(scene: GlassScene, level: GlassEffects) = GlassStyle.of(scheme, success, warning, isDark, scene, level)
    }

    private class Check(val label: String, val foreground: Int, val background: Int, val minimum: Double)

    private val variants: List<Variant> = ThemeRegistry.all.flatMap { theme ->
        listOfNotNull(
            theme.dark?.let { theme.colors(dark = true).let { c -> Variant("${theme.name} (dark)", c.scheme, c.success, c.warning, true) } },
            theme.light?.let { theme.colors(dark = false).let { c -> Variant("${theme.name} (light)", c.scheme, c.success, c.warning, false) } },
        )
    } + (0 until 360 step 15).flatMap { hue ->
        val seed = Hct.from(hue.toDouble(), 48.0, 50.0).toInt()
        listOf(true, false).map { dark ->
            val colors = SchemeBuilder.withStatusColors(materialYou(seed, dark), dark)
            Variant("Material You, seed hue $hue (${if (dark) "dark" else "light"})", colors.scheme, colors.success, colors.warning, dark)
        }
    }

    @Test
    fun panelTextReachesAaOverEveryBackdrop() = assertAll { v, scene, level ->
        val s = v.scheme
        val style = v.style(scene, level)
        val body = listOf("onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
        val accents = body + listOf(
            "secondary" to s.secondary, "tertiary" to s.tertiary, "error" to s.error,
            "success" to v.success, "warning" to v.warning,
        )
        val backdrops = panelBackdrops(v, style, scene)
        listOf("panel" to (style.panelAlpha to body), "accent panel" to (style.accentPanelAlpha to accents)).flatMap { (kind, pair) ->
            val (alpha, text) = pair
            backdrops.flatMap { backdrop ->
                val fill = composite(s.surfaceContainer.toArgb(), backdrop, paint(alpha))
                val lit = listOf(
                    "" to fill,
                    " under the gloss" to composite(WHITE, fill, paint(style.glossAlpha)),
                    " under the shade" to composite(BLACK, fill, paint(style.shadeAlpha)),
                )
                lit.flatMap { (where, background) ->
                    text.map { (role, color) -> Check("$role on a $kind$where over ${hex(backdrop)}", color.toArgb(), background, 4.5) }
                }
            }
        }
    }

    @Test
    fun libraryTileNamesReachAaOverEveryBackdrop() = assertAll { v, scene, level ->
        val style = v.style(scene, level)
        val tile = libraryTileFill(v.scheme, v.isDark).toArgb()
        val name = v.scheme.onSurface.toArgb()
        panelBackdrops(v, style, scene).flatMap { backdrop ->
            val fill = composite(tile, backdrop, paint(style.panelAlpha))
            listOf(
                "" to fill,
                " under the gloss" to composite(WHITE, fill, paint(style.glossAlpha)),
                " under the shade" to composite(BLACK, fill, paint(style.shadeAlpha)),
            ).map { (where, background) -> Check("onSurface on a Library tile$where over ${hex(backdrop)}", name, background, 4.5) }
        }
    }

    @Test
    fun screenTextReachesAaOverTheGlows() = assertAll { v, scene, level ->
        if (scene != GlassScene.GLOWS) return@assertAll emptyList()
        val s = v.scheme
        val style = v.style(GlassScene.GLOWS, level)
        val text = listOf("onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
        glowBackdrops(v, style).flatMap { backdrop ->
            listOf(backdrop, composite(BLACK, backdrop, style.shadowAlpha * SHADOW_REACH)).flatMap { background ->
                text.map { (role, color) -> Check("$role on the backdrop ${hex(background)}", color.toArgb(), background, 4.5) }
            }
        }
    }

    @Test
    fun sceneTextReachesAaOnItsPlate() = assertAll { v, scene, level ->
        if (scene == GlassScene.GLOWS) return@assertAll emptyList()
        val s = v.scheme
        val style = v.style(scene, level)
        val text = listOf("onBackground" to s.onBackground, "onSurfaceVariant" to s.onSurfaceVariant, "primary" to s.primary)
        sceneColors(v, scene).flatMap { backdrop ->
            val plate = composite(s.background.toArgb(), backdrop, paint(style.backdropPlateAlpha))
            text.map { (role, color) -> Check("$role on the backdrop plate over ${hex(backdrop)}", color.toArgb(), plate, 4.5) }
        }
    }

    @Test
    fun topBarStandsOutFromTheAurora() {
        // The top bar is see-through until content scrolls under it: its title and icons, a checked one in primary, (3:1) over the aurora's upper bands.
        val failures = variants.flatMap { v ->
            val stops = AuroraColors(v.scheme, v.isDark).stops.map(::argb)
            val upper = (0..8).map { lerp(stops[0], stops[1], it / 8.0) }
            listOf("onSurface" to v.scheme.onSurface, "onSurfaceVariant" to v.scheme.onSurfaceVariant, "primary" to v.scheme.primary).flatMap { (role, color) ->
                upper.mapNotNull { backdrop ->
                    val ratio = contrast(color.toArgb(), backdrop)
                    if (ratio >= 3.0) null else "${v.name}: $role in the top bar over the aurora ${hex(backdrop)} is ${fmt(ratio)}:1, needs 3.0:1"
                }
            }
        }
        assertTrue("${failures.size} failures\n" + failures.take(25).joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun dockLabelsReachAaOverAnythingScrollingUnder() = assertAll { v, scene, level ->
        val s = v.scheme
        val labels = listOf("onSurface" to s.onSurface, "onSurfaceVariant" to s.onSurfaceVariant)
        dockFills(v, v.style(scene, level), scene).flatMap { (where, fill) ->
            labels.map { (role, color) -> Check("$role on the dock$where", color.toArgb(), fill, 4.5) }
        }
    }

    @Test
    fun dockSelectionKeepsItsIconVisible() = assertAll { v, scene, level ->
        val style = v.style(scene, level)
        val primary = v.scheme.primary.toArgb()
        dockFills(v, style, scene).map { (where, fill) ->
            Check("primary icon on the dock's pill$where", primary, composite(primary, fill, paint(style.indicatorAlpha)), 3.0)
        }
    }

    @Test
    fun glowsStayNearTheBackgroundsLuminance() {
        val failures = variants.flatMap { v ->
            val background = luminance(v.scheme.background.toArgb())
            v.style(GlassScene.GLOWS, GlassEffects.FULL).glows.mapNotNull { glow ->
                val l = luminance(glow.color.toArgb())
                val ceiling = if (v.isDark) max(background, DARK_GLOW_LUMINANCE) else background
                if (l <= ceiling + LUMINANCE_ROUNDING) null
                else "${v.name}: glow ${hex(glow.color.toArgb())} has luminance ${fmt(l)}, above ${fmt(ceiling)}"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun valuesStayInRange() {
        val failures = variants.flatMap { v ->
            GlassScene.entries.flatMap { scene -> GlassEffects.entries.map { scene to it } }.mapNotNull { (scene, level) ->
                val style = v.style(scene, level)
                val floor = when (level) {
                    GlassEffects.FULL -> 0.6f
                    GlassEffects.SUBTLE -> 0.8f
                    GlassEffects.OFF -> 1f
                }
                val flat = level != GlassEffects.OFF ||
                    (style.glossAlpha == 0f && style.shadeAlpha == 0f && style.shadowAlpha == 0f && style.glows.all { it.alpha == 0f })
                val problems = listOfNotNull(
                    "panel alpha ${style.panelAlpha} below the $level floor".takeUnless { style.panelAlpha in floor..1f },
                    "dock alpha ${style.dockAlpha} below the $level floor".takeUnless { style.dockAlpha in floor..1f },
                    "gloss, shade, shadow or glows with glass effects off".takeUnless { flat },
                    "accent panel alpha ${style.accentPanelAlpha} below the panel's".takeUnless { style.accentPanelAlpha in style.panelAlpha..1f },
                    "glows drawn over the ${label(scene)}".takeUnless { scene == GlassScene.GLOWS || style.glows.all { it.alpha == 0f } },
                    "shadow drawn over the ${label(scene)}".takeUnless { scene == GlassScene.GLOWS || style.shadowAlpha == 0f },
                    "backdrop plate over the glows".takeUnless { scene != GlassScene.GLOWS || style.backdropPlateAlpha == 0f },
                )
                if (problems.isEmpty()) null else "${v.name} over the ${label(scene)}, $level: ${problems.joinToString()}"
            }
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /**
     * Colors that can scroll under the dock: greys across the whole range, and saturated and
     * pastel primaries and secondaries at several strengths.
     */
    private val anyContent: List<Int> = run {
        val greys = (0..255 step 5).map { (0xFF shl 24) or (it shl 16) or (it shl 8) or it }
        val hues = listOf(0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF, 0xFF00FF, 0xFFFF00).map { it or (0xFF shl 24) }
        val strengths = (1..4).map { it / 4.0 }
        (greys + hues.flatMap { hue -> strengths.flatMap { t -> listOf(composite(hue, BLACK, t), composite(hue, WHITE, t)) } }).distinct()
    }

    /**
     * The dock's fill, plain and at its gloss's and shade's peaks, over everything that can scroll
     * under it; with Retrowave effects also through the neon halo at every strength.
     */
    private fun dockFills(v: Variant, style: GlassStyle, scene: GlassScene): List<Pair<String, Int>> {
        val primary = v.scheme.primary.toArgb()
        val container = v.scheme.surfaceContainer.toArgb()
        val retrowave = scene == GlassScene.SUNSET || scene == GlassScene.CITY
        val under = if (retrowave) anyContent.flatMap { c -> (0..4).map { composite(primary, c, NEON_HALO * it / 4) } }.distinct() else anyContent
        return under.flatMap { content ->
            val fill = composite(container, content, paint(style.dockAlpha))
            listOf(
                " over ${hex(content)}" to fill,
                " under the gloss over ${hex(content)}" to composite(WHITE, fill, paint(style.glossAlpha)),
                " under the shade over ${hex(content)}" to composite(BLACK, fill, paint(style.shadeAlpha)),
            )
        }
    }

    /** Colors under a panel: the glows and a neighboring panel's shadow where it reaches, or a drawn scene, Retrowave's under the neon halo. */
    private fun panelBackdrops(v: Variant, style: GlassStyle, scene: GlassScene): List<Int> = when (scene) {
        GlassScene.GLOWS -> glowBackdrops(v, style).flatMap { listOf(it, composite(BLACK, it, style.shadowAlpha * SHADOW_REACH)) }.distinct()
        GlassScene.SUNSET, GlassScene.CITY -> withNeonHalo(v, sceneColors(v, scene))
        else -> sceneColors(v, scene)
    }

    /** A drawn scene's colors, which text drawn straight on it sees through its plate. */
    private fun sceneColors(v: Variant, scene: GlassScene): List<Int> = when (scene) {
        GlassScene.GLOWS -> error("the glows are fitted, not drawn")
        GlassScene.AURORA -> auroraScene(v)
        GlassScene.MESH -> meshScene(v)
        GlassScene.SUNSET -> sunsetScene(v)
        GlassScene.CITY -> cityScene(v)
        GlassScene.WAVES -> waveScene(v, XmbBackdropScene.WAVES)
        GlassScene.DEEP -> waveScene(v, XmbBackdropScene.DEEP)
    }

    /** The aurora's gradient, at every eighth of the way between its stops. */
    private fun auroraScene(v: Variant): List<Int> {
        val steps = (0..8).map { it / 8.0 }
        return AuroraColors(v.scheme, v.isDark).stops.map(::argb).zipWithNext().flatMap { (a, b) -> steps.map { lerp(a, b, it) } }.distinct()
    }

    /** The background, and the mesh's lines at every eighth of their strength, as antialiasing blends their edges into it. */
    private fun meshScene(v: Variant): List<Int> {
        val background = v.scheme.background.toArgb()
        val line = argb(meshLine(v.scheme, v.isDark))
        return (0..8).map { lerp(background, line, it / 8.0) }.distinct()
    }

    /**
     * The XMB waves' gradient, and across the band the ribbons reach, every point under none to all of
     * the ribbons, plain, under a crest's glow, and under its glow and line.
     */
    private fun waveScene(v: Variant, scene: XmbBackdropScene): List<Int> {
        val waves = WaveColors(v.scheme, v.isDark, scene)
        fun layer(color: Color, under: Int) = composite(argb(color), under, paint(color.alpha))
        val steps = (0..8).map { it / 8.0 }
        val sky = listOf(waves.top, waves.middle, waves.bottom).map(::argb).zipWithNext().flatMap { (a, b) -> steps.map { lerp(a, b, it) } }
        val band = WaveColors.BAND
        val underRibbons = (0..16).map { argb(waves.skyAt(band.start + (band.endInclusive - band.start) * it / 16f)) }
        val lit = underRibbons.flatMap { under ->
            (1..WaveColors.MAX_OVERLAP).runningFold(under) { color, _ -> layer(waves.ribbon, color) }.flatMap { ribbons ->
                val glow = layer(waves.crestGlow, ribbons)
                listOf(ribbons, glow, layer(waves.crest, glow))
            }
        }
        return (sky + lit).distinct()
    }

    /** The background under every combination of glows, each at every tenth of its strength, in drawing order. */
    private fun glowBackdrops(v: Variant, style: GlassStyle): List<Int> =
        style.glows.fold(listOf(v.scheme.background.toArgb())) { layers, glow ->
            layers.flatMap { under -> (0..10).map { composite(glow.color.toArgb(), under, glow.alpha * it / 10.0) } }.distinct()
        }

    /** Retrowave's [colors], each also under a card's neon halo at every strength. */
    private fun withNeonHalo(v: Variant, colors: List<Int>): List<Int> {
        val primary = v.scheme.primary.toArgb()
        val steps = (0..4).map { it / 4.0 }
        return colors.flatMap { under -> steps.map { composite(primary, under, NEON_HALO * it) } }.distinct()
    }

    /** The Retrowave sunset across its gradients: the sky, the sun and its glow, the floor, grid, horizon and stars. */
    private fun sunsetScene(v: Variant): List<Int> {
        val sunset = SunsetColors(v.scheme, v.isDark)
        val steps = (0..4).map { it / 4.0 }
        val glowAtSun = (0..6).map { composite(argb(sunset.sunGlow), argb(sunset.skyBottom), sunset.sunGlow.alpha * it / 6.0) }
        val sun = steps.flatMap { t ->
            val color = lerp(argb(sunset.sunTop), argb(sunset.sunBottom), t)
            glowAtSun.map { composite(color, it, sunset.sunAlpha.toDouble()) }
        }
        return (sunsetSky(sunset) + glowAtSun + sun + sunsetGround(sunset)).distinct()
    }

    /**
     * Night city: the sunset's sky, floor, grid and horizon without the sun; the buildings; the horizon's
     * glow at every strength over their feet, the sky and the floor, with its line on each; stars at every
     * strength all the way down the sky in dark themes; and the antennas against the sky.
     */
    private fun cityScene(v: Variant): List<Int> {
        val sunset = SunsetColors(v.scheme, v.isDark)
        val steps = (0..4).map { it / 4.0 }
        val sky = sunsetSky(sunset)
        val horizon = listOf(sunset.silhouette, sunset.skyBottom, sunset.floorTop).map(::argb).flatMap { base ->
            val glows = steps.map { composite(argb(sunset.horizonGlow), base, sunset.horizonGlow.alpha * it) }
            glows + glows.map { composite(argb(sunset.horizonLine), it, sunset.horizonLine.alpha.toDouble()) }
        }
        val stars = if (sunset.isDark) sky.flatMap { under -> steps.map { composite(argb(sunset.star), under, 0.15 + (sunset.starMaxAlpha - 0.15) * it) } } else emptyList()
        val antennas = sky.map { composite(argb(sunset.antenna), it, sunset.antenna.alpha.toDouble()) }
        return (sky + sunsetGround(sunset) + horizon + stars + antennas).distinct()
    }

    /** The sunset's sky across its gradient, and its stars in dark themes. */
    private fun sunsetSky(sunset: SunsetColors): List<Int> {
        val steps = (0..4).map { it / 4.0 }
        val sky = listOf(sunset.skyTop, sunset.skyMiddle, sunset.skyBottom).map(::argb).zipWithNext().flatMap { (a, b) -> steps.map { lerp(a, b, it) } }
        val stars = if (sunset.isDark) steps.map { composite(argb(sunset.star), argb(sunset.skyTop), 0.15 + (sunset.starMaxAlpha - 0.15) * it) } else emptyList()
        return sky + stars
    }

    /** The sunset's floor across its gradient, its grid at every strength, and the horizon's glow and line. */
    private fun sunsetGround(sunset: SunsetColors): List<Int> {
        val steps = (0..4).map { it / 4.0 }
        val floor = steps.map { lerp(argb(sunset.floorTop), argb(sunset.floorBottom), it) }
        val grid = floor.flatMap { under -> steps.map { composite(argb(sunset.grid), under, sunset.grid.alpha * it) } }
        val horizon = listOf(argb(sunset.skyBottom), argb(sunset.floorTop)).flatMap { under ->
            steps.map { composite(argb(sunset.horizonGlow), under, sunset.horizonGlow.alpha * it) } +
                composite(argb(sunset.horizonLine), under, sunset.horizonLine.alpha.toDouble())
        }
        return floor + grid + horizon
    }

    /** [color] made opaque, for layering at its own alpha. */
    private fun argb(color: Color) = color.copy(alpha = 1f).toArgb()

    private fun assertAll(checks: (Variant, GlassScene, GlassEffects) -> List<Check>) {
        val failures = variants.flatMap { v ->
            GlassScene.entries.flatMap { scene ->
                GlassEffects.entries.flatMap { level ->
                    checks(v, scene, level).mapNotNull { check ->
                        val ratio = contrast(check.foreground, check.background)
                        if (ratio >= check.minimum) null
                        else "${v.name} over the ${label(scene)}, $level: ${check.label} is ${fmt(ratio)}:1, needs ${check.minimum}:1"
                    }
                }
            }
        }
        assertTrue("${failures.size} failures\n" + failures.take(25).joinToString("\n"), failures.isEmpty())
    }

    /** A Material You scheme as Compose Material 3 1.2.1 builds it on Android 12 and 13, from Material's tonal-spot palettes. */
    private fun materialYou(seed: Int, dark: Boolean): ColorScheme {
        val palettes = SchemeTonalSpot(Hct.fromInt(seed), dark, 0.0)
        fun TonalPalette.at(tone: Int) = Color(tone(tone))
        val nv = palettes.neutralVariantPalette
        return if (dark) {
            darkColorScheme(
                primary = palettes.primaryPalette.at(80), secondary = palettes.secondaryPalette.at(80), tertiary = palettes.tertiaryPalette.at(80),
                background = nv.at(6), onBackground = nv.at(90), surface = nv.at(6), onSurface = nv.at(90),
                surfaceVariant = nv.at(30), onSurfaceVariant = nv.at(80), outline = nv.at(60), outlineVariant = nv.at(30),
                surfaceBright = nv.at(24), surfaceDim = nv.at(6), surfaceContainerLowest = nv.at(4), surfaceContainerLow = nv.at(10),
                surfaceContainer = nv.at(12), surfaceContainerHigh = nv.at(17), surfaceContainerHighest = nv.at(22),
            )
        } else {
            lightColorScheme(
                primary = palettes.primaryPalette.at(40), secondary = palettes.secondaryPalette.at(40), tertiary = palettes.tertiaryPalette.at(40),
                background = nv.at(98), onBackground = nv.at(10), surface = nv.at(98), onSurface = nv.at(10),
                surfaceVariant = nv.at(90), onSurfaceVariant = nv.at(30), outline = nv.at(50), outlineVariant = nv.at(80),
                surfaceBright = nv.at(98), surfaceDim = nv.at(87), surfaceContainerLowest = nv.at(100), surfaceContainerLow = nv.at(96),
                surfaceContainer = nv.at(94), surfaceContainerHigh = nv.at(92), surfaceContainerHighest = nv.at(90),
            )
        }
    }

    /** [foreground] at [alpha] over opaque [background], blended per 8-bit channel as the screen does. */
    private fun composite(foreground: Int, background: Int, alpha: Double): Int {
        fun channel(shift: Int) = ((foreground shr shift and 0xFF) * alpha + (background shr shift and 0xFF) * (1 - alpha)).roundToInt()
        return (0xFF shl 24) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }

    private fun composite(foreground: Int, background: Int, alpha: Float) = composite(foreground, background, alpha.toDouble())

    /** A style alpha as the screen applies it: Compose passes paint alpha to Android as 8 bits, rounded. */
    private fun paint(alpha: Float): Double = (alpha * 255f).roundToInt() / 255.0

    /** Gradient interpolation between two opaque colors, per 8-bit channel. */
    private fun lerp(a: Int, b: Int, t: Double) = composite(b, a, t)

    private fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    private val luminances = HashMap<Int, Double>()

    /** WCAG 2 relative luminance of an sRGB color. */
    private fun luminance(argb: Int): Double = luminances.getOrPut(argb) {
        fun channel(value: Int): Double {
            val c = value / 255.0
            return if (c <= 0.03928) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)
        }
        0.2126 * channel(argb shr 16 and 0xFF) + 0.7152 * channel(argb shr 8 and 0xFF) + 0.0722 * channel(argb and 0xFF)
    }

    private fun label(scene: GlassScene) = scene.name.lowercase(Locale.ROOT)

    private fun hex(argb: Int) = String.format(Locale.ROOT, "#%06x", argb and 0xFFFFFF)

    private fun fmt(x: Double) = String.format(Locale.ROOT, "%.2f", x)

    private companion object {
        val WHITE = 0xFFFFFFFF.toInt()
        val BLACK = 0xFF000000.toInt()

        /**
         * Share of a panel's shadow that reaches text or a neighboring panel, as the app assumes: the
         * shadow is offset 4 dp with a 12 dp blur, and both sit at least 8 dp from a panel's edges.
         */
        const val SHADOW_REACH = 0.1

        /** Peak alpha of a card's neon halo at the cards' intensity. */
        const val NEON_HALO = 0.55 * 0.45

        const val DARK_GLOW_LUMINANCE = 0.03
        const val LUMINANCE_ROUNDING = 0.005
    }
}
