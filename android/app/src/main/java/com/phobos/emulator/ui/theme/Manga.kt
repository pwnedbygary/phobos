package com.phobos.emulator.ui.theme

import android.graphics.Bitmap
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontSynthesis
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.R
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** Bangers (SIL Open Font License 1.1; see LICENSE), comic-book sound-effect lettering, for headings. */
val MangaHeadingFont = FontFamily(Font(R.font.bangers))

/** Comic Neue (SIL Open Font License 1.1; see LICENSE), comic hand lettering drawn for reading. */
val MangaFont = FontFamily(
    Font(R.font.comic_neue_regular, FontWeight.Normal),
    Font(R.font.comic_neue_bold, FontWeight.Bold),
)

/** Bangers has one weight, and its letters sit tight; lettering gets a little air. */
private fun TextStyle.inLettering(): TextStyle = copy(
    fontFamily = MangaHeadingFont,
    fontWeight = FontWeight.Normal,
    letterSpacing = 0.5.sp,
    fontSynthesis = FontSynthesis.None,
)

private fun TextStyle.inComicNeue(): TextStyle = copy(fontFamily = MangaFont, letterSpacing = 0.sp)

/** The app's type scale as a comic's: headlines and the top bar's title in Bangers, everything else in Comic Neue. */
val MangaTypography = with(PhobosTypography) {
    Typography(
        displayLarge = displayLarge.inLettering(),
        displayMedium = displayMedium.inLettering(),
        displaySmall = displaySmall.inLettering(),
        headlineLarge = headlineLarge.inLettering(),
        headlineMedium = headlineMedium.inLettering(),
        headlineSmall = headlineSmall.inLettering(),
        titleLarge = titleLarge.inLettering(),
        titleMedium = titleMedium.inComicNeue(),
        titleSmall = titleSmall.inComicNeue(),
        bodyLarge = bodyLarge.inComicNeue(),
        bodyMedium = bodyMedium.inComicNeue(),
        bodySmall = bodySmall.inComicNeue(),
        labelLarge = labelLarge.inComicNeue(),
        labelMedium = labelMedium.inComicNeue(),
        labelSmall = labelSmall.inComicNeue(),
    )
}

private val Panel = RoundedCornerShape(0.dp)

/** Square panels, like a manga page's; dialogs and the pause menu ([Shapes.extraLarge]) are round, like speech balloons. */
private val MangaShapes = Shapes(extraSmall = Panel, small = Panel, medium = Panel, large = Panel, extraLarge = RoundedCornerShape(28.dp))

/** The screentone's dots on the backdrop. */
internal fun mangaTone(scheme: ColorScheme, isDark: Boolean): Color =
    lerp(scheme.background, scheme.onBackground, if (isDark) 0.22f else 0.28f)

private val PANEL_LINE = 3.dp
private val CAPTION_LINE = 1.5.dp
private val CAPTION_REACH_X = 6.dp
private val CAPTION_REACH_Y = 3.dp

/** Dot spacing along a row of the screentone, whose rows are half this apart and offset, a grid turned 45 degrees. */
private val TONE_PITCH = 8.dp

/**
 * The manga backdrop: paper with a screentone whose dots grow from nothing a quarter of the way down
 * to their full size at the bottom. One column of the tone is rendered and repeated across.
 */
@Composable
fun MangaBackdrop(scheme: ColorScheme, isDark: Boolean, modifier: Modifier = Modifier) {
    Spacer(
        modifier.drawWithCache {
            val pitch = max(4, TONE_PITCH.toPx().roundToInt())
            val rows = pitch / 2f
            val height = max(1, size.height.roundToInt())
            val paper = scheme.background
            val ink = mangaTone(scheme, isDark)
            val pixels = IntArray(pitch * height)
            val largest = rows * 0.62f
            for (y in 0 until height) {
                val t = ((y.toFloat() / height - 0.25f) / 0.75f).coerceIn(0f, 1f)
                val radius = largest * t * t * (3 - 2 * t)
                val row = floor(y / rows).toInt()
                for (x in 0 until pitch) {
                    var coverage = 0f
                    if (radius > 0f) {
                        for (r in row - 1..row + 1) {
                            val dy = y + 0.5f - (r + 0.5f) * rows
                            val offset = if (r % 2 == 0) 0f else pitch / 2f
                            for (k in -1..1) {
                                val dx = x + 0.5f - (offset + k * pitch)
                                coverage = max(coverage, (radius - sqrt(dx * dx + dy * dy) + 0.5f).coerceIn(0f, 1f))
                            }
                        }
                    }
                    pixels[y * pitch + x] = lerp(paper, ink, coverage).toArgb()
                }
            }
            val column = Bitmap.createBitmap(pixels, pitch, height, Bitmap.Config.ARGB_8888).asImageBitmap()
            val brush = ShaderBrush(ImageShader(column, TileMode.Repeated, TileMode.Clamp))
            onDrawBehind { drawRect(brush) }
        },
    )
}

