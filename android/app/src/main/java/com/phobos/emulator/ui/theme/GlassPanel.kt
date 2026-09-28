package com.phobos.emulator.ui.theme

import android.graphics.BlurMaskFilter
import android.graphics.RuntimeShader
import android.os.Build
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Spacer
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.ClipOp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.LinearGradientShader
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.PaintingStyle
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.asComposeRenderEffect
import androidx.compose.ui.graphics.drawscope.ContentDrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.isSpecified
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.node.CompositionLocalConsumerModifierNode
import androidx.compose.ui.node.DrawModifierNode
import androidx.compose.ui.node.GlobalPositionAwareModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.currentValueOf
import androidx.compose.ui.node.invalidateDraw
import androidx.compose.ui.node.requireGraphicsContext
import androidx.compose.ui.platform.InspectorInfo
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import kotlin.math.min

/**
 * Draws a glass panel of [shape] around the content: a soft shadow and a tight contact shadow
 * outside the panel only, so they never show through the translucent fill; [fill] at [alpha]; the
 * style's gloss from the top left and shade toward the bottom right; a bevel inside the edge, light
 * along the top and dark along the bottom; then, over the content, a hairline [rim] that is
 * brightest along the top edge, scaled by the style's rim strength, or a solid [outline] in its
 * place. Paths, brushes and paints are cached per size, so a color change (the theme's cross-fade)
 * only redraws. A panel floating [overPages] (the dock) draws its fill, when the style's refraction
 * is on (Android 13 and later), over the pages and backdrop behind it as seen through a lens at its
 * edge. Other panels keep the plain fill: a lens renders offscreen every frame, and over the smooth
 * glows it would barely show.
 */
fun Modifier.glassPanel(
    shape: Shape,
    fill: Color,
    alpha: Float,
    style: GlassStyle,
    isDark: Boolean,
    rim: Boolean = true,
    shadow: Boolean = true,
    outline: Color = Color.Unspecified,
    overPages: Boolean = false,
): Modifier = this then GlassPanelElement(
    shape, fill, alpha, style.glossAlpha, style.shadeAlpha, if (shadow) style.shadowAlpha else 0f,
    if (shadow) style.contactShadowAlpha else 0f, style.bevelLight, style.bevelShade, isDark,
    if (rim) style.rimStrength else 0f, outline, style.refraction, overPages,
)

private data class GlassPanelElement(
    val shape: Shape,
    val fill: Color,
    val alpha: Float,
    val gloss: Float,
    val shade: Float,
    val shadow: Float,
    val contact: Float,
    val bevelLight: Float,
    val bevelShade: Float,
    val isDark: Boolean,
    val rim: Float,
    val outline: Color,
    val refraction: Float,
    val overPages: Boolean,
) : ModifierNodeElement<GlassPanelNode>() {
    override fun create() = GlassPanelNode(this)

    override fun update(node: GlassPanelNode) = node.update(this)

    override fun InspectorInfo.inspectableProperties() {
        name = "glassPanel"
    }
}

