package com.phobos.emulator.ui.theme

import android.graphics.Bitmap
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.R
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.roundToInt

/** Press Start 2P (SIL Open Font License 1.1; see LICENSE), an 8x8 arcade font, for short uppercase headings. */
val PixelHeadingFont = FontFamily(Font(R.font.press_start_2p))

/** Pixelify Sans (SIL Open Font License 1.1; see LICENSE), a variable-weight pixel font drawn for reading. */
@OptIn(ExperimentalTextApi::class)
val PixelFont = FontFamily(
    Font(R.font.pixelify_sans, FontWeight.Normal, variationSettings = FontVariation.Settings(FontVariation.weight(400))),
    Font(R.font.pixelify_sans, FontWeight.Medium, variationSettings = FontVariation.Settings(FontVariation.weight(500))),
    Font(R.font.pixelify_sans, FontWeight.SemiBold, variationSettings = FontVariation.Settings(FontVariation.weight(600))),
    Font(R.font.pixelify_sans, FontWeight.Bold, variationSettings = FontVariation.Settings(FontVariation.weight(700))),
)

/**
 * Press Start 2P at [size]. Sizes step by 3 sp, which at common phone densities and font scales puts
 * the font's pixels close to whole screen pixels.
 */
private fun heading(size: Int, lineHeight: Int) = TextStyle(
    fontFamily = PixelHeadingFont,
    fontWeight = FontWeight.Normal,
    fontSize = size.sp,
    lineHeight = lineHeight.sp,
    letterSpacing = 0.sp,
    fontFeatureSettings = NO_LIGATURES,
)

private val Base = Typography()

/** Pixelify's standard ligatures join "fi" and "fl" into one glyph that reads like a capital A. */
private const val NO_LIGATURES = "liga 0"

private fun TextStyle.inPixelFont(weight: FontWeight) =
    copy(fontFamily = PixelFont, fontWeight = weight, letterSpacing = 0.sp, fontFeatureSettings = NO_LIGATURES)

/**
 * The type scale with pixel art effects: page headers and the top bar's title in Press Start 2P,
 * everything else, dialog titles included, in Pixelify Sans at Material's sizes.
 */
val PixelTypography = Typography(
    displayLarge = heading(36, 54),
    displayMedium = heading(30, 45),
    displaySmall = heading(24, 36),
    headlineLarge = heading(24, 36),
    headlineMedium = heading(21, 32),
    headlineSmall = Base.headlineSmall.inPixelFont(FontWeight.Bold),
    titleLarge = heading(15, 24),
    titleMedium = Base.titleMedium.inPixelFont(FontWeight.SemiBold),
    titleSmall = Base.titleSmall.inPixelFont(FontWeight.SemiBold),
    bodyLarge = Base.bodyLarge.inPixelFont(FontWeight.Normal),
    bodyMedium = Base.bodyMedium.inPixelFont(FontWeight.Normal),
    bodySmall = Base.bodySmall.inPixelFont(FontWeight.Normal),
    labelLarge = Base.labelLarge.inPixelFont(FontWeight.SemiBold),
    labelMedium = Base.labelMedium.inPixelFont(FontWeight.Medium),
    labelSmall = Base.labelSmall.inPixelFont(FontWeight.Medium),
)

/** Section headers with pixel art effects: small Press Start 2P, uppercase at the call site. */
val PixelSectionHeading: TextStyle = heading(9, 14)

/** Pixel heading: a hard drop shadow one pixel-font pixel down and right, like an arcade title. */
fun TextStyle.pixelShadow(color: Color): TextStyle = copy(
    shadow = Shadow(color = color, offset = Offset(3f, 3f), blurRadius = 0f),
)

/**
 * A rectangle whose corners are cut in [steps] square steps, like a pixel-art window; each corner's
 * size is the whole staircase. A [CornerBasedShape], like Material's rounded and cut corners, so it
 * works in [Shapes] and shrinks to fit small components.
 */