/**
 * An impact burst in [size], centered: [BURST_POINTS] spikes of uneven length around an ellipse, like
 * the flash behind a comic's sound effect.
 */
private fun burst(size: Size): Path = Path().apply {
    val cx = size.width / 2f
    val cy = size.height / 2f
    val points = BURST_POINTS * 2
    for (i in 0 until points) {
        val angle = 2 * PI * i / points - PI / 2 + 0.2
        val reach = if (i % 2 == 1) 0.62f else if (i % 4 == 0) 1f else 0.84f
        val x = cx + (cx * reach * cos(angle)).toFloat()
        val y = cy + (cy * reach * sin(angle)).toFloat()
        if (i == 0) moveTo(x, y) else lineTo(x, y)
    }
    close()
}

private const val BURST_POINTS = 12

/** A title's hard offset shadow, like a color plate printed out of register behind lettering. */
private fun TextStyle.offsetShadow(color: Color, distance: Float): TextStyle =
    copy(shadow = Shadow(color = color, offset = Offset(distance, distance), blurRadius = 0f))

/** Manga ink: black-line panels, comic lettering, caption boxes, screentone and impact bursts. */
object MangaUi : SolidUi() {
    override val typography: Typography get() = MangaTypography
    override val shapes: Shapes get() = MangaShapes
    override val capitalHeaders: Boolean get() = true

    @Composable
    override fun titleStyle(base: TextStyle): TextStyle {
        val distance = with(LocalDensity.current) { 2.dp.toPx() }
        return base.offsetShadow(MaterialTheme.colorScheme.primary.copy(alpha = 0.55f), distance)
    }

    @Composable
    override fun sectionStyle(base: TextStyle): TextStyle {
        val distance = with(LocalDensity.current) { 1.5.dp.toPx() }
        return MangaTypography.titleLarge.copy(fontSize = 20.sp).offsetShadow(MaterialTheme.colorScheme.onBackground.copy(alpha = 0.35f), distance)
    }

    @Composable
    override fun Backdrop(modifier: Modifier) = MangaBackdrop(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark, modifier)

    /** A caption box: paper inside a thin ink line, like a narration box. */
    @Composable
    override fun plate(): Modifier {
        val paper = MaterialTheme.colorScheme.background
        val ink = MaterialTheme.colorScheme.onSurface
        return Modifier.drawBehind {
            val x = CAPTION_REACH_X.toPx()
            val y = CAPTION_REACH_Y.toPx()
            val line = CAPTION_LINE.toPx()
            drawRect(paper, Offset(-x, -y), Size(size.width + x * 2, size.height + y * 2))
            drawRect(ink, Offset(line / 2 - x, line / 2 - y), Size(size.width + x * 2 - line, size.height + y * 2 - line), style = Stroke(line))
        }
    }

    @Composable
    override fun panel(shape: Shape, fill: Color): Modifier {
        val ink = MaterialTheme.colorScheme.onSurface
        return Modifier.drawWithCache {
            val outline = shape.createOutline(size, layoutDirection, this)
            val border = ring(shape, size, layoutDirection, 0f, PANEL_LINE.toPx())
            onDrawWithContent {
                drawOutline(outline, fill)
                drawContent()
                drawPath(border, ink)
            }
        }
    }

    @Composable
    override fun edge(shape: Shape): Modifier {
        val ink = MaterialTheme.colorScheme.onSurface
        return Modifier.drawWithCache {
            val border = ring(shape, size, layoutDirection, 0f, PANEL_LINE.toPx())
            onDrawWithContent {
                drawContent()
                drawPath(border, ink)
            }
        }
    }

    @Composable
    override fun menuBorder() = BorderStroke(2.dp, MaterialTheme.colorScheme.onSurface)

    override val dockShape: Shape get() = Panel
    override val dockTabShape: Shape get() = Panel

