package com.phobos.emulator.ui.hud

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.pillShape

/** How far outside the box its outline and handles sit, clear of the text along its edges. */
private val FRAME_GAP = 3.dp

/**
 * The HUD's edit mode over the paused game. [box], where the HUD is now (in this layer's
 * coordinates, within [area], the screen above any keyboard), gets an outline and a handle on
 * each corner and side. Dragging the box moves it, dragging a handle resizes it, pinching scales
 * it, and a tap outside it is [onDone]. Each gesture starts with [onGestureStart], then reports
 * the finger's travel since it went down, or the pinch's spread as a factor of where it began.
 * No touch reaches the layers under it; handles take touches 24 dp around them. While [holdDown],
 * the finger whose hold opened the mode is still down, dragging the box, and touches that start
 * meanwhile are ignored.
 */
@Composable
internal fun HudEditLayer(
    box: Rect,
    area: Size,
    canResetSize: Boolean,
    holdDown: Boolean,
    onGestureStart: () -> Unit,
    onMove: (Offset) -> Unit,
    onResize: (HudHandle, Offset) -> Unit,
    onPinch: (Float) -> Unit,
    onResetSize: () -> Unit,
    onCancel: () -> Unit,
    onDone: () -> Unit,
) {
    val latestBox by rememberUpdatedState(box)
    val latestHoldDown by rememberUpdatedState(holdDown)
    val latestOnGestureStart by rememberUpdatedState(onGestureStart)
    val latestOnMove by rememberUpdatedState(onMove)
    val latestOnResize by rememberUpdatedState(onResize)
    val latestOnPinch by rememberUpdatedState(onPinch)
    val latestOnDone by rememberUpdatedState(onDone)
    val accent = MaterialTheme.colorScheme.primary

    Box(Modifier.fillMaxSize()) {
        Canvas(
            Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    val reach = 24.dp.toPx()
                    val gap = FRAME_GAP.toPx()
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = false)
                        down.consume()
                        val ignored = latestHoldDown
                        val target = hudEditTarget(latestBox.inflate(gap), down.position, reach)
                        var dragging = false
                        var pinching = false
                        var pinchFrom = 0f
                        while (true) {
                            val event = awaitPointerEvent()
                            event.changes.forEach { it.consume() }
                            val pressed = event.changes.filter { it.pressed }
                            if (pressed.isEmpty()) break
                            if (ignored) continue
                            if (pressed.size >= 2) {
                                val spread = (pressed[0].position - pressed[1].position).getDistance()
                                if (!pinching) {
                                    pinching = true
                                    pinchFrom = spread
                                    latestOnGestureStart()
                                } else if (pinchFrom > 0f) {
                                    latestOnPinch(spread / pinchFrom)
                                }
                            } else if (!pinching && target != HudEditTarget.Outside) {
                                val travel = pressed[0].position - down.position
                                if (!dragging && travel.getDistance() > viewConfiguration.touchSlop) {
                                    dragging = true
                                    latestOnGestureStart()
                                }
                                if (dragging) {
                                    when (target) {
                                        is HudEditTarget.Handle -> latestOnResize(target.handle, travel)
                                        HudEditTarget.Box -> latestOnMove(travel)
                                        HudEditTarget.Outside -> Unit
                                    }
                                }
                            }
                        }
                        if (!ignored && !dragging && !pinching && target == HudEditTarget.Outside) latestOnDone()
                    }
                }
        ) {
            val stroke = 2.dp.toPx()
            val frame = box.inflate(FRAME_GAP.toPx())
            drawRect(accent, frame.topLeft, frame.size, style = Stroke(stroke))
            val half = 6.dp.toPx()
            for (handle in HudHandle.entries) {
                val at = handle.at(frame)
                val rim = half + stroke
                drawRect(Color.Black.copy(alpha = 0.6f), at - Offset(rim, rim), Size(rim * 2, rim * 2))
                drawRect(accent, at - Offset(half, half), Size(half * 2, half * 2))
            }
        }

        // The buttons sit in the half of the screen the box isn't in.
        val atTop = box.center.y > area.height / 2f
        val areaHeight = with(LocalDensity.current) { area.height.toDp() }
        Box(
            Modifier.fillMaxWidth().height(areaHeight),
            contentAlignment = if (atTop) Alignment.TopCenter else Alignment.BottomCenter,
        ) {
            // A Surface keeps the touches on it from the layers behind, so a tap on the bar isn't a tap outside the box.
            Surface(
                shape = MaterialTheme.shapes.large,
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                contentColor = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(16.dp),
            ) {
                Column(
                    Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(
                        "Drag it to move it, pinch to resize it, or drag a handle to change its shape. Tap outside it when you're done.",
                        style = MaterialTheme.typography.bodySmall,
                        textAlign = TextAlign.Center,
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = onResetSize, enabled = canResetSize, shape = pillShape()) { Text("Reset size") }
                        OutlinedButton(onClick = onCancel, shape = pillShape()) { Text("Cancel") }
                        Button(onClick = onDone, shape = pillShape()) { Text("Done") }
                    }
                }
            }
        }
    }
}
