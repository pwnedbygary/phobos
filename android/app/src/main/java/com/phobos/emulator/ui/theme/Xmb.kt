package com.phobos.emulator.ui.theme

import android.animation.ValueAnimator
import androidx.compose.foundation.layout.Spacer
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameMillis
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.R
import com.phobos.emulator.data.XmbBackdropScene
import kotlinx.coroutines.delay
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.sin

@OptIn(ExperimentalTextApi::class)
private fun mplus(weight: FontWeight) =
    Font(R.font.mplus1, weight, variationSettings = FontVariation.Settings(FontVariation.weight(weight.weight)))

/**
 * M PLUS 1 (SIL Open Font License 1.1; see LICENSE), a Japanese gothic like a console menu's lettering.
 * It is cut down to Latin, Greek, Cyrillic and symbols; other scripts use the system font.
 */
val XmbFont = FontFamily(
    mplus(FontWeight.Light),
    mplus(FontWeight.Normal),
    mplus(FontWeight.Medium),
    mplus(FontWeight.SemiBold),
    mplus(FontWeight.Bold),
)

/** A soft white glow around large lettering, like the menu's. */
private val TextGlow = Shadow(color = Color.White.copy(alpha = 0.35f), blurRadius = 16f)

private fun TextStyle.inXmbFont(weight: FontWeight, glow: Boolean = false): TextStyle =
    copy(fontFamily = XmbFont, fontWeight = weight, letterSpacing = 0.sp, shadow = if (glow) TextGlow else shadow)

/** The app's type scale in M PLUS 1: light, glowing headings, and a step lighter than Material's weights elsewhere. */
val XmbTypography = with(PhobosTypography) {
    Typography(
        displayLarge = displayLarge.inXmbFont(FontWeight.Light, glow = true),
        displayMedium = displayMedium.inXmbFont(FontWeight.Light, glow = true),
        displaySmall = displaySmall.inXmbFont(FontWeight.Light, glow = true),
        headlineLarge = headlineLarge.inXmbFont(FontWeight.Light, glow = true),
        headlineMedium = headlineMedium.inXmbFont(FontWeight.Light, glow = true),
        headlineSmall = headlineSmall.inXmbFont(FontWeight.Normal),
        titleLarge = titleLarge.inXmbFont(FontWeight.Normal, glow = true),
        titleMedium = titleMedium.inXmbFont(FontWeight.Medium),
        titleSmall = titleSmall.inXmbFont(FontWeight.Medium),
        bodyLarge = bodyLarge.inXmbFont(FontWeight.Normal),
        bodyMedium = bodyMedium.inXmbFont(FontWeight.Normal),
        bodySmall = bodySmall.inXmbFont(FontWeight.Normal),
        labelLarge = labelLarge.inXmbFont(FontWeight.Medium),
        labelMedium = labelMedium.inXmbFont(FontWeight.Medium),
        labelSmall = labelSmall.inXmbFont(FontWeight.Medium),
    )
}

/** The XMB [scene]'s colors, shared with the glass panels' contrast search ([GlassStyle]). */
internal class WaveColors(scheme: ColorScheme, isDark: Boolean, scene: XmbBackdropScene = XmbBackdropScene.WAVES) {
    /** Tints of the primary color at no less contrast with the text than the background has, like the glass glows. */
    val top = tintKeepingContrast(scheme.background, scheme.primary, 0.4f, isDark)
    val middle = tintKeepingContrast(scheme.background, scheme.primary, 0.2f, isDark)

    /** Deeper in dark themes, deeper still for Deep, and paler in light ones, away from the text. */
    val bottom = when {
        !isDark -> lerp(scheme.background, Color.White, 0.5f)
        scene == XmbBackdropScene.DEEP -> lerp(lerp(scheme.background, Color.Black, 0.35f), Color.Black, 0.35f)
        else -> lerp(scheme.background, Color.Black, 0.35f)
    }

    /** The ribbons', strands' and sparkles' light: white on dark themes, and the primary color on light ones, where white wouldn't show. */
    private val light = if (isDark) Color.White else scheme.primary

    /** How strongly the ribbons show: fainter when Calm. Deep draws strands instead. */
    private val strength = if (scene == XmbBackdropScene.CALM) 0.5f else 1f

    /** A ribbon's see-through fill. */
    val ribbon = light.copy(alpha = 0.06f * strength)

    /** The soft glow along each edge of a ribbon, a wide stroke under [crest]. */
    val crestGlow = light.copy(alpha = 0.05f * strength)

