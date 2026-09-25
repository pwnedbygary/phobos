package com.phobos.emulator.ui.touch

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.GridOff
import androidx.compose.material.icons.filled.GridOn
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.changedToDownIgnoreConsumed
import androidx.compose.ui.input.pointer.changedToUpIgnoreConsumed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * Full-screen layout editor for one controller family. Drag a control to move it, pinch to
 * resize it; hidden controls are drawn as faded ghosts and can be selected and shown again.
 *
 * Landscape and portrait layouts are separate customizations; rotating the device switches to
 * the other orientation's layout (unsaved changes to the previous one are discarded).
 *
 * @param showGamePlaceholder draw a stand-in game picture (editing from Settings, no game running).
 */
@Composable
fun TouchLayoutEditor(
    layout: TouchLayout,
    prefs: TouchPrefs,
    overridesFor: (landscape: Boolean) -> Map<String, ElementOverride>,
    onSave: (landscape: Boolean, overrides: Map<String, ElementOverride>) -> Unit,
    onCancel: () -> Unit,
    modifier: Modifier = Modifier,
    showGamePlaceholder: Boolean = false,
) {
    val density = LocalDensity.current
    val layoutDirection = LocalLayoutDirection.current
    val textMeasurer = rememberTextMeasurer()
    val painter = remember(textMeasurer) { TouchPainter(textMeasurer) }
    val opacity = maxOf(prefs.opacity, 0.75f)
    val cutout = WindowInsets.displayCutout

    BoxWithConstraints(modifier.fillMaxSize()) {
        val widthPx = constraints.maxWidth.toFloat()
        val heightPx = constraints.maxHeight.toFloat()
        val landscape = widthPx >= heightPx
        val currentOverridesFor by rememberUpdatedState(overridesFor)

        var overrides by remember(landscape, layout.family) { mutableStateOf(currentOverridesFor(landscape)) }
        var selectedId by remember(landscape, layout.family) { mutableStateOf<String?>(null) }
        var snap by remember { mutableStateOf(true) }

        // Same insets as the in-game overlay so default positions match what the player sees.
        val insets = ScreenInsets(
            left = cutout.getLeft(density, layoutDirection).toFloat(),
            top = cutout.getTop(density).toFloat(),
            right = cutout.getRight(density, layoutDirection).toFloat(),
            bottom = cutout.getBottom(density).toFloat(),
        )
        val mirror = prefs.swapHands
        val placed = placeLayout(
            layout, overrides, widthPx, heightPx, density.density, prefs.scale, insets,
            includeHidden = true, mirror = mirror,
        )
        val placedState by rememberUpdatedState(placed)
        val gridPx = GRID_DP * density.density

        fun hiddenState(el: PlacedElement) = isHidden(el.element, overrides[el.element.id], landscape)

        fun update(id: String, transform: (ElementOverride) -> ElementOverride) {
            overrides = overrides + (id to transform(overrides[id] ?: ElementOverride()))
        }

        Canvas(
            Modifier
                .fillMaxSize()
                .pointerInput(landscape, layout.family, mirror) {
                    awaitPointerEventScope {
                        var dragId: String? = null
                        var dragPointer = -1L
                        var dragStart = Offset.Zero
                        var dragging = false
                        var grabDx = 0f
                        var grabDy = 0f
                        var pinchStartDistance = 0f
                        var pinchStartScale = 1f
                        val down = LinkedHashMap<Long, Offset>()

                        while (true) {
                            val event = awaitPointerEvent()
                            for (change in event.changes) {
                                val id = change.id.value
                                when {
                                    change.changedToDownIgnoreConsumed() -> {
                                        down[id] = change.position
                                        if (down.size == 1) {
                                            val slop = 8f * density.density
                                            val hit = placedState.asReversed().firstOrNull { el ->
                                                abs(change.position.x - el.cx) <= el.halfW + slop &&
                                                    abs(change.position.y - el.cy) <= el.halfH + slop
                                            }
                                            selectedId = hit?.element?.id
                                            dragId = hit?.element?.id
                                            dragPointer = id
                                            dragStart = change.position
                                            dragging = false
                                            if (hit != null) {
                                                grabDx = change.position.x - hit.cx
                                                grabDy = change.position.y - hit.cy
                                            }
                                        } else if (down.size == 2 && dragId != null) {
                                            val (a, b) = down.values.toList()
                                            pinchStartDistance = hypot(a.x - b.x, a.y - b.y)
                                            pinchStartScale = overrides[dragId!!]?.scale ?: 1f
                                        }
                                    }
                                    change.changedToUpIgnoreConsumed() -> {
                                        down.remove(id)
                                        if (id == dragPointer || down.isEmpty()) {
                                            dragId = null
                                            dragPointer = -1L
                                        } else if (down.size == 1) {
                                            // Pinch ended: keep dragging from where the remaining finger is now.
                                            val p = down[dragPointer]
                                            val el = placedState.firstOrNull { it.element.id == dragId }
                                            if (p != null && el != null) {
                                                grabDx = p.x - el.cx
                                                grabDy = p.y - el.cy
                                                dragStart = p
                                                dragging = false
                                            }
                                        }
                                    }
                                    change.pressed -> down[id] = change.position
                                }
                                change.consume()
                            }

                            val target = dragId ?: continue
                            val element = placedState.firstOrNull { it.element.id == target } ?: continue
                            val w = size.width.toFloat()
                            val h = size.height.toFloat()
                            if (down.size >= 2 && pinchStartDistance > 0f) {
                                val (a, b) = down.values.toList()
                                val scale = (pinchStartScale * hypot(a.x - b.x, a.y - b.y) / pinchStartDistance)
                                    .coerceIn(TouchLayoutCodec.MIN_ELEMENT_SCALE, TouchLayoutCodec.MAX_ELEMENT_SCALE)
                                update(target) { o ->
                                    o.copy(
                                        fx = o.fx ?: mirrorFraction(element.cx / w, mirror),
                                        fy = o.fy ?: (element.cy / h),
                                        scale = scale,
                                    )
                                }
                            } else {
                                val p = down[dragPointer] ?: continue
                                // A tap only selects; moving it would pin the element in place.
                                if (!dragging) {
                                    if ((p - dragStart).getDistance() < viewConfiguration.touchSlop) continue
                                    dragging = true
                                }
                                var cx = p.x - grabDx
                                var cy = p.y - grabDy
                                if (snap) {
                                    cx = (cx / gridPx).roundToInt() * gridPx
                                    cy = (cy / gridPx).roundToInt() * gridPx
                                }
                                cx = cx.coerceIn(element.halfW, (w - element.halfW).coerceAtLeast(element.halfW))
                                cy = cy.coerceIn(element.halfH, (h - element.halfH).coerceAtLeast(element.halfH))
                                // Stored unmirrored so toggling swap-hands keeps the customization.
                                update(target) { o -> o.copy(fx = mirrorFraction(cx / w, mirror), fy = cy / h) }
                            }
                        }
                    }
                }
        ) {
            if (showGamePlaceholder) {
                drawRect(Color(0xFF07090D))
                // Game picture stand-in: centered in landscape, at the top in portrait.
                val (gameW, gameH) = if (landscape) (heightPx * 4f / 3f) to heightPx else widthPx to (widthPx * 3f / 4f)
                drawRect(Color(0xFF1A2130), Offset((widthPx - gameW) / 2f, 0f), Size(gameW, gameH))
            }
            if (snap) {
                val gridColor = Color.White.copy(alpha = 0.05f)
                var x = gridPx * 4
                while (x < size.width) { drawLine(gridColor, Offset(x, 0f), Offset(x, size.height)); x += gridPx * 4 }
                var y = gridPx * 4
                while (y < size.height) { drawLine(gridColor, Offset(0f, y), Offset(size.width, y)); y += gridPx * 4 }
            }
            for (el in placed) {
                val hidden = hiddenState(el)
                with(painter) { drawElement(el, null, if (hidden) opacity * 0.25f else opacity) }
                val selected = el.element.id == selectedId
                val outline = when {
                    selected -> Color(TouchPalette.GOLD)
                    hidden -> Color.White.copy(alpha = 0.35f)
                    else -> Color.White.copy(alpha = 0.18f)
                }
                drawRoundRect(
                    color = outline,
                    topLeft = Offset(el.cx - el.halfW, el.cy - el.halfH),
                    size = Size(el.halfW * 2f, el.halfH * 2f),
                    cornerRadius = CornerRadius(8.dp.toPx()),
                    style = Stroke(
                        width = (if (selected) 2.dp else 1.dp).toPx(),
                        pathEffect = if (selected) null else PathEffect.dashPathEffect(floatArrayOf(10f, 8f)),
                    ),
                )
            }
        }

        val selected = placed.firstOrNull { it.element.id == selectedId }
        EditorToolbar(
            modifier = Modifier
                .align(if (landscape) Alignment.Center else Alignment.TopCenter)
                .padding(top = if (landscape) 0.dp else 48.dp),
            title = "${layout.family.displayName} · ${if (landscape) "landscape" else "portrait"}",
            selectedTitle = selected?.element?.title,
            selectedHidden = selected?.let { hiddenState(it) } ?: false,
            snap = snap,
            onSmaller = { selected?.let { el -> update(el.element.id) { it.copy(scale = (it.scale - 0.1f).coerceAtLeast(TouchLayoutCodec.MIN_ELEMENT_SCALE)) } } },
            onLarger = { selected?.let { el -> update(el.element.id) { it.copy(scale = (it.scale + 0.1f).coerceAtMost(TouchLayoutCodec.MAX_ELEMENT_SCALE)) } } },
            onToggleHidden = { selected?.let { el -> update(el.element.id) { it.copy(hidden = !hiddenState(el)) } } },
            onResetSelected = { selected?.let { el -> overrides = overrides - el.element.id } },
            onToggleSnap = { snap = !snap },
            onResetAll = { overrides = emptyMap(); selectedId = null },
            onCancel = onCancel,
            onSave = { onSave(landscape, overrides.filterValues { it != ElementOverride() }) },
        )
    }
}

