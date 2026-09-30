package com.phobos.emulator.ui.theme

import androidx.compose.foundation.layout.Box
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.takeOrElse
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.min

/** The contrast an outline aims for against what it surrounds. */
internal const val OUTLINE_CONTRAST = 7f

/** How far an outline reaches past the text or icon it surrounds. */
val LegibilityOutlineWidth: Dp = 1.25.dp

/**
 * The outline for [content] drawn where the theme doesn't control what's behind it: the theme's
 * [primary] moved toward black or white, whichever [content] contrasts with more, until the two reach
 * [OUTLINE_CONTRAST], or all the way where they can't.
 */
fun legibilityOutline(content: Color, primary: Color): Color {
    val opaque = content.copy(alpha = 1f)
    val away = if (contrastRatio(opaque, Color.Black) >= contrastRatio(opaque, Color.White)) Color.Black else Color.White
    for (step in 0..SEARCH_STEPS) {
        val candidate = lerp(primary.copy(alpha = 1f), away, step / SEARCH_STEPS.toFloat())
        if (contrastRatio(candidate, opaque) >= OUTLINE_CONTRAST) return candidate
    }
    return away
}

/**
 * A Library tile's fill: the card color with its hue and colorfulness moved toward the theme's primary
 * at the card's own lightness (a light theme's primary is dark and a dark theme's often pale, so a
 * plain tint would move the tile toward its text), then away from its name's color if needed, until
 * the name has at least the contrast it has on a card.
 */
fun libraryTileFill(scheme: ColorScheme, isDark: Boolean): Color {
    val card = Oklab.of(scheme.surfaceContainer)
    val primary = Oklab.of(scheme.primary)
    val hued = Oklab(card.l, card.a + (primary.a - card.a) * TILE_TINT, card.b + (primary.b - card.b) * TILE_TINT)
    val tile = Oklab.lch(card.l, min(hued.chroma, Oklab.maxChroma(card.l, hued.hue)), hued.hue).toColor()
    val needed = contrastRatio(scheme.onSurface, scheme.surfaceContainer)
    val away = if (isDark) Color.Black else Color.White
    for (step in 0..SEARCH_STEPS) {
        val candidate = lerp(tile, away, step / SEARCH_STEPS.toFloat())
        if (contrastRatio(scheme.onSurface, candidate) >= needed) return candidate
    }
    return away
}

/** [Text] with a [legibilityOutline] around its glyphs, for text over backgrounds the theme doesn't control. */
@Composable
fun LegibleText(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = Color.Unspecified,
    style: TextStyle = LocalTextStyle.current,
    textAlign: TextAlign? = null,
    maxLines: Int = Int.MAX_VALUE,
    overflow: TextOverflow = TextOverflow.Clip,
) {
    val fill = color.takeOrElse { style.color.takeOrElse { LocalContentColor.current } }
    val primary = MaterialTheme.colorScheme.primary
    val outline = remember(fill, primary) { legibilityOutline(fill, primary) }
    // The stroke is centered on the glyphs' edges, so it takes twice the reach; the text covers the inner half.
    val stroke = with(LocalDensity.current) { Stroke(width = LegibilityOutlineWidth.toPx() * 2f, join = StrokeJoin.Round) }
    Box(modifier, propagateMinConstraints = true) {
        // Translucent text fades with its outline; drawn at its own alpha, the outline would show through it.
        Box(fadeTo(fill.alpha), propagateMinConstraints = true) {
            Text(
                text,
                Modifier.clearAndSetSemantics {},
                color = outline,
                style = style.copy(drawStyle = stroke, shadow = null),
                textAlign = textAlign,
                maxLines = maxLines,
                overflow = overflow,
            )
            Text(text, color = fill.copy(alpha = 1f), style = style, textAlign = textAlign, maxLines = maxLines, overflow = overflow)
        }
    }
}

/** [Icon] with a [legibilityOutline] around it, for icons over backgrounds the theme doesn't control. */
@Composable
fun LegibleIcon(imageVector: ImageVector, contentDescription: String?, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current) =
    LegibleIcon(rememberVectorPainter(imageVector), contentDescription, modifier, tint)

/** [Icon] with a [legibilityOutline] around it, for icons over backgrounds the theme doesn't control. */
@Composable
fun LegibleIcon(painter: Painter, contentDescription: String?, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current) {
    val primary = MaterialTheme.colorScheme.primary
    val outline = remember(tint, primary) { legibilityOutline(tint, primary) }
    Icon(painter, contentDescription, modifier.then(fadeTo(tint.alpha)).legibleOutline(outline), tint.copy(alpha = 1f))
}

private fun fadeTo(alpha: Float): Modifier = if (alpha < 1f) Modifier.graphicsLayer { this.alpha = alpha } else Modifier

/** Draws [color] [width] wide around the silhouette of whatever the content draws, under it. */
fun Modifier.legibleOutline(color: Color, width: Dp = LegibilityOutlineWidth): Modifier = drawWithCache {
    val reach = width.toPx()
    val silhouette = Paint().apply { colorFilter = ColorFilter.tint(color, BlendMode.SrcIn) }
    val bounds = Rect(-reach, -reach, size.width + reach, size.height + reach)
    onDrawWithContent {
        drawIntoCanvas { canvas ->
            canvas.saveLayer(bounds, silhouette)
            for ((dx, dy) in OUTLINE_DIRECTIONS) translate(dx * reach, dy * reach) { this@onDrawWithContent.drawContent() }
            canvas.restore()
        }
        drawContent()
    }
}

private const val SEARCH_STEPS = 50
private const val TILE_TINT = 0.3f
private const val DIAGONAL = 0.7071f
private val OUTLINE_DIRECTIONS = listOf(
    1f to 0f, -1f to 0f, 0f to 1f, 0f to -1f,
    DIAGONAL to DIAGONAL, DIAGONAL to -DIAGONAL, -DIAGONAL to DIAGONAL, -DIAGONAL to -DIAGONAL,
)