class PixelShape(
    topStart: CornerSize,
    topEnd: CornerSize,
    bottomEnd: CornerSize,
    bottomStart: CornerSize,
    private val steps: Int,
) : CornerBasedShape(topStart, topEnd, bottomEnd, bottomStart) {
    /** [steps] steps of [step] at every corner. */
    constructor(step: Dp, steps: Int) : this(CornerSize(step * steps), CornerSize(step * steps), CornerSize(step * steps), CornerSize(step * steps), steps)

    override fun createOutline(
        size: Size,
        topStart: Float,
        topEnd: Float,
        bottomEnd: Float,
        bottomStart: Float,
        layoutDirection: LayoutDirection,
    ): Outline {
        if (topStart + topEnd + bottomEnd + bottomStart == 0f) return Outline.Rectangle(Rect(Offset.Zero, size))
        val ltr = layoutDirection == LayoutDirection.Ltr
        val tl = if (ltr) topStart else topEnd
        val tr = if (ltr) topEnd else topStart
        val br = if (ltr) bottomEnd else bottomStart
        val bl = if (ltr) bottomStart else bottomEnd
        val w = size.width
        val h = size.height
        val n = steps.coerceAtLeast(1)
        val path = Path()
        // Clockwise from the top edge; each corner is a staircase of n steps across its size.
        path.moveTo(tl, 0f)
        path.lineTo(w - tr, 0f)
        for (k in 1..n) {
            val s = tr / n
            path.lineTo(w - tr + (k - 1) * s, k * s)
            path.lineTo(w - tr + k * s, k * s)
        }
        path.lineTo(w, h - br)
        for (k in 1..n) {
            val s = br / n
            path.lineTo(w - (k - 1) * s, h - br + k * s)
            path.lineTo(w - k * s, h - br + k * s)
        }
        path.lineTo(bl, h)
        for (k in 1..n) {
            val s = bl / n
            path.lineTo(bl - (k - 1) * s, h - k * s)
            path.lineTo(bl - k * s, h - k * s)
        }
        path.lineTo(0f, tl)
        for (k in 1..n) {
            val s = tl / n
            path.lineTo((k - 1) * s, tl - k * s)
            path.lineTo(k * s, tl - k * s)
        }
        path.close()
        return Outline.Generic(path)
    }

    override fun copy(topStart: CornerSize, topEnd: CornerSize, bottomEnd: CornerSize, bottomStart: CornerSize) =
        PixelShape(topStart, topEnd, bottomEnd, bottomStart, steps)

    override fun equals(other: Any?) = other is PixelShape && other.steps == steps &&
        other.topStart == topStart && other.topEnd == topEnd && other.bottomEnd == bottomEnd && other.bottomStart == bottomStart

    override fun hashCode() = listOf(topStart, topEnd, bottomEnd, bottomStart, steps).hashCode()
}

val PixelShapes = Shapes(
    extraSmall = PixelShape(2.dp, 1),
    small = PixelShape(2.dp, 2),
    medium = PixelShape(3.dp, 2),
    large = PixelShape(3.dp, 3),
    extraLarge = PixelShape(4.dp, 3),
)

/** Stands in for Material's fully rounded pill, which [Shapes] doesn't cover, with pixel art effects. */
val PixelPillShape = PixelShape(2.dp, 2)

/** Pixel art, the look of the app icon. */
object PixelUi : SolidUi() {
    override val typography: Typography get() = PixelTypography
    override val shapes: Shapes get() = PixelShapes
    override val pill: CornerBasedShape get() = PixelPillShape
    override val capitalHeaders: Boolean get() = true

    @Composable
    override fun titleStyle(base: TextStyle) = base.pixelShadow(pixelShadow(LocalPhobosTheme.current.isDark))

    @Composable
    override fun sectionStyle(base: TextStyle) = PixelSectionHeading.pixelShadow(pixelShadow(LocalPhobosTheme.current.isDark))

