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
import androidx.compose.foundation.shape.CornerBasedShape
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
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.center
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
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
import com.phobos.emulator.data.CrtBackdropScene
import kotlin.math.max

/** VT323 (SIL Open Font License 1.1; see LICENSE), the type of DEC's VT320 terminal. */
val TerminalFont = FontFamily(Font(R.font.vt323))

/** VT323 draws small in its em square, so terminal type runs this much larger than the app's type scale. */
private const val TERMINAL_SCALE = 1.25f

/** Slashed zeros, as on the terminal; no ligatures. */
private const val TERMINAL_FEATURES = "liga 0, zero 1"

private fun TextStyle.inTerminalFont(): TextStyle {
    val size = fontSize.value * TERMINAL_SCALE
    return copy(
        fontFamily = TerminalFont,
        fontWeight = FontWeight.Normal,
        fontSize = size.sp,
        lineHeight = max(lineHeight.value, size * 1.1f).sp,
        letterSpacing = 0.sp,
        fontSynthesis = FontSynthesis.None,
        fontFeatureSettings = TERMINAL_FEATURES,
    )
}

/** The app's type scale in VT323. VT323 has one weight, and synthesized bold would smear it. */
val TerminalTypography = with(PhobosTypography) {
    Typography(
        displayLarge = displayLarge.inTerminalFont(),
        displayMedium = displayMedium.inTerminalFont(),
        displaySmall = displaySmall.inTerminalFont(),
        headlineLarge = headlineLarge.inTerminalFont(),
        headlineMedium = headlineMedium.inTerminalFont(),
        headlineSmall = headlineSmall.inTerminalFont(),
        titleLarge = titleLarge.inTerminalFont(),
        titleMedium = titleMedium.inTerminalFont(),
        titleSmall = titleSmall.inTerminalFont(),
        bodyLarge = bodyLarge.inTerminalFont(),
        bodyMedium = bodyMedium.inTerminalFont(),
        bodySmall = bodySmall.inTerminalFont(),
        labelLarge = labelLarge.inTerminalFont(),
        labelMedium = labelMedium.inTerminalFont(),
        labelSmall = labelSmall.inTerminalFont(),
    )
}

private val Square = RoundedCornerShape(0.dp)

/** A phosphor bloom in [color] around terminal headings. */
private fun TextStyle.phosphor(color: Color): TextStyle = copy(shadow = Shadow(color = color.copy(alpha = 0.6f), blurRadius = 14f))

/**
 * The darkening of a scanline on the backdrop. Only the backdrop has them: darkening the pages' text
 * with its panel takes most themes' accent text below AA, since they're tuned to just pass.
 */
internal fun crtScanline(isDark: Boolean): Float = if (isDark) 0.30f else 0.08f

/** The vignette's darkening at its edge. */
internal fun crtVignette(isDark: Boolean): Float = if (isDark) 0.45f else 0.12f

/** How far the backdrop's center glows toward the primary color, like a CRT's phosphor bloom. */
internal fun crtBloom(isDark: Boolean): Float = if (isDark) 0.12f else 0.08f

/**
 * One scanline every [SCANLINE_PITCH] screen pixels: a repeating image of a dark row under clear
 * ones, so the whole screen takes one draw.
 */
private val scanlines: ShaderBrush by lazy {
    val pixels = IntArray(SCANLINE_PITCH) { if (it == SCANLINE_PITCH - 1) 0xFF000000.toInt() else 0 }
    val image = Bitmap.createBitmap(pixels, 1, SCANLINE_PITCH, Bitmap.Config.ARGB_8888).asImageBitmap()
    ShaderBrush(ImageShader(image, TileMode.Repeated, TileMode.Repeated))
}

private const val SCANLINE_PITCH = 3