    /** An impact burst in the primary color, inked, behind the selected icon; it bursts open with the indicator. */
    override fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme) {
        val path = burst(size)
        drawPath(path, colors.primary, alpha = alpha)
        drawPath(path, colors.onSurface, alpha = alpha, style = Stroke(1.5.dp.toPx(), join = StrokeJoin.Miter))
    }

    @Composable
    override fun dockIconColor() = MaterialTheme.colorScheme.onPrimary

    @Composable
    override fun Switch(checked: Boolean) = MangaSwitch(checked)

    @Composable
    override fun SliderThumb() = MangaSliderThumb()

    @Composable
    override fun SliderTrack(fraction: Float) = MangaBar(fraction = { fraction }, sweep = null, modifier = Modifier)

    @Composable
    override fun ProgressBar(progress: (() -> Float)?, modifier: Modifier) {
        val sweep = if (progress == null) {
            rememberInfiniteTransition(label = "mangaProgress")
                .animateFloat(0f, 1f, infiniteRepeatable(tween(durationMillis = 900, easing = LinearEasing)), label = "mangaProgressSweep")
        } else {
            null
        }
        MangaBar(progress, sweep?.let { { it.value } }, modifier)
    }
}

private val SwitchLabel = MangaTypography.titleLarge.copy(fontSize = 18.sp)

/** A speech balloon of a switch: "ON!" on the primary color, or "OFF" on paper, in an ink line. */
@Composable
private fun MangaSwitch(checked: Boolean) {
    val scheme = MaterialTheme.colorScheme
    val measurer = rememberTextMeasurer()
    val label = remember(checked, scheme, measurer) {
        measurer.measure(if (checked) "ON!" else "OFF", SwitchLabel.copy(color = if (checked) scheme.onPrimary else scheme.onSurfaceVariant))
    }
    val widest = remember(measurer) { max(measurer.measure("ON!", SwitchLabel).size.width, measurer.measure("OFF", SwitchLabel).size.width) }
    val width = with(LocalDensity.current) { widest.toDp() + 24.dp }
    Canvas(Modifier.size(width = width, height = 30.dp)) {
        val outline = CircleShape.createOutline(size, layoutDirection, this)
        if (checked) drawOutline(outline, scheme.primary)
        drawPath(ring(CircleShape, size, layoutDirection, 0f, 2.dp.toPx()), scheme.onSurface)
        drawText(label, topLeft = Offset((size.width - label.size.width) / 2f, (size.height - label.size.height) / 2f))
    }
}

/** A slider's thumb: an inked dot in the primary color. */
@Composable
private fun MangaSliderThumb() {
    val scheme = MaterialTheme.colorScheme
    Canvas(Modifier.size(22.dp)) {
        drawCircle(scheme.primary)
        drawPath(ring(CircleShape, size, layoutDirection, 0f, 2.dp.toPx()), scheme.onSurface)
    }
}

/** Hatching: lines slanting up to the right every [HATCH_PITCH], [HATCH_LINE] wide, shifted along by [phase] pitches. */
private fun DrawScope.drawHatching(color: Color, phase: Float) {
    val pitch = HATCH_PITCH.toPx()
    val line = HATCH_LINE.toPx()
    var x = -size.height + (phase % 1f) * pitch - pitch
    while (x < size.width + pitch) {
        drawLine(color, Offset(x, size.height), Offset(x + size.height, 0f), strokeWidth = line)
        x += pitch
    }
}

private val HATCH_PITCH = 6.dp
private val HATCH_LINE = 2.dp

/**
 * A bar in an ink line, hatched in the primary color, like a manga's shading, up to [fraction]; or,
 * when [sweep] is set, hatched all along with the lines moving.
 */
@Composable
private fun MangaBar(fraction: (() -> Float)?, sweep: (() -> Float)?, modifier: Modifier) {
    val scheme = MaterialTheme.colorScheme
    Canvas(modifier.fillMaxWidth().height(12.dp)) {
        val outline = CircleShape.createOutline(size, layoutDirection, this)
        drawOutline(outline, scheme.surfaceContainerHighest)
        val filled = when {
            sweep != null -> size.width
            fraction != null -> size.width * fraction().coerceIn(0f, 1f)
            else -> 0f
        }
        if (filled > 0f) {
            val ltr = layoutDirection == LayoutDirection.Ltr
            clipPath(Path().apply { addOutline(outline) }) {
                clipRect(left = if (ltr) 0f else size.width - filled, right = if (ltr) filled else size.width) {
                    drawHatching(scheme.primary, sweep?.invoke() ?: 0f)
                }
            }
        }
        drawPath(ring(CircleShape, size, layoutDirection, 0f, 2.dp.toPx()), scheme.onSurface)
    }
}
