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
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.CacheDrawScope
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.DrawScope
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
import com.phobos.emulator.data.RpgBackdropScene
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * DotGothic16 (SIL Open Font License 1.1; see LICENSE), a 16-dot font like the text of a Japanese
 * 16-bit RPG. It is cut down to Latin, Greek, Cyrillic and symbols; other scripts use the system font.
 */
val RpgFont = FontFamily(Font(R.font.dotgothic16))

private fun TextStyle.inRpgFont(): TextStyle = copy(
    fontFamily = RpgFont,
    fontWeight = FontWeight.Normal,
    letterSpacing = 0.sp,
    fontSynthesis = FontSynthesis.None,
)

/** The app's type scale in DotGothic16. It has one weight, and synthesized bold would smear its dots. */
val RpgTypography = with(PhobosTypography) {
    Typography(
        displayLarge = displayLarge.inRpgFont(),
        displayMedium = displayMedium.inRpgFont(),
        displaySmall = displaySmall.inRpgFont(),
        headlineLarge = headlineLarge.inRpgFont(),
        headlineMedium = headlineMedium.inRpgFont(),
        headlineSmall = headlineSmall.inRpgFont(),
        titleLarge = titleLarge.inRpgFont(),
        titleMedium = titleMedium.inRpgFont(),
        titleSmall = titleSmall.inRpgFont(),
        bodyLarge = bodyLarge.inRpgFont(),
        bodyMedium = bodyMedium.inRpgFont(),
        bodySmall = bodySmall.inRpgFont(),
        labelLarge = labelLarge.inRpgFont(),
        labelMedium = labelMedium.inRpgFont(),
        labelSmall = labelSmall.inRpgFont(),
    )
}

/** One art pixel of the window frames, the gauges and the backdrop's lattice. */
private val ART = 1.5.dp

private val RpgShapes = Shapes(
    extraSmall = PixelShape(ART, 1),
    small = PixelShape(ART, 2),
    medium = PixelShape(ART, 3),
    large = PixelShape(2.dp, 3),
    extraLarge = PixelShape(2.dp, 3),
)

private val RpgPill = PixelShape(ART, 2)

/** A window's fill at its top: the panel's color tinted toward the primary color ([tintKeepingContrast]). */
internal fun rpgWindowTop(fill: Color, primary: Color, isDark: Boolean): Color = tintKeepingContrast(fill, primary, 0.16f, isDark)

/** A window's fill at its bottom: deeper in dark themes and paler in light ones, the far end of the gradient. */
internal fun rpgWindowBottom(fill: Color, isDark: Boolean): Color =
    if (isDark) lerp(fill, Color.Black, 0.35f) else lerp(fill, Color.White, 0.5f)

/** The frame's band: silver in dark themes, like the classic menu window, and the primary color in light ones. */
internal fun rpgFrameBand(scheme: ColorScheme, isDark: Boolean): Color =
    if (isDark) lerp(scheme.onSurface, scheme.surfaceContainer, 0.15f) else scheme.primary

/** The dark lines either side of the frame's band. */
internal fun rpgFrameEdge(scheme: ColorScheme, isDark: Boolean): Color = if (isDark) Color.Black else scheme.onSurface

/** The backdrop's lattice lines. */
internal fun rpgLattice(scheme: ColorScheme, isDark: Boolean): Color =
    lerp(scheme.background, scheme.primary, if (isDark) 0.12f else 0.14f)

/** A gauge's empty trough. */
internal fun rpgTrough(scheme: ColorScheme, isDark: Boolean): Color =
    if (isDark) lerp(scheme.surfaceContainer, Color.Black, 0.5f) else scheme.surfaceContainerHighest

/** A window frame's paths: dark [lines] four art pixels wide, with a [band] two wide over their middle. */
private class WindowFrame(val lines: Path, val band: Path)

/** The frame just inside [shape]: a dark line, a band two art pixels wide and another dark line. */
private fun CacheDrawScope.windowFrame(shape: Shape): WindowFrame {
    val art = ART.toPx()
    return WindowFrame(
        lines = ring(shape, size, layoutDirection, 0f, art * 4),
        band = ring(shape, size, layoutDirection, art, art * 3),
    )
}

private fun DrawScope.drawWindowFrame(frame: WindowFrame, band: Color, edge: Color) {
    drawPath(frame.lines, edge)
    drawPath(frame.band, band)
}