    /** The bright line along each edge of a ribbon. */
    val crest = light.copy(alpha = (if (isDark) 0.24f else 0.3f) * strength)

    /** Deep's strands where their sheet lies flat; they fade as it turns edge on ([Sheet.fade]). */
    val strand = light.copy(alpha = if (isDark) 0.2f else 0.24f)

    /** A sparkle drifting up through Deep's sheets, at its brightest. */
    val sparkle = light.copy(alpha = if (isDark) 0.42f else 0.5f)

    /** The soft pool of light behind Deep's sheets, at its middle. */
    val pool = light.copy(alpha = if (isDark) 0.08f else 0.1f)

    /** The gradient at [fraction] of the height, blended per sRGB channel as the screen blends it. */
    fun skyAt(fraction: Float): Color =
        if (fraction <= MIDDLE) middle.copy(alpha = fraction / MIDDLE).compositeOver(top)
        else bottom.copy(alpha = ((fraction - MIDDLE) / (1 - MIDDLE)).coerceIn(0f, 1f)).compositeOver(middle)

    companion object {
        /** Where the gradient passes through [middle]. */
        const val MIDDLE = 0.45f

        /** The most ribbons that ever lie over one point: all of [WAVES]. */
        val MAX_OVERLAP = WAVES.size

        /** The part of the height the ribbons can reach, with room for their glow. */
        val BAND: ClosedFloatingPointRange<Float> =
            WAVES.minOf { it.center - it.amplitude - it.thickness / 2 } - 0.02f..WAVES.maxOf { it.center + it.amplitude + it.thickness / 2 } + 0.02f

        /** The part of the height Deep's sheets and sparkles reach. */
        val DEEP_BAND: ClosedFloatingPointRange<Float> =
            SHEETS.minOf { it.center - it.amplitude - it.spread / 2 } - 0.03f..SHEETS.maxOf { it.center + it.amplitude + it.spread / 2 } + 0.03f

        /**
         * The most of [strand]'s alpha Deep's strands lay over one point, as a multiple: each sheet is drawn as one
         * path, so its own strands don't add up where they gather, and the two sheets can cross.
         */
        val STRAND_STACK: Float = SHEETS.size.toFloat()
    }
}

/**
 * A ribbon of light across the screen: its middle swings [amplitude] (of the height) around [center]
 * along a wave [length] (of the width) long, and it twists, thinning to a quarter of [thickness] and
 * back, along [twist]. It moves [speed] and twists [twistSpeed] wavelengths per [WAVE_PERIOD_MS], whole
 * numbers so the loop is seamless.
 */
private class Wave(
    val center: Float,
    val amplitude: Float,
    val length: Float,
    val speed: Int,
    val thickness: Float,
    val twist: Float,
    val twistSpeed: Int,
    val offset: Float,
) {
    /** Traces the ribbon at [time] (0 to 1 through the period) into [fill], and its edges into [upper] and [lower]. */
    fun trace(size: Size, time: Float, fill: Path, upper: Path, lower: Path) {
        fill.reset()
        upper.reset()
        lower.reset()
        val steps = WAVE_STEPS
        val tops = FloatArray(steps + 1)
        val bottoms = FloatArray(steps + 1)
        for (i in 0..steps) {
            val u = i.toFloat() / steps
            val middle = size.height * (center + amplitude * sin(2 * PI * (u / length + speed * time + offset)).toFloat())
            val swell = 0.5f + 0.5f * sin(2 * PI * (u / twist + twistSpeed * time + offset * 2)).toFloat()
            val half = size.height * thickness / 2f * (0.25f + 0.75f * swell)
            tops[i] = middle - half
            bottoms[i] = middle + half
        }
        for (i in 0..steps) {
            val x = size.width * i / steps
            if (i == 0) {
                fill.moveTo(x, tops[i])
                upper.moveTo(x, tops[i])
                lower.moveTo(x, bottoms[i])
            } else {
                fill.lineTo(x, tops[i])
                upper.lineTo(x, tops[i])
                lower.lineTo(x, bottoms[i])
            }
        }
        for (i in steps downTo 0) fill.lineTo(size.width * i / steps, bottoms[i])
        fill.close()
    }
}

private const val WAVE_STEPS = 64
private const val WAVE_PERIOD_MS = 40_000L
/** The wait between wave frames, which with the wait for the next vsync comes to about 30 frames a second. */
private const val WAVE_FRAME_MS = 25L

