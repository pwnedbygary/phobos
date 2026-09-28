package com.phobos.emulator.ui.theme

import android.graphics.BlurMaskFilter
import android.os.Build
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Spacer
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.ClipOp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawscope.ContentDrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.node.DrawModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.invalidateDraw
import androidx.compose.ui.platform.InspectorInfo
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp

/**
 * Draws a glass panel of [shape] around the content: a soft shadow outside the panel only, so it
 * never shows through the translucent fill; [fill] at [alpha]; the style's gloss from the top left
 * and shade toward the bottom right; then, over the content, a hairline [rim] that is brightest
 * along the top edge. Paths and brushes are cached per size, so a color change (the theme's
 * cross-fade) only redraws.
 */
fun Modifier.glassPanel(
    shape: Shape,
    fill: Color,
    alpha: Float,
    style: GlassStyle,
    isDark: Boolean,
    rim: Boolean = true,
    shadow: Boolean = true,
): Modifier = this then GlassPanelElement(
    shape, fill, alpha, style.glossAlpha, style.shadeAlpha, if (shadow) style.shadowAlpha else 0f, isDark, rim,
)

private data class GlassPanelElement(
    val shape: Shape,
    val fill: Color,
    val alpha: Float,
    val gloss: Float,
    val shade: Float,
    val shadow: Float,
    val isDark: Boolean,
    val rim: Boolean,
) : ModifierNodeElement<GlassPanelNode>() {
    override fun create() = GlassPanelNode(this)

    override fun update(node: GlassPanelNode) = node.update(this)

    override fun InspectorInfo.inspectableProperties() {
        name = "glassPanel"
    }
}

private class GlassPanelNode(private var spec: GlassPanelElement) : Modifier.Node(), DrawModifierNode {
    private val path = Path()
    private val shadowPath = Path()
    private val shadowPaint = Paint()
    private var sheen: Brush? = null
    private var rimBrush: Brush? = null
    private var rimStroke = Stroke()
    private var cachedSize = Size.Unspecified
    private var cachedDirection: LayoutDirection? = null
    private var cachedDensity = 0f

    fun update(new: GlassPanelElement) {
        val old = spec
        spec = new
        if (new.shape != old.shape || new.gloss != old.gloss || new.shade != old.shade || new.shadow != old.shadow || new.isDark != old.isDark) {
            cachedSize = Size.Unspecified
        }
        invalidateDraw()
    }

    override fun ContentDrawScope.draw() {
        if (size != cachedSize || layoutDirection != cachedDirection || density != cachedDensity) rebuild()
        val spec = spec
        if (spec.shadow > 0f && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            clipPath(path, ClipOp.Difference) { drawIntoCanvas { it.drawPath(shadowPath, shadowPaint) } }
        }
        drawPath(path, spec.fill, alpha = spec.alpha)
        sheen?.let { drawPath(path, it) }
        drawContent()
        if (spec.rim) rimBrush?.let { drawPath(path, it, style = rimStroke) }
    }

    private fun ContentDrawScope.rebuild() {
        val spec = spec
        val outline = spec.shape.createOutline(size, layoutDirection, this)
        path.reset()
        path.addOutline(outline)
        shadowPath.reset()
        shadowPath.addOutline(outline)
        shadowPath.translate(Offset(0f, SHADOW_OFFSET.toPx()))
        // Hardware-accelerated canvases ignore mask filters before Android 9; draw() skips the shadow there.
        if (spec.shadow > 0f && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            shadowPaint.color = Color.Black.copy(alpha = spec.shadow)
            shadowPaint.asFrameworkPaint().maskFilter = BlurMaskFilter(SHADOW_BLUR.toPx(), BlurMaskFilter.Blur.NORMAL)
        }
        val corner = Offset(size.width, size.height)
        sheen = if (spec.gloss > 0f || spec.shade > 0f) {
            Brush.linearGradient(
                0f to Color.White.copy(alpha = spec.gloss),
                0.45f to Color.White.copy(alpha = 0f),
                0.55f to Color.Black.copy(alpha = 0f),
                1f to Color.Black.copy(alpha = spec.shade),
                start = Offset.Zero,
                end = corner,
            )
        } else {
            null
        }
        rimBrush = if (spec.isDark) {
            Brush.verticalGradient(0f to Color.White.copy(alpha = 0.28f), 0.5f to Color.White.copy(alpha = 0.07f), 1f to Color.White.copy(alpha = 0.04f), endY = size.height)
        } else {
            Brush.verticalGradient(
                0f to Color.White.copy(alpha = 0.9f),
                0.4f to Color.White.copy(alpha = 0f),
                0.4f to Color.Black.copy(alpha = 0f),
                1f to Color.Black.copy(alpha = 0.1f),
                endY = size.height,
            )
        }
        rimStroke = Stroke(width = RIM.toPx())
        cachedSize = size
        cachedDirection = layoutDirection
        cachedDensity = density
    }

    private companion object {
        val SHADOW_OFFSET = 4.dp
        val SHADOW_BLUR = 12.dp
        val RIM = 1.dp
    }
}

/**
 * Soft color glows behind the app's screens, the aurora the glass panels sit on. They fade to a new
 * theme's colors alongside its cross-fade, and the drawing is cached until the size or colors change.
 */
@Composable
fun GlassBackdrop(modifier: Modifier = Modifier) {
    val glows = LocalPhobosTheme.current.glass.glows
    val colors = glows.map { glow ->
        animateColorAsState(glow.color.copy(alpha = glow.alpha), tween(durationMillis = 450), label = "glow").value
    }
    Spacer(
        modifier.drawWithCache {
            val extent = size.maxDimension
            val brushes = glows.mapIndexed { index, glow ->
                val color = colors[index]
                Brush.radialGradient(
                    0f to color,
                    0.5f to color.copy(alpha = color.alpha * 0.45f),
                    1f to color.copy(alpha = 0f),
                    center = Offset(glow.x * size.width, glow.y * size.height),
                    radius = glow.radius * extent,
                )
            }
            onDrawBehind { brushes.forEach { drawRect(it) } }
        },
    )
}
