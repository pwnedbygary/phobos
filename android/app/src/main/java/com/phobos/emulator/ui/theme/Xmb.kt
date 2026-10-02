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

/** The XMB scene's colors, shared with the glass panels' contrast search ([GlassStyle]). */
internal class WaveColors(scheme: ColorScheme, isDark: Boolean) {
    /** Tints of the primary color at no less contrast with the text than the background has, like the glass glows. */
    val top = tintKeepingContrast(scheme.background, scheme.primary, 0.4f, isDark)
    val middle = tintKeepingContrast(scheme.background, scheme.primary, 0.2f, isDark)

    /** Deeper in dark themes and paler in light ones, away from the text. */
    val bottom = if (isDark) lerp(scheme.background, Color.Black, 0.35f) else lerp(scheme.background, Color.White, 0.5f)

    /** The ribbons' light: white on dark themes, and the primary color on light ones, where white wouldn't show. */
    private val light = if (isDark) Color.White else scheme.primary

    /** A ribbon's see-through fill. */
    val ribbon = light.copy(alpha = 0.06f)

    /** The soft glow along each edge of a ribbon, a wide stroke under [crest]. */
    val crestGlow = light.copy(alpha = 0.05f)

    /** The bright line along each edge of a ribbon. */
    val crest = light.copy(alpha = if (isDark) 0.24f else 0.3f)

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
 * The XMB backdrop: a gradient from a tint of the primary color at the top to the background, crossed
 * by ribbons of light that drift and twist, holding still when Android's animations are off.
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
            val colors = WaveColors(scheme, isDark)
            val bottom = when (scene) {
                XmbBackdropScene.DEEP -> lerp(colors.bottom, Color.Black, 0.35f)
                else -> colors.bottom
            }
            val sky = Brush.verticalGradient(0f to colors.top, WaveColors.MIDDLE to colors.middle, 1f to bottom)
            val glow = Stroke(6.dp.toPx())
            val crest = Stroke(1.5.dp.toPx())
            val fill = Path()
            val upper = Path()
            val lower = Path()
            onDrawBehind {
                drawRect(sky)
                val now = time.floatValue
                val waves = if (scene == XmbBackdropScene.CALM) WAVES.take(1) else WAVES
                val strength = when (scene) {
                    XmbBackdropScene.CALM -> 0.5f
                    XmbBackdropScene.DEEP -> 1.35f
                    XmbBackdropScene.WAVES -> 1f
                }
                waves.forEach { wave ->
                    wave.trace(size, now, fill, upper, lower)
                    drawPath(fill, colors.ribbon, alpha = strength)
                    drawPath(upper, colors.crestGlow, alpha = strength, style = glow)
                    drawPath(upper, colors.crest, alpha = strength, style = crest)
                    drawPath(lower, colors.crestGlow, alpha = strength, style = glow)
                    drawPath(lower, colors.crest, alpha = strength, style = crest)
                }
            }
        },
    )
}

/** XMB waves: M PLUS 1 lettering and the glass panels over ribbons of light flowing across a gradient. */
object XmbUi : UiStyle() {
    override val typography: Typography get() = XmbTypography
    override val ownBackdrop: Boolean get() = true

    @Composable
    override fun Backdrop(modifier: Modifier) =
        XmbBackdrop(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark, LocalPhobosTheme.current.xmbBackdrop, modifier)
}