    /** Takes the theme's final colors, not the cross-fade: the scene is rendered again whenever its colors change. */
    @Composable
    override fun Backdrop(modifier: Modifier) {
        val theme = LocalPhobosTheme.current
        PixelBackdrop(theme.scheme, theme.isDark, modifier)
    }

    @Composable
    override fun plate() = Modifier.pixelPlate(MaterialTheme.colorScheme.background)

    @Composable
    override fun panel(shape: Shape, fill: Color) =
        Modifier.pixelPanel(shape, fill, pixelBorder(MaterialTheme.colorScheme), pixelShadow(LocalPhobosTheme.current.isDark))

    @Composable
    override fun edge(shape: Shape) =
        Modifier.pixelEdge(shape, pixelBorder(MaterialTheme.colorScheme), pixelShadow(LocalPhobosTheme.current.isDark))

    @Composable
    override fun menuBorder() = BorderStroke(PANEL_BORDER, pixelBorder(MaterialTheme.colorScheme))

    override val dockShape: Shape = PixelShape(4.dp, 3)
    override val dockTabShape: Shape = PixelShape(3.dp, 2)
    override fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme) =
        drawOutline(PixelPillShape.createOutline(size, layoutDirection, this), colors.primaryContainer, alpha = alpha)

    @Composable
    override fun dockIconColor() = MaterialTheme.colorScheme.onPrimaryContainer

    @Composable
    override fun Switch(checked: Boolean) = PixelSwitch(checked)

    @Composable
    override fun SliderThumb() = PixelSliderThumb()

    @Composable
    override fun SliderTrack(fraction: Float) = PixelSliderTrack(fraction)

    @Composable
    override fun ProgressBar(progress: (() -> Float)?, modifier: Modifier) = PixelProgressBar(progress, modifier)
}

/** The pixel backdrop's colors for [scheme]: light themes get a paler, quieter scene. */
internal fun pixelSceneColors(scheme: ColorScheme, isDark: Boolean): PixelSceneColors {
    val strength = if (isDark) 1f else 0.55f
    val background = scheme.background
    val primary = scheme.primary
    val planetLit = lerp(background, primary, 0.5f * strength)
    val planetShade = lerp(background, primary, 0.32f * strength)
    return PixelSceneColors(
        skyTop = background.toArgb(),
        skyBottom = lerp(background, primary, 0.2f * strength).toArgb(),
        star = if (isDark) scheme.onBackground.toArgb() else lerp(background, scheme.onBackground, 0.35f).toArgb(),
        planetLit = planetLit.toArgb(),
        planetShade = planetShade.toArgb(),
        crater = lerp(planetShade, if (isDark) Color.Black else background, 0.3f).toArgb(),
        rim = lerp(primary, if (isDark) Color.White else background, 0.3f).toArgb(),
        glow = lerp(background, primary, 0.6f * strength).toArgb(),
    )
}

/** The size of one art pixel in the backdrop. */
private val ART_PIXEL = 4.dp

/**
 * Pixel-art backdrop behind the app's screens: stars over a banded, dithered sky and a planet's limb
 * along the bottom, in the theme's colors. It is rendered small, one element per art pixel, and
 * scaled up without filtering, so it stays crisp and costs almost nothing to draw.
 */
@Composable
fun PixelBackdrop(scheme: ColorScheme, isDark: Boolean, modifier: Modifier = Modifier) {
    Spacer(
        modifier.drawWithCache {
            val pixelPx = max(1f, ART_PIXEL.toPx())
            val cols = ceil(size.width / pixelPx).toInt().coerceAtLeast(1)
            val rows = ceil(size.height / pixelPx).toInt().coerceAtLeast(1)
            val pixels = PixelScene.render(cols, rows, pixelSceneColors(scheme, isDark))
            val image = Bitmap.createBitmap(pixels, cols, rows, Bitmap.Config.ARGB_8888).asImageBitmap()
            val target = IntSize(ceil(cols * pixelPx).toInt(), ceil(rows * pixelPx).toInt())
            onDrawBehind {
                drawImage(image, dstOffset = IntOffset.Zero, dstSize = target, filterQuality = FilterQuality.None)
            }
        },
    )
}