/** How far through [WAVE_PERIOD_MS] the waves are at [millis] on the monotonic clock, from 0 to 1. */
private fun wavePhase(millis: Long): Float = (millis % WAVE_PERIOD_MS) / WAVE_PERIOD_MS.toFloat()

private val WAVES = listOf(
    Wave(center = 0.52f, amplitude = 0.10f, length = 1.4f, speed = 1, thickness = 0.12f, twist = 0.9f, twistSpeed = 2, offset = 0f),
    Wave(center = 0.58f, amplitude = 0.08f, length = 1.1f, speed = -1, thickness = 0.08f, twist = 0.7f, twistSpeed = 1, offset = 0.3f),
    Wave(center = 0.63f, amplitude = 0.12f, length = 1.8f, speed = 2, thickness = 0.05f, twist = 1.2f, twistSpeed = -1, offset = 0.6f),
)

/**
 * A sheet of [strands] fine lines, Deep's wave: its middle swings [amplitude] (of the height) around
 * [center] along a wave [length] (of the width) long, and it turns along [twist], its strands fanning
 * out to [spread] where it lies flat and gathering into one line where it's edge on. It moves [speed]
 * and turns [twistSpeed] wavelengths per [WAVE_PERIOD_MS], whole numbers so the loop is seamless.
 */
internal class Sheet(
    val center: Float,
    val amplitude: Float,
    val length: Float,
    val speed: Int,
    val spread: Float,
    val twist: Float,
    val twistSpeed: Int,
    val strands: Int,
    val offset: Float,
) {
    private fun fan(u: Float, time: Float) = spread * cos(2 * PI * (u / twist + twistSpeed * time + offset * 2)).toFloat()

    /** Where strand [index] is, as a fraction of the height, [u] of the way across at [time] (0 to 1 through the period). */
    fun strandAt(index: Int, u: Float, time: Float): Float {
        val middle = center + amplitude * sin(2 * PI * (u / length + speed * time + offset)).toFloat()
        return middle + (index / (strands - 1f) - 0.5f) * fan(u, time)
    }

    /**
     * How strongly the strands show [u] of the way across at [time]: fully where the sheet lies flat, down to
     * [EDGE_ON] where it's edge on, as a ribbon of threads thins to a faint line seen edge on.
     */
    fun fade(u: Float, time: Float): Float = max(EDGE_ON, abs(fan(u, time)) / spread)

    companion object {
        const val EDGE_ON = 0.06f
    }
}

internal val SHEETS = listOf(
    Sheet(center = 0.6f, amplitude = 0.09f, length = 1.6f, speed = 1, spread = 0.16f, twist = 1.1f, twistSpeed = 1, strands = 18, offset = 0.1f),
    Sheet(center = 0.67f, amplitude = 0.06f, length = 1.2f, speed = -1, spread = 0.1f, twist = 0.8f, twistSpeed = 2, strands = 10, offset = 0.55f),
)

/**
 * Deep's sparkles, rising through its band and wrapping, faded in and out at its edges so the wrap
 * doesn't show, twinkling and swaying a little, in whole cycles per [WAVE_PERIOD_MS] so the loop is seamless.
 */
internal object Sparkles {
    const val COUNT = 40

    /**
     * Sparkle [index] at [time] (0 to 1 through the period) into [out]: across the width and down the height
     * (fractions), and how bright, 0 to 1; written into an array, as the backdrop asks 30 times a second.
     */
    fun at(index: Int, time: Float, out: FloatArray) {
        val band = WaveColors.DEEP_BAND
        val rise = 1 + index % 3
        val climb = (PixelKit.unit(SEED, index * 4) + time * rise) % 1f
        val twinkle = 0.55f + 0.45f * sin(2 * PI * (time * (2 + index % 5) + PixelKit.unit(SEED, index * 4 + 3))).toFloat()
        out[0] = PixelKit.unit(SEED, index * 4 + 1) + 0.015f * sin(2 * PI * (time * (1 + index % 2) + PixelKit.unit(SEED, index * 4 + 2))).toFloat()
        out[1] = band.endInclusive - climb * (band.endInclusive - band.start)
        out[2] = sin(PI * climb).toFloat() * twinkle
    }

    private const val SEED = 2006
}

/**
 * The XMB backdrop: a gradient from a tint of the primary color at the top to the background, crossed
 * by ribbons of light that drift and twist (Waves, or Calm's one faint ribbon), or, for Deep, a darker
 * gradient with a pool of light where two sheets of fine strands sweep and turn, fanning out and
 * gathering, and sparkles drift up through them. It holds still when Android's animations are off.
 */
