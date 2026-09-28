package com.phobos.emulator.ui.theme

import androidx.compose.runtime.Stable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.layout.LayoutCoordinates

/**
 * What the dock refracts: the backdrop and the pages under it, recorded into graphics layers as
 * they draw. [root] is the coordinate space the dock measures its offset in, and [pagesOrigin]
 * where the pages sit in it. [onRecorded] runs after either is recorded again: the dock sits beside
 * the pages rather than inside them, so it isn't redrawn when they scroll unless it is told.
 * [backdropAlpha] reads the backdrop's fade; the recording isn't redone as it fades (and stops while
 * a game hides it), so the dock only uses the lens once the backdrop is fully shown.
 */
@Stable
class GlassCapture(val backdrop: GraphicsLayer, val pages: GraphicsLayer) {
    var root: LayoutCoordinates? = null
    var pagesOrigin: Offset = Offset.Zero
    var onRecorded: (() -> Unit)? = null
    var backdropAlpha: () -> Float = { 1f }
}

val LocalGlassCapture = staticCompositionLocalOf<GlassCapture?> { null }

/** Draws the content as usual and records it into [layer] for the dock to refract, then calls [onRecorded]. */
fun Modifier.recordForGlass(layer: GraphicsLayer, onRecorded: () -> Unit): Modifier = drawWithContent {
    layer.record { this@drawWithContent.drawContent() }
    drawLayer(layer)
    onRecorded()
}

/**
 * The lens the dock draws the scene behind it through. Within [band] of the edge the scene bends
 * inward by up to [strength] (red a little less and blue a little more), and the tint thins from
 * alphas.x inside to alphas.y at the rim, so the bending shows there; text keeps out of that band,
 * so it always sits over the full alpha. Outside the rounded rectangle nothing is drawn.
 */
internal const val REFRACTION_SHADER = """
uniform shader content;
uniform float2 size;
uniform float radius;
uniform float band;
uniform float strength;
uniform float4 fill;
uniform float2 alphas;

float roundRectDistance(float2 p, float2 halfSize, float r) {
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 p = coord - halfSize;
    float d = roundRectDistance(p, halfSize, radius);
    float2 normal = normalize(float2(
        roundRectDistance(p + float2(1.0, 0.0), halfSize, radius) - roundRectDistance(p - float2(1.0, 0.0), halfSize, radius),
        roundRectDistance(p + float2(0.0, 1.0), halfSize, radius) - roundRectDistance(p - float2(0.0, 1.0), halfSize, radius)
    ) + 0.00001);
    float rim = 1.0 - clamp(-d / band, 0.0, 1.0);
    float2 bend = -normal * rim * rim * strength;
    half4 scene = content.eval(coord + bend);
    half red = content.eval(coord + bend * 0.85).r;
    half blue = content.eval(coord + bend * 1.15).b;
    scene = half4(min(red, scene.a), scene.g, min(blue, scene.a), scene.a);
    half tint = half(mix(alphas.x, alphas.y, smoothstep(0.0, 1.0, rim)));
    half4 glass = scene * (1.0 - tint) + half4(half3(fill.rgb), 1.0) * tint;
    return glass * half(clamp(0.5 - d, 0.0, 1.0));
}
"""