/**
 * A pixel panel's edge around the content: a solid shadow offset down and right, drawn behind, and a
 * hard [border] inside [shape], drawn over the content. The content draws its own fill.
 */
fun Modifier.pixelEdge(shape: Shape, border: Color, shadow: Color): Modifier = drawWithCache {
    val outline = shape.createOutline(size, layoutDirection, this)
    val path = Path().apply { addOutline(outline) }
    val offset = PANEL_SHADOW.toPx()
    val shadowPath = Path().apply {
        addOutline(outline)
        translate(Offset(offset, offset))
    }
    val stroke = Stroke(width = PANEL_BORDER.toPx() * 2)
    onDrawWithContent {
        drawPath(shadowPath, shadow)
        drawContent()
        clipPath(path) { drawPath(path, border, style = stroke) }
    }
}

/** A pixel panel: [pixelEdge] around an opaque [fill]. */
fun Modifier.pixelPanel(shape: Shape, fill: Color, border: Color, shadow: Color): Modifier =
    pixelEdge(shape, border, shadow).drawWithCache {
        val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawWithCache)) }
        onDrawBehind { drawPath(path, fill) }
    }

/** The border and shadow colors of pixel panels in [scheme]. */
fun pixelBorder(scheme: ColorScheme): Color = scheme.outline
fun pixelShadow(isDark: Boolean): Color = Color.Black.copy(alpha = if (isDark) 0.55f else 0.22f)

/**
 * A hard-edged plate of [color] behind text drawn straight on the pixel backdrop, so it stays
 * readable where it passes over the planet ([PIXEL_PLATE_ALPHA]).
 */
fun Modifier.pixelPlate(color: Color): Modifier = drawWithCache {
    val reach = PLATE_REACH.toPx()
    val shape = PixelShape(2.dp, 2)
    val plateSize = Size(size.width + reach * 2, size.height + reach * 2)
    val path = Path().apply {
        addOutline(shape.createOutline(plateSize, layoutDirection, this@drawWithCache))
        translate(Offset(-reach, -reach))
    }
    onDrawBehind { drawPath(path, color.copy(alpha = PIXEL_PLATE_ALPHA)) }
}

/**
 * The pixel plate's alpha: solid, a label block, since at 0.9 the brightest stars and the planet's
 * rim showed through enough to take some themes' text below AA (PixelContrastTest).
 */
const val PIXEL_PLATE_ALPHA = 1f

private val PANEL_BORDER = 2.dp
private val PANEL_SHADOW = 4.dp
private val PLATE_REACH = 4.dp

/** One art pixel of the controls below. */
private val ART = 2.dp

/** A check mark on a 7 by 5 grid of art pixels. */
private val PIXEL_CHECK = listOf(6 to 0, 5 to 1, 0 to 2, 4 to 2, 1 to 3, 3 to 3, 2 to 4)

/** A [PANEL_BORDER] of [color] inside [outline]. */
private fun DrawScope.drawPixelBorder(outline: Outline, color: Color) {
    clipPath(Path().apply { addOutline(outline) }) { drawOutline(outline, color, style = Stroke(PANEL_BORDER.toPx() * 2)) }
}

/**
 * Material's switch with pixel art effects, since the switch takes no shape: a stepped track and a
 * square thumb that hops across in art pixels, with a pixel check mark when on, in the switch's
 * colors. Display only, like the switch it stands in for in a settings row, which does the toggling.
 */