@Composable
fun XmbBackdrop(
    scheme: ColorScheme,
    isDark: Boolean,
    scene: XmbBackdropScene = XmbBackdropScene.WAVES,
    modifier: Modifier = Modifier,
) {
    // Frame times are on the monotonic clock, so starting from it keeps the first animated frame from jumping.
    val time = remember { mutableFloatStateOf(wavePhase(System.nanoTime() / 1_000_000)) }
    LaunchedEffect(Unit) {
        if (!ValueAnimator.areAnimatorsEnabled()) return@LaunchedEffect
        // The waves drift slowly: about 30 frames a second looks as smooth as every vsync, at a fraction of the cost.
        while (true) {
            delay(WAVE_FRAME_MS)
            withFrameMillis { now -> time.floatValue = wavePhase(now) }
        }
    }
    Spacer(
        modifier.drawWithCache {
            val colors = WaveColors(scheme, isDark, scene)
            val sky = Brush.verticalGradient(0f to colors.top, WaveColors.MIDDLE to colors.middle, 1f to colors.bottom)
            if (scene == XmbBackdropScene.DEEP) {
                val pool = Brush.radialGradient(
                    listOf(colors.pool, colors.pool.copy(alpha = 0f)),
                    center = Offset(size.width * 0.5f, size.height * 0.62f),
                    radius = size.maxDimension * 0.55f,
                )
                val line = Stroke(1.dp.toPx())
                val sparkle = 1.6.dp.toPx()
                val strands = Path()
                val at = FloatArray(3)
                onDrawBehind {
                    drawRect(sky)
                    drawRect(pool)
                    val now = time.floatValue
                    SHEETS.forEach { sheet ->
                        val fade = Brush.horizontalGradient(
                            List(FADE_STOPS + 1) { k -> colors.strand.copy(alpha = colors.strand.alpha * sheet.fade(k / FADE_STOPS.toFloat(), now)) },
                            startX = 0f,
                            endX = size.width,
                        )
                        // One path for the whole sheet: where its strands gather, they don't add up.
                        strands.reset()
                        for (index in 0 until sheet.strands) {
                            for (i in 0..WAVE_STEPS) {
                                val u = i.toFloat() / WAVE_STEPS
                                val y = size.height * sheet.strandAt(index, u, now)
                                if (i == 0) strands.moveTo(0f, y) else strands.lineTo(size.width * u, y)
                            }
                        }
                        drawPath(strands, fade, style = line)
                    }
                    for (index in 0 until Sparkles.COUNT) {
                        Sparkles.at(index, now, at)
                        val center = Offset(size.width * at[0], size.height * at[1])
                        val radius = sparkle * (0.75f + 0.5f * (index % 3) / 2f)
                        drawCircle(colors.sparkle, radius * 2.5f, center, alpha = colors.sparkle.alpha * at[2] * 0.25f)
                        drawCircle(colors.sparkle, radius, center, alpha = colors.sparkle.alpha * at[2])
                    }
                }
            } else {
                val glow = Stroke(6.dp.toPx())
                val crest = Stroke(1.5.dp.toPx())
                val fill = Path()
                val upper = Path()
                val lower = Path()
                onDrawBehind {
                    drawRect(sky)
                    val now = time.floatValue
                    val waves = if (scene == XmbBackdropScene.CALM) WAVES.take(1) else WAVES
                    waves.forEach { wave ->
                        wave.trace(size, now, fill, upper, lower)
                        drawPath(fill, colors.ribbon)
                        drawPath(upper, colors.crestGlow, style = glow)
                        drawPath(upper, colors.crest, style = crest)
                        drawPath(lower, colors.crestGlow, style = glow)
                        drawPath(lower, colors.crest, style = crest)
                    }
                }
            }
        },
    )
}

/**
 * Stops across the width at which a Deep sheet's fade is laid into its strands' gradient: enough that, between
 * two, the fade where a sheet turns edge on stays near [Sheet.EDGE_ON] rather than pulsing as it passes them.
 */
private const val FADE_STOPS = 64

/** XMB waves: M PLUS 1 lettering and the glass panels over ribbons of light flowing across a gradient. */
object XmbUi : UiStyle() {
    override val typography: Typography get() = XmbTypography
    override val ownBackdrop: Boolean get() = true

    @Composable
    override fun Backdrop(modifier: Modifier) =
        XmbBackdrop(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark, LocalPhobosTheme.current.xmbBackdrop, modifier)
}