private class GlassPanelNode(private var spec: GlassPanelElement) :
    Modifier.Node(), DrawModifierNode, CompositionLocalConsumerModifierNode, GlobalPositionAwareModifierNode {
    private val path = Path()
    private val shadowPath = Path()
    private val shadowPaint = Paint()
    private val contactPath = Path()
    private val contactPaint = Paint()
    private val bevelPaint = Paint()
    private var bevel = false
    private var sheen: Brush? = null
    private var rimBrush: Brush? = null
    private var rimStroke = Stroke()
    private var cachedSize = Size.Unspecified
    private var cachedDirection: LayoutDirection? = null
    private var cachedDensity = 0f

    // Refraction: the panel's offset in the capture's root, its corner radius (negative when the
    // shape isn't a rounded rectangle), and the lens layer and shader with the uniforms they hold.
    private var origin = Offset.Zero
    private var cornerRadius = -1f
    private var lensLayer: GraphicsLayer? = null
    private var lensShader: Any? = null
    private var lensSize = Size.Unspecified
    private var lensFill = Color.Unspecified
    private var lensAlpha = -1f
    private var lensStrength = -1f
    private var lensRadius = -1f
    private var lensCapture: GlassCapture? = null
    private val redrawLens: () -> Unit = { invalidateDraw() }

    fun update(new: GlassPanelElement) {
        val old = spec
        spec = new
        if (new.shape != old.shape || new.gloss != old.gloss || new.shade != old.shade || new.shadow != old.shadow ||
            new.contact != old.contact || new.bevelLight != old.bevelLight || new.bevelShade != old.bevelShade ||
            new.isDark != old.isDark || new.rim != old.rim
        ) {
            cachedSize = Size.Unspecified
        }
        invalidateDraw()
    }

    override fun onGloballyPositioned(coordinates: LayoutCoordinates) {
        // Tracked even while refraction is off, so turning it on doesn't bend the wrong part of the screen.
        if (!spec.overPages) return
        val root = currentValueOf(LocalGlassCapture)?.root ?: return
        if (root.isAttached && coordinates.isAttached) origin = root.localPositionOf(coordinates, Offset.Zero)
    }

    override fun onDetach() {
        lensLayer?.let { requireGraphicsContext().releaseGraphicsLayer(it) }
        lensLayer = null
        lensSize = Size.Unspecified
        lensCapture?.let { if (it.onRecorded === redrawLens) it.onRecorded = null }
        lensCapture = null
    }

    override fun ContentDrawScope.draw() {
        if (size != cachedSize || layoutDirection != cachedDirection || density != cachedDensity) rebuild()
        val spec = spec
        if ((spec.shadow > 0f || spec.contact > 0f) && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            clipPath(path, ClipOp.Difference) {
                drawIntoCanvas {
                    if (spec.shadow > 0f) it.drawPath(shadowPath, shadowPaint)
                    if (spec.contact > 0f) it.drawPath(contactPath, contactPaint)
                }
            }
        }
        val capture = if (spec.overPages && spec.refraction > 0f && cornerRadius >= 0f && !lensUnavailable && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            currentValueOf(LocalGlassCapture)
        } else {
            null
        }
        if (capture == null || !drawThroughLens(capture)) drawPath(path, spec.fill, alpha = spec.alpha)
        sheen?.let { drawPath(path, it) }
        if (bevel) clipPath(path) { drawIntoCanvas { it.drawPath(path, bevelPaint) } }
        drawContent()
        if (spec.outline.isSpecified) drawPath(path, spec.outline, style = rimStroke)
        else if (spec.rim > 0f) rimBrush?.let { drawPath(path, it, style = rimStroke) }
    }

    /**
     * Draws the fill over the scene behind the panel, bent at its edge by the refraction lens, or
     * returns false (and turns the lens off for good) if the device can't compile the shader.
     */
    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    private fun ContentDrawScope.drawThroughLens(capture: GlassCapture): Boolean {
        if (capture.backdropAlpha() < 1f) return false
        val spec = spec
        if (size != lensSize || spec.fill != lensFill || spec.alpha != lensAlpha || spec.refraction != lensStrength || cornerRadius != lensRadius) {
            val shader = lensShader as? RuntimeShader ?: try {
                RuntimeShader(REFRACTION_SHADER).also { lensShader = it }
            } catch (e: IllegalArgumentException) {
                Log.w("PhobosGlass", "Refraction shader unavailable; drawing glass without it", e)
                lensUnavailable = true
                return false
            }
            shader.setFloatUniform("size", size.width, size.height)
            shader.setFloatUniform("radius", cornerRadius)
            shader.setFloatUniform("band", LENS_BAND.toPx())
            shader.setFloatUniform("strength", LENS_STRENGTH.toPx() * spec.refraction)
            shader.setFloatUniform("fill", spec.fill.red, spec.fill.green, spec.fill.blue, 1f)
            shader.setFloatUniform("alphas", spec.alpha, min(spec.alpha, LENS_RIM_ALPHA))
            // A RenderEffect keeps the uniforms the shader had when it was created, so it is rebuilt with them.
            val blur = LENS_BLUR.toPx()
            lensLayer().renderEffect = android.graphics.RenderEffect.createChainEffect(
                android.graphics.RenderEffect.createRuntimeShaderEffect(shader, "content"),
                android.graphics.RenderEffect.createBlurEffect(blur, blur, android.graphics.Shader.TileMode.CLAMP),
            ).asComposeRenderEffect()
            lensSize = size
            lensFill = spec.fill
            lensAlpha = spec.alpha
            lensStrength = spec.refraction
            lensRadius = cornerRadius
        }
        // The pages and backdrop are recorded again as they change, and each time the lens redraws with them.
        capture.onRecorded = redrawLens
        lensCapture = capture
        val layer = lensLayer()
        val origin = origin
        val pagesOrigin = capture.pagesOrigin
        layer.record {
            translate(-origin.x, -origin.y) {
                drawLayer(capture.backdrop)
                translate(pagesOrigin.x, pagesOrigin.y) { drawLayer(capture.pages) }
            }
        }
        drawLayer(layer)
        return true
    }

    private fun lensLayer(): GraphicsLayer = lensLayer ?: requireGraphicsContext().createGraphicsLayer().also { lensLayer = it }

    private fun ContentDrawScope.rebuild() {
        val spec = spec
        val outline = spec.shape.createOutline(size, layoutDirection, this)
        cornerRadius = when (outline) {
            is Outline.Rectangle -> 0f
            is Outline.Rounded -> outline.roundRect.topLeftCornerRadius.x
            is Outline.Generic -> -1f
        }
        path.reset()
        path.addOutline(outline)
        shadowPath.reset()
        shadowPath.addOutline(outline)
        shadowPath.translate(Offset(0f, SHADOW_OFFSET.toPx()))
        contactPath.reset()
        contactPath.addOutline(outline)
        contactPath.translate(Offset(0f, CONTACT_OFFSET.toPx()))
        // Hardware-accelerated canvases ignore mask filters before Android 9; draw() skips the shadows and bevel there.
        val masks = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
        if (spec.shadow > 0f && masks) {
            shadowPaint.color = Color.Black.copy(alpha = spec.shadow)
            shadowPaint.asFrameworkPaint().maskFilter = BlurMaskFilter(SHADOW_BLUR.toPx(), BlurMaskFilter.Blur.NORMAL)
        }
        if (spec.contact > 0f && masks) {
            contactPaint.color = Color.Black.copy(alpha = spec.contact)
            contactPaint.asFrameworkPaint().maskFilter = BlurMaskFilter(CONTACT_BLUR.toPx(), BlurMaskFilter.Blur.NORMAL)
        }
        bevel = (spec.bevelLight > 0f || spec.bevelShade > 0f) && masks
        if (bevel) {
            // Centered on the outline and clipped to the panel, so half the stroke shows, softened inward.
            bevelPaint.style = PaintingStyle.Stroke
            bevelPaint.strokeWidth = BEVEL_STROKE.toPx()
            bevelPaint.shader = LinearGradientShader(
                from = Offset.Zero,
                to = Offset(0f, size.height),
                colors = listOf(
                    Color.White.copy(alpha = spec.bevelLight), Color.White.copy(alpha = 0f),
                    Color.Black.copy(alpha = 0f), Color.Black.copy(alpha = spec.bevelShade),
                ),
                colorStops = listOf(0f, 0.5f, 0.5f, 1f),
            )
            bevelPaint.asFrameworkPaint().maskFilter = BlurMaskFilter(BEVEL_BLUR.toPx(), BlurMaskFilter.Blur.NORMAL)
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
        val rim = spec.rim
        rimBrush = if (spec.isDark) {
            Brush.verticalGradient(
                0f to Color.White.copy(alpha = 0.28f * rim),
                0.5f to Color.White.copy(alpha = 0.07f * rim),
                1f to Color.White.copy(alpha = 0.04f * rim),
                endY = size.height,
            )
        } else {
            Brush.verticalGradient(
                0f to Color.White.copy(alpha = 0.9f * rim),
                0.4f to Color.White.copy(alpha = 0f),
                0.4f to Color.Black.copy(alpha = 0f),
                1f to Color.Black.copy(alpha = 0.1f * rim),
                endY = size.height,
            )
        }
        rimStroke = Stroke(width = RIM.toPx())
        cachedSize = size
        cachedDirection = layoutDirection
        cachedDensity = density
    }

    private companion object {
        /** Set once if the device can't compile the refraction shader; every panel then draws a plain fill. */
        var lensUnavailable = false

        val SHADOW_OFFSET = 4.dp
        val SHADOW_BLUR = 12.dp
        val CONTACT_OFFSET = 1.5.dp
        val CONTACT_BLUR = 3.dp

        /** 4 dp inside the edge plus the blur, under the 8 dp text keeps from a panel's edge. */
        val BEVEL_STROKE = 8.dp
        val BEVEL_BLUR = 2.5.dp
        val RIM = 1.dp

        /** The lens works within the 8 dp band along the edge that text keeps clear of, so text keeps the full tint. */
        val LENS_BAND = 8.dp
        val LENS_STRENGTH = 6.dp
        val LENS_BLUR = 4.dp
        const val LENS_RIM_ALPHA = 0.3f
    }
}

/**
 * Soft color glows behind the app's screens, the aurora the glass panels sit on. They fade to a new
 * theme's colors alongside its cross-fade, and the drawing is cached until the size or colors change.
 */
@Composable
fun GlassBackdrop(modifier: Modifier = Modifier) {
    val glass = LocalPhobosTheme.current.glass
    val glows = glass.glows
    // Keyed by level so a Glass effects change applies at once; theme changes still fade.
    val colors = key(glass.level) {
        glows.map { glow ->
            animateColorAsState(glow.color.copy(alpha = glow.alpha), tween(durationMillis = 450), label = "glow").value
        }
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
            onDrawBehind { brushes.forEachIndexed { index, brush -> if (colors[index].alpha > 0f) drawRect(brush) } }
        },
    )
}