/** The CRT backdrop: the background with a phosphor bloom in the middle, scanlines and a vignette. */
@Composable
fun CrtBackdrop(
    scheme: ColorScheme,
    isDark: Boolean,
    scene: CrtBackdropScene = CrtBackdropScene.GREEN,
    modifier: Modifier = Modifier,
) {
    Spacer(
        modifier.drawWithCache {
            val background = scheme.background
            val center = size.center
            val radius = size.maxDimension * 0.75f
            val phosphor = when (scene) {
                CrtBackdropScene.GREEN -> Color(0xFF54FF8B)
                CrtBackdropScene.AMBER -> Color(0xFFFFB347)
                CrtBackdropScene.BLUE -> Color(0xFF69B7FF)
            }
            val bloomColor = lerp(scheme.primary, phosphor, 0.6f)
            val bloom = Brush.radialGradient(listOf(lerp(background, bloomColor, crtBloom(isDark)), background), center, radius)
            val vignette = Brush.radialGradient(
                0.5f to Color.Transparent,
                1f to Color.Black.copy(alpha = crtVignette(isDark)),
                center = center,
                radius = radius,
            )
            val lines = crtScanline(isDark)
            onDrawBehind {
                drawRect(bloom)
                drawRect(scanlines, alpha = lines)
                drawRect(vignette)
            }
        },
    )
}

private val LINE = 1.5.dp
private val INNER_LINE = 1.dp
private val LINE_GAP = 2.5.dp
private val PLATE_REACH = 4.dp

/** A box-drawing double line just inside the edge, like ═ and ║ around a terminal window. */
private fun DrawScope.drawDoubleLine(color: Color) {
    val outer = LINE.toPx()
    val inner = INNER_LINE.toPx()
    drawRect(color, Offset(outer / 2, outer / 2), Size(size.width - outer, size.height - outer), style = Stroke(outer))
    val inset = outer + LINE_GAP.toPx() + inner / 2
    drawRect(color, Offset(inset, inset), Size(size.width - inset * 2, size.height - inset * 2), style = Stroke(inner))
}

/** CRT terminal: VT323, a phosphor glow, scanlines behind boxed windows, and a blinking cursor. */
object CrtUi : SolidUi() {
    private val squareShapes = Shapes(extraSmall = Square, small = Square, medium = Square, large = Square, extraLarge = Square)

    override val typography: Typography get() = TerminalTypography
    override val shapes: Shapes get() = squareShapes
    override val pill: CornerBasedShape get() = Square
    override val capitalHeaders: Boolean get() = true
    override val titleCursor: Boolean get() = true

    @Composable
    override fun titleStyle(base: TextStyle) = base.phosphor(MaterialTheme.colorScheme.primary)

    @Composable
    override fun titleColor(onPanel: Boolean) = MaterialTheme.colorScheme.primary

    @Composable
    override fun sectionStyle(base: TextStyle) =
        TerminalTypography.labelLarge.copy(fontSize = 18.sp).phosphor(MaterialTheme.colorScheme.primary)

    @Composable
    override fun sectionRule(): Modifier {
        val color = MaterialTheme.colorScheme.primary.copy(alpha = 0.7f)
        return Modifier.drawBehind {
            val line = INNER_LINE.toPx()
            val gap = LINE_GAP.toPx()
            val top = (size.height - gap) / 2
            drawRect(color, Offset(0f, top - line), Size(size.width, line))
            drawRect(color, Offset(0f, top + gap), Size(size.width, line))
        }
    }

    @Composable
    override fun Backdrop(modifier: Modifier) =
        CrtBackdrop(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark, LocalPhobosTheme.current.crtBackdrop, modifier)

    @Composable
    override fun plate(): Modifier {
        val background = MaterialTheme.colorScheme.background
        return Modifier.drawBehind {
            val reach = PLATE_REACH.toPx()
            drawRect(background, Offset(-reach, -reach), Size(size.width + reach * 2, size.height + reach * 2))
        }
    }

    @Composable
    override fun panel(shape: Shape, fill: Color): Modifier {
        val line = MaterialTheme.colorScheme.primary
        return Modifier.drawWithContent {
            drawRect(fill)
            drawContent()
            drawDoubleLine(line)
        }
    }

    @Composable
    override fun edge(shape: Shape): Modifier {
        val line = MaterialTheme.colorScheme.primary
        return Modifier.drawWithContent {
            drawContent()
            drawDoubleLine(line)
        }
    }

    @Composable
    override fun menuBorder() = BorderStroke(LINE, MaterialTheme.colorScheme.primary)