@Composable
private fun EditorToolbar(
    modifier: Modifier,
    title: String,
    selectedTitle: String?,
    selectedHidden: Boolean,
    snap: Boolean,
    onSmaller: () -> Unit,
    onLarger: () -> Unit,
    onToggleHidden: () -> Unit,
    onResetSelected: () -> Unit,
    onToggleSnap: () -> Unit,
    onResetAll: () -> Unit,
    onCancel: () -> Unit,
    onSave: () -> Unit,
) {
    Surface(
        modifier = modifier.widthIn(max = 520.dp),
        shape = RoundedCornerShape(18.dp),
        color = Color(0xE6101319),
        contentColor = Color.White,
        tonalElevation = 6.dp,
        shadowElevation = 8.dp,
    ) {
        Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(
                selectedTitle ?: "Drag to move · pinch to resize · select a faded control to show it",
                style = MaterialTheme.typography.bodySmall,
                color = if (selectedTitle != null) Color(TouchPalette.GOLD) else Color.White.copy(alpha = 0.7f),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(2.dp), verticalAlignment = Alignment.CenterVertically) {
                val enabled = selectedTitle != null
                IconButton(onClick = onSmaller, enabled = enabled) { Icon(Icons.Default.Remove, "Smaller") }
                IconButton(onClick = onLarger, enabled = enabled) { Icon(Icons.Default.Add, "Larger") }
                IconButton(onClick = onToggleHidden, enabled = enabled) {
                    Icon(if (selectedHidden) Icons.Default.Visibility else Icons.Default.VisibilityOff, if (selectedHidden) "Show" else "Hide")
                }
                IconButton(onClick = onResetSelected, enabled = enabled) { Icon(Icons.Default.Refresh, "Reset control") }
                Spacer(Modifier.width(8.dp))
                IconButton(onClick = onToggleSnap) { Icon(if (snap) Icons.Default.GridOn else Icons.Default.GridOff, "Snap to grid") }
                IconButton(onClick = onResetAll) { Icon(Icons.Default.RestartAlt, "Reset layout") }
                IconButton(onClick = onCancel) { Icon(Icons.Default.Close, "Cancel") }
                IconButton(onClick = onSave) { Icon(Icons.Default.Check, "Save", tint = Color(TouchPalette.GOLD)) }
            }
        }
    }
}

private const val GRID_DP = 8f