@Composable
fun PixelSwitch(checked: Boolean, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    val position by animateFloatAsState(if (checked) 1f else 0f, tween(durationMillis = 120), label = "pixelSwitch")
    Canvas(modifier.size(width = 52.dp, height = 30.dp)) {
        val art = ART.toPx()
        val track = PixelPillShape.createOutline(size, layoutDirection, this)
        drawOutline(track, if (checked) scheme.primary else scheme.surfaceContainerHighest)
        if (!checked) drawPixelBorder(track, scheme.outline)
        val thumb = art * 9
        val travel = size.width - thumb - art * 6
        val start = art * 3 + (travel * position / art).roundToInt() * art
        val x = if (layoutDirection == LayoutDirection.Ltr) start else size.width - start - thumb
        translate(x, (size.height - thumb) / 2f) {
            drawOutline(PixelShape(ART, 1).createOutline(Size(thumb, thumb), layoutDirection, this), if (checked) scheme.onPrimary else scheme.outline)
            if (checked) PIXEL_CHECK.forEach { (col, row) -> drawRect(scheme.primary, Offset(art * (1 + col), art * (2 + row)), Size(art, art)) }
        }
    }
}

/** A slider's thumb with pixel art effects: a block in the primary color with a hard outline. */
@Composable
fun PixelSliderThumb() {
    val scheme = MaterialTheme.colorScheme
    Canvas(Modifier.size(width = 16.dp, height = 28.dp)) {
        val outline = PixelShape(ART, 1).createOutline(size, layoutDirection, this)
        drawOutline(outline, scheme.primary)
        drawPixelBorder(outline, scheme.onSurface)
    }
}

/** A slider's track with pixel art effects: a bordered bar filled in the primary color up to [fraction]. */
@Composable
fun PixelSliderTrack(fraction: Float) {
    val scheme = MaterialTheme.colorScheme
    Canvas(Modifier.fillMaxWidth().height(14.dp)) {
        val outline = PixelShape(ART, 1).createOutline(size, layoutDirection, this)
        drawOutline(outline, scheme.surfaceContainerHighest)
        val filled = size.width * fraction.coerceIn(0f, 1f)
        val ltr = layoutDirection == LayoutDirection.Ltr
        clipRect(left = if (ltr) 0f else size.width - filled, right = if (ltr) filled else size.width) { drawOutline(outline, scheme.primary) }
        drawPixelBorder(outline, scheme.outline)
    }
}

/**
 * Linear progress with pixel art effects: a bordered bar filling with blocks up to [progress], or,
 * while [progress] is null, a short run of blocks sweeping across it.
 */
@Composable
fun PixelProgressBar(progress: (() -> Float)?, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    val sweep = if (progress == null) {
        rememberInfiniteTransition(label = "pixelProgress")
            .animateFloat(0f, 1f, infiniteRepeatable(tween(durationMillis = 1600, easing = LinearEasing)), label = "pixelProgressSweep")
    } else {
        null
    }
    Canvas(modifier.fillMaxWidth().height(14.dp)) {
        val outline = PixelShape(ART, 1).createOutline(size, layoutDirection, this)
        drawOutline(outline, scheme.surfaceContainerHighest)
        val art = ART.toPx()
        val block = art * 3
        val pitch = block + art
        val count = ((size.width - art * 3) / pitch).toInt().coerceAtLeast(1)
        val run = 4
        val (first, end) = if (progress != null) {
            0 to (progress().coerceIn(0f, 1f) * count).toInt()
        } else {
            val head = (sweep!!.value * (count + run)).toInt()
            head - run to head
        }
        for (i in first.coerceAtLeast(0) until end.coerceAtMost(count)) {
            val start = art * 2 + i * pitch
            val x = if (layoutDirection == LayoutDirection.Ltr) start else size.width - start - block
            drawRect(scheme.primary, Offset(x, art * 2), Size(block, size.height - art * 4))
        }
        drawPixelBorder(outline, scheme.outline)
    }
}