    override val dockShape: Shape get() = Square
    override val dockTabShape: Shape get() = Square
    override fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme) =
        drawRect(colors.primary, size = size, alpha = alpha)

    @Composable
    override fun dockIconColor() = MaterialTheme.colorScheme.onPrimary

    @Composable
    override fun Switch(checked: Boolean) = CrtSwitch(checked)

    @Composable
    override fun SliderThumb() = CrtSliderThumb()

    @Composable
    override fun SliderTrack(fraction: Float) = CrtBar(fraction = { fraction }, sweep = null, modifier = Modifier)

    @Composable
    override fun ProgressBar(progress: (() -> Float)?, modifier: Modifier) {
        val sweep = if (progress == null) {
            rememberInfiniteTransition(label = "crtProgress")
                .animateFloat(0f, 1f, infiniteRepeatable(tween(durationMillis = 1600, easing = LinearEasing)), label = "crtProgressSweep")
        } else {
            null
        }
        CrtBar(progress, sweep?.let { { it.value } }, modifier)
    }
}

private val SwitchLabel = TerminalTypography.labelLarge.copy(fontSize = 20.sp)

/** A terminal's switch: ON in inverse video, or OFF in a box. */
@Composable
private fun CrtSwitch(checked: Boolean) {
    val scheme = MaterialTheme.colorScheme
    val measurer = rememberTextMeasurer()
    val label = remember(checked, scheme, measurer) {
        measurer.measure(if (checked) "ON" else "OFF", SwitchLabel.copy(color = if (checked) scheme.onPrimary else scheme.onSurfaceVariant))
    }
    val widest = remember(measurer) { measurer.measure("OFF", SwitchLabel).size.width }
    val width = with(LocalDensity.current) { maxOf(widest.toDp() + 20.dp, 56.dp) }
    Canvas(Modifier.size(width = width, height = 30.dp)) {
        if (checked) {
            drawRect(scheme.primary)
        } else {
            val line = LINE.toPx()
            drawRect(scheme.outline, Offset(line / 2, line / 2), Size(size.width - line, size.height - line), style = Stroke(line))
        }
        drawText(label, topLeft = Offset((size.width - label.size.width) / 2f, (size.height - label.size.height) / 2f))
    }
}

/** A slider's thumb: a block cursor in the primary color, cut out of the bar by a gap in the card's color. */
@Composable
private fun CrtSliderThumb() {
    val scheme = MaterialTheme.colorScheme
    Canvas(Modifier.size(width = 14.dp, height = 30.dp)) {
        drawRect(scheme.surfaceContainer)
        val gap = 2.dp.toPx()
        drawRect(scheme.primary, Offset(gap, gap), Size(size.width - gap * 2, size.height - gap * 2))
    }
}

/**
 * A boxed bar of character-cell blocks: lit in the primary color up to [fraction], or, when [sweep]
 * is set, a short run of lit blocks crossing it; the rest glow faintly, like ░.
 */
@Composable
private fun CrtBar(fraction: (() -> Float)?, sweep: (() -> Float)?, modifier: Modifier) {
    val scheme = MaterialTheme.colorScheme
    Canvas(modifier.fillMaxWidth().height(16.dp)) {
        val line = LINE.toPx()
        drawRect(scheme.primary, Offset(line / 2, line / 2), Size(size.width - line, size.height - line), style = Stroke(line))
        val pad = line + 2.dp.toPx()
        val block = 4.dp.toPx()
        val pitch = block + 2.dp.toPx()
        val count = ((size.width - pad * 2 + pitch - block) / pitch).toInt().coerceAtLeast(1)
        val run = 5
        val lit: Pair<Int, Int> = when {
            sweep != null -> (sweep() * (count + run)).toInt().let { head -> head - run to head }
            fraction != null -> 0 to (fraction().coerceIn(0f, 1f) * count).toInt()
            else -> 0 to 0
        }
        val (first, end) = lit
        for (i in 0 until count) {
            val start = pad + i * pitch
            val x = if (layoutDirection == LayoutDirection.Ltr) start else size.width - start - block
            val color = if (i in first until end) scheme.primary else scheme.primary.copy(alpha = 0.22f)
            drawRect(color, Offset(x, pad), Size(block, size.height - pad * 2))
        }
    }
}
