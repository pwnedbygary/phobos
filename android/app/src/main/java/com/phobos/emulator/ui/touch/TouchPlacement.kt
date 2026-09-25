package com.phobos.emulator.ui.touch

/** Insets in px that default positions stay clear of (the overlays pass the display cutout). */
data class ScreenInsets(val left: Float = 0f, val top: Float = 0f, val right: Float = 0f, val bottom: Float = 0f)

/**
 * Resolves every element of [layout] to a screen position for a [widthPx] x [heightPx] overlay.
 *
 * Default positions come from the element's landscape or portrait [Placement] (offsets scale with
 * [globalScale] so larger controls move away from the edges together) and avoid [insets]. A user
 * override replaces the position with a fraction of the overlay size. Every element is clamped
 * fully on screen. Hidden elements are skipped unless [includeHidden] (the layout editor).
 *
 * [mirror] swaps left and right (swap-hands): element positions are mirrored, while the
 * arrangement inside a button cluster is kept. Overrides stay stored unmirrored.
 */
fun placeLayout(
    layout: TouchLayout,
    overrides: Map<String, ElementOverride>,
    widthPx: Float,
    heightPx: Float,
    density: Float,
    globalScale: Float,
    insets: ScreenInsets = ScreenInsets(),
    includeHidden: Boolean = false,
    mirror: Boolean = false,
): List<PlacedElement> {
    if (widthPx <= 0f || heightPx <= 0f) return emptyList()
    val landscape = widthPx >= heightPx
    val offsetUnit = density * globalScale
    val placed = ArrayList<PlacedElement>(layout.elements.size)
    for (element in layout.elements) {
        val override = overrides[element.id]
        if (!includeHidden && isHidden(element, override, landscape)) continue

        val unit = offsetUnit * (override?.scale ?: 1f)
        var cx: Float
        var cy: Float
        if (override?.fx != null && override.fy != null) {
            cx = mirrorFraction(override.fx, mirror) * widthPx
            cy = override.fy * heightPx
        } else {
            val p = if (landscape) element.land else element.port
            val anchorX = mirrorFraction(p.anchor.fx, mirror)
            val dx = if (mirror) -p.dx else p.dx
            cx = anchorX * widthPx + dx * offsetUnit + edgeInset(anchorX, insets.left, insets.right)
            cy = p.anchor.fy * heightPx + p.dy * offsetUnit + edgeInset(p.anchor.fy, insets.top, insets.bottom)
        }
        cx = clampCenter(cx, element.halfWidth * unit, widthPx)
        cy = clampCenter(cy, element.halfHeight * unit, heightPx)
        placed += PlacedElement(element, cx, cy, unit, mirror)
    }
    return placed
}

/** Horizontal fraction as seen with swap-hands on or off (the mapping is its own inverse). */
fun mirrorFraction(fx: Float, mirror: Boolean): Float = if (mirror) 1f - fx else fx

fun isHidden(element: TouchElement, override: ElementOverride?, landscape: Boolean): Boolean =
    override?.hidden ?: (element.hiddenByDefault || (!landscape && element.hiddenInPortrait))

private fun edgeInset(anchorFraction: Float, start: Float, end: Float): Float = when (anchorFraction) {
    0f -> start
    1f -> -end
    else -> 0f
}

private fun clampCenter(center: Float, half: Float, extent: Float): Float =
    if (half * 2f >= extent) extent / 2f else center.coerceIn(half, extent - half)