/** A title's hard drop shadow, one of the font's dots down and right, like 16-bit menu text. */
private fun TextStyle.rpgShadow(isDark: Boolean, dot: Float): TextStyle = copy(
    shadow = Shadow(color = Color.Black.copy(alpha = if (isDark) 0.8f else 0.2f), offset = Offset(dot, dot), blurRadius = 0f),
)

/** The size of one of DotGothic16's dots at [style]'s size, in whole pixels. */
@Composable
private fun fontDot(style: TextStyle): Float = with(LocalDensity.current) { max(1f, (style.fontSize.toPx() / 16f).roundToInt().toFloat()) }

/** The lattice's diamonds, in art pixels across. */
private const val LATTICE = 16

/** The RPG backdrop: a menu lattice, starfield, or torchlit dungeon wall. */
@Composable
fun RpgBackdrop(
    scheme: ColorScheme,
    isDark: Boolean,
    scene: RpgBackdropScene = RpgBackdropScene.LATTICE,
    modifier: Modifier = Modifier,
) {
    Spacer(
        modifier.drawWithCache {
            when (scene) {
                RpgBackdropScene.LATTICE -> {
                    val art = max(1, ART.toPx().roundToInt())
                    val side = LATTICE * art
                    val line = rpgLattice(scheme, isDark).toArgb()
                    val background = scheme.background.toArgb()
                    val pixels = IntArray(side * side) { i ->
                        val x = (i % side) / art
                        val y = (i / side) / art
                        if ((x + y) % LATTICE == 0 || (x - y + LATTICE) % LATTICE == 0) line else background
                    }
                    val tile = Bitmap.createBitmap(pixels, side, side, Bitmap.Config.ARGB_8888).asImageBitmap()
                    val brush = ShaderBrush(ImageShader(tile, TileMode.Repeated, TileMode.Repeated))
                    onDrawBehind { drawRect(brush) }
                }
                RpgBackdropScene.STARS -> {
                    val sky = lerp(scheme.background, Color(0xFF0B1028), if (isDark) 0.62f else 0.28f)
                    val star = lerp(scheme.onBackground, Color.White, 0.5f)
                    onDrawBehind {
                        drawRect(sky)
                        repeat(86) { index ->
                            val x = ((index * 47) % 101) / 100f * size.width
                            val y = ((index * 71 + 13) % 103) / 102f * size.height
                            val twinkle = 0.3f + (index % 5) * 0.14f
                            drawCircle(star, radius = if (index % 11 == 0) 1.5.dp.toPx() else 0.7.dp.toPx(), center = Offset(x, y), alpha = twinkle)
                        }
                    }
                }
                RpgBackdropScene.DUNGEON -> {
                    val wall = lerp(scheme.background, Color.Black, if (isDark) 0.38f else 0.18f)
                    val mortar = lerp(wall, Color.Black, 0.5f)
                    val brick = lerp(wall, scheme.primary, 0.08f)
                    val glow = lerp(scheme.tertiary, Color(0xFFFFB34D), 0.55f)
                    onDrawBehind {
                        drawRect(wall)
                        val course = 26.dp.toPx()
                        val brickWidth = 58.dp.toPx()
                        var y = 0f
                        var row = 0
                        while (y < size.height) {
                            val shift = if (row % 2 == 0) 0f else brickWidth / 2f
                            var x = -shift
                            while (x < size.width) {
                                drawRect(brick, Offset(x + 1.dp.toPx(), y + 1.dp.toPx()), Size(brickWidth - 2.dp.toPx(), course - 2.dp.toPx()))
                                x += brickWidth
                            }
                            drawLine(mortar, Offset(0f, y), Offset(size.width, y), 1.dp.toPx())
                            y += course
                            row++
                        }
                        listOf(0.2f, 0.5f, 0.8f).forEach { x ->
                            drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = 0.24f), Color.Transparent), center = Offset(size.width * x, size.height * 0.46f), radius = 110.dp.toPx()), radius = 110.dp.toPx(), center = Offset(size.width * x, size.height * 0.46f))
                        }
                    }
                }
            }
        },
    )
}

/**
 * The pointing hand of 16-bit RPG menus, pointing right: X is its outline, W the glove and S the
 * shading between the curled fingers.
 */
private val HAND = listOf(
    "...XXX........",
    "..XWWWXXXXXXX.",
    ".XWWWWWWWWWWWX",
    "XWWWWWXXXXXXX.",
    "XWWWWWWWWX....",
    "XWWWWWSXXX....",
    "XWWWWWWWX.....",
    ".XWWWWSXX.....",
    "..XWWWWX......",
    "...XXXX.......",
)
private val HAND_WIDTH = HAND[0].length
private val HAND_HEIGHT = HAND.size
private val HAND_ART = 1.25.dp
private val HandOutline = Color(0xFF101018)
private val HandGlove = Color(0xFFF4F4F8)
private val HandShade = Color(0xFFB4B8C6)

/** [HAND] at [topLeft] in art pixels of [art], pointing left instead when [mirrored]. */
private fun DrawScope.drawHand(topLeft: Offset, art: Float, mirrored: Boolean, alpha: Float) {
    HAND.forEachIndexed { row, line ->
        line.forEachIndexed { column, cell ->
            val color = when (cell) {
                'X' -> HandOutline
                'W' -> HandGlove
                'S' -> HandShade
                else -> return@forEachIndexed
            }
            val x = if (mirrored) HAND_WIDTH - 1 - column else column
            drawRect(color, Offset(topLeft.x + x * art, topLeft.y + row * art), Size(art, art), alpha = alpha)
        }
    }
}

/** 16-bit RPG menu: framed windows with a gradient fill, DotGothic16, and a pointing-hand cursor. */
object RpgUi : SolidUi() {
    override val typography: Typography get() = RpgTypography
    override val shapes: Shapes get() = RpgShapes
    override val pill: CornerBasedShape get() = RpgPill

    @Composable
    override fun titleStyle(base: TextStyle) = base.rpgShadow(LocalPhobosTheme.current.isDark, fontDot(base))

    @Composable
    override fun sectionStyle(base: TextStyle): TextStyle {
        val style = RpgTypography.titleMedium
        return style.rpgShadow(LocalPhobosTheme.current.isDark, fontDot(style))
    }

    @Composable
    override fun Backdrop(modifier: Modifier) =
        RpgBackdrop(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark, LocalPhobosTheme.current.rpgBackdrop, modifier)

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
        val scheme = MaterialTheme.colorScheme
        val isDark = LocalPhobosTheme.current.isDark
        val top = remember(fill, scheme.primary, isDark) { rpgWindowTop(fill, scheme.primary, isDark) }
        val gradient = Brush.verticalGradient(listOf(top, rpgWindowBottom(fill, isDark)))
        val band = rpgFrameBand(scheme, isDark)
        val edge = rpgFrameEdge(scheme, isDark)
        return Modifier.drawWithCache {
            val outline = shape.createOutline(size, layoutDirection, this)
            val frame = windowFrame(shape)
            onDrawWithContent {
                drawOutline(outline, gradient)
                drawContent()
                drawWindowFrame(frame, band, edge)
            }
        }
    }

    @Composable
    override fun edge(shape: Shape): Modifier {
        val scheme = MaterialTheme.colorScheme
        val isDark = LocalPhobosTheme.current.isDark
        val band = rpgFrameBand(scheme, isDark)
        val edge = rpgFrameEdge(scheme, isDark)
        return Modifier.drawWithCache {
            val frame = windowFrame(shape)
            onDrawWithContent {
                drawContent()
                drawWindowFrame(frame, band, edge)
            }
        }
    }

    @Composable
    override fun menuBorder() = BorderStroke(ART * 2, rpgFrameBand(MaterialTheme.colorScheme, LocalPhobosTheme.current.isDark))

    override val dockShape: Shape = PixelShape(2.dp, 3)
    override val dockTabShape: Shape = PixelShape(ART, 2)

    /** The hand, at the start of the tab and pointing at its icon; it slides out as the indicator springs open. */
    override fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme) {
        val art = max(1f, HAND_ART.toPx().roundToInt().toFloat())
        val ltr = layoutDirection == LayoutDirection.Ltr
        // One art pixel past the indicator's start, so the fingertip stops short of the icon.
        val x = if (ltr) -art else size.width - (HAND_WIDTH - 1) * art
        val y = ((size.height - HAND_HEIGHT * art) / 2f).roundToInt().toFloat()
        drawHand(Offset(x.roundToInt().toFloat(), y), art, mirrored = !ltr, alpha = alpha)
    }

    @Composable
    override fun dockIconColor() = MaterialTheme.colorScheme.primary

    @Composable
    override fun Switch(checked: Boolean) = RpgSwitch(checked)

    @Composable
    override fun SliderThumb() = RpgSliderThumb()

    @Composable
    override fun SliderTrack(fraction: Float) = RpgGauge(fraction = { fraction }, sweep = null, modifier = Modifier)

    @Composable
    override fun ProgressBar(progress: (() -> Float)?, modifier: Modifier) {
        val sweep = if (progress == null) {
            rememberInfiniteTransition(label = "rpgProgress")
                .animateFloat(0f, 1f, infiniteRepeatable(tween(durationMillis = 1400, easing = LinearEasing)), label = "rpgProgressSweep")
        } else {
            null
        }
        RpgGauge(progress, sweep?.let { { it.value } }, modifier)
    }
}

private val PLATE_REACH = 4.dp

private val SwitchLabel = RpgTypography.labelLarge.copy(fontSize = 16.sp)

/** How far the option that isn't chosen fades. */
private const val UNCHOSEN_ALPHA = 0.45f

/** A config option from an RPG's menu: ON and OFF side by side, the chosen one lit and the other faded. */
@Composable
private fun RpgSwitch(checked: Boolean) {
    val scheme = MaterialTheme.colorScheme
    val measurer = rememberTextMeasurer()
    val unchosen = scheme.onSurfaceVariant.copy(alpha = UNCHOSEN_ALPHA)
    val on = remember(checked, scheme, measurer) { measurer.measure("ON", SwitchLabel.copy(color = if (checked) scheme.primary else unchosen)) }
    val off = remember(checked, scheme, measurer) { measurer.measure("OFF", SwitchLabel.copy(color = if (checked) unchosen else scheme.onSurface)) }
    val width = with(LocalDensity.current) { (on.size.width + off.size.width).toDp() + 12.dp }
    Canvas(Modifier.size(width = width, height = 30.dp)) {
        val ltr = layoutDirection == LayoutDirection.Ltr
        drawText(on, topLeft = Offset(if (ltr) 0f else size.width - on.size.width, (size.height - on.size.height) / 2f))
        drawText(off, topLeft = Offset(if (ltr) size.width - off.size.width else 0f, (size.height - off.size.height) / 2f))
    }
}

/** A slider's thumb: a short upright piece of window frame. */
@Composable
private fun RpgSliderThumb() {
    val scheme = MaterialTheme.colorScheme
    val isDark = LocalPhobosTheme.current.isDark
    val band = rpgFrameBand(scheme, isDark)
    val edge = rpgFrameEdge(scheme, isDark)
    Canvas(Modifier.size(width = 14.dp, height = 28.dp)) {
        val shape = PixelShape(ART, 1)
        drawOutline(shape.createOutline(size, layoutDirection, this), band)
        drawPath(ring(shape, size, layoutDirection, 0f, ART.toPx()), edge)
    }
}

/**
 * A gauge like an RPG's HP bar: a trough inside a dark line, lit with a glossy primary gradient up to
 * [fraction], or, when [sweep] is set, a short lit stretch crossing it.
 */
@Composable
private fun RpgGauge(fraction: (() -> Float)?, sweep: (() -> Float)?, modifier: Modifier) {
    val scheme = MaterialTheme.colorScheme
    val isDark = LocalPhobosTheme.current.isDark
    val trough = rpgTrough(scheme, isDark)
    val edge = rpgFrameEdge(scheme, isDark)
    val lit = Brush.verticalGradient(listOf(lerp(scheme.primary, Color.White, 0.45f), scheme.primary, lerp(scheme.primary, Color.Black, 0.25f)))
    Canvas(modifier.fillMaxWidth().height(14.dp)) {
        val art = ART.toPx()
        val shape = PixelShape(ART, 1)
        drawOutline(shape.createOutline(size, layoutDirection, this), trough)
        val inner = art * 2
        val span = size.width - inner * 2
        val range: Pair<Float, Float> = when {
            sweep != null -> (sweep() * 1.25f - 0.25f).let { head -> head to head + 0.25f }
            fraction != null -> 0f to fraction().coerceIn(0f, 1f)
            else -> 0f to 0f
        }
        val (start, end) = range
        val from = inner + span * start.coerceIn(0f, 1f)
        val to = inner + span * end.coerceIn(0f, 1f)
        if (to > from) {
            val ltr = layoutDirection == LayoutDirection.Ltr
            clipRect(left = if (ltr) from else size.width - to, right = if (ltr) to else size.width - from, top = inner, bottom = size.height - inner) {
                drawRect(lit)
            }
        }
        drawPath(ring(shape, size, layoutDirection, 0f, art), edge)
    }
}
