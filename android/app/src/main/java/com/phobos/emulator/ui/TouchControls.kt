package com.phobos.emulator.ui

import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.*
import androidx.compose.ui.input.pointer.*
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.*
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.GameInputState
import kotlin.math.*

// ─── mapKeyCodeToBit ─────────────────────────────────────────────────────────

fun mapKeyCodeToBit(code: Int): Int = when (code) {
    KeyEvent.KEYCODE_BUTTON_A      -> PhobosCore.Input.A
    KeyEvent.KEYCODE_BUTTON_B      -> PhobosCore.Input.B
    KeyEvent.KEYCODE_BUTTON_X      -> PhobosCore.Input.X
    KeyEvent.KEYCODE_BUTTON_Y      -> PhobosCore.Input.Y
    KeyEvent.KEYCODE_BUTTON_L1     -> PhobosCore.Input.L1
    KeyEvent.KEYCODE_BUTTON_R1     -> PhobosCore.Input.R1
    KeyEvent.KEYCODE_BUTTON_L2     -> PhobosCore.Input.L2
    KeyEvent.KEYCODE_BUTTON_R2     -> PhobosCore.Input.R2
    KeyEvent.KEYCODE_BUTTON_SELECT -> PhobosCore.Input.SELECT
    KeyEvent.KEYCODE_BUTTON_START  -> PhobosCore.Input.START
    KeyEvent.KEYCODE_DPAD_UP       -> PhobosCore.Input.UP
    KeyEvent.KEYCODE_DPAD_DOWN     -> PhobosCore.Input.DOWN
    KeyEvent.KEYCODE_DPAD_LEFT     -> PhobosCore.Input.LEFT
    KeyEvent.KEYCODE_DPAD_RIGHT    -> PhobosCore.Input.RIGHT
    else -> 0
}

// ─── TouchControls ───────────────────────────────────────────────────────────

@Composable
fun TouchControls(
    modifier: Modifier = Modifier,
    systemName: String = "",
    onInputChanged: (Int) -> Unit = {},
    opacity: Float = 0.85f,
    sizeScale: Float = 1.0f
) {
    var pressedButtons by remember { mutableIntStateOf(0) }

    fun updateInput(button: Int, pressed: Boolean) {
        val newButtons = if (pressed) pressedButtons or button else pressedButtons and button.inv()
        if (newButtons != pressedButtons) {
            pressedButtons = newButtons
            onInputChanged(newButtons)
        }
    }

    Box(modifier = modifier.fillMaxSize()) {
        ShoulderBar(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 80.dp, start = 16.dp, end = 16.dp)
                .fillMaxWidth(),
            opacity = opacity,
            sizeScale = sizeScale,
            onPress = { bit, pressed -> updateInput(bit, pressed) }
        )

        AnalogStick(
            isLeft = true,
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = 24.dp, bottom = 220.dp),
            sizeScale = sizeScale,
            opacity = opacity
        )

        AnalogStick(
            isLeft = false,
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 24.dp, bottom = 220.dp),
            sizeScale = sizeScale,
            opacity = opacity
        )

        DpadCross(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = 24.dp, bottom = 24.dp),
            sizeScale = sizeScale,
            opacity = opacity,
            onPress = { bit, pressed -> updateInput(bit, pressed) }
        )

        FaceCluster(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 24.dp, bottom = 24.dp),
            systemName = systemName,
            sizeScale = sizeScale,
            opacity = opacity,
            onPress = { bit, pressed -> updateInput(bit, pressed) }
        )

        SystemBar(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 24.dp),
            sizeScale = sizeScale,
            opacity = opacity,
            onPress = { bit, pressed -> updateInput(bit, pressed) }
        )
    }
}

// ─── ButtonChipVisual ────────────────────────────────────────────────────────

private fun DrawScope.drawButtonChip(
    bounds: Rect,
    isPressed: Boolean,
    tint: Color?,
    isWide: Boolean,
    opacity: Float,
    textMeasurer: TextMeasurer,
    label: String,
    fontSizeSp: Float
) {
    val fillColor = if (isPressed) {
        (tint ?: Color(0xFF3A6FC8)).copy(alpha = opacity)
    } else {
        (tint ?: Color(0xCC1A1A1A)).copy(alpha = opacity * 0.7f)
    }
    val borderColor = Color(0xFFCCCCCC).copy(alpha = opacity)

    if (isWide) {
        val cornerRadius = bounds.height / 2f
        drawRoundRect(
            color = fillColor,
            topLeft = bounds.topLeft,
            size = bounds.size,
            cornerRadius = CornerRadius(cornerRadius, cornerRadius)
        )
        drawRoundRect(
            color = borderColor,
            topLeft = bounds.topLeft,
            size = bounds.size,
            cornerRadius = CornerRadius(cornerRadius, cornerRadius),
            style = Stroke(width = 3f)
        )
    } else {
        val center = bounds.center
        val radius = minOf(bounds.width, bounds.height) / 2f
        drawCircle(color = fillColor, radius = radius, center = center)
        drawCircle(color = borderColor, radius = radius, center = center, style = Stroke(width = 3f))
    }

    if (label.isNotEmpty()) {
        val textLayoutResult = textMeasurer.measure(
            text = AnnotatedString(label),
            style = TextStyle(
                color = Color.White.copy(alpha = opacity),
                fontSize = fontSizeSp.sp,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center
            )
        )
        drawText(
            textLayoutResult = textLayoutResult,
            topLeft = Offset(
                bounds.center.x - textLayoutResult.size.width / 2f,
                bounds.center.y - textLayoutResult.size.height / 2f
            )
        )
    }
}

// ─── ClusterDispatcher ───────────────────────────────────────────────────────

/**
 * Multi-touch cluster handler. Owns per-pointer-ID button tracking so that
 * multiple fingers can independently press different buttons within the zone.
 */
@Composable
private fun ClusterDispatcher(
    modifier: Modifier,
    buttons: List<ClusterButton>,
    opacity: Float,
    onPress: (Int, Boolean) -> Unit
) {
    data class PointerOwner(val pointerId: Long, val buttonIndex: Int)

    val pointerOwners = remember { mutableStateListOf<PointerOwner>() }
    val view = LocalView.current

    Box(
        modifier = modifier.pointerInput(buttons) {
            awaitPointerEventScope {
                while (true) {
                    val event = awaitPointerEvent()
                    event.changes.forEach { change ->
                        val pid = change.id.value
                        val pos = change.position

                        when {
                            change.changedToDown() -> {
                                val hitIndex = buttons.indexOfFirst { it.bounds.contains(pos) }
                                if (hitIndex >= 0 && pointerOwners.none { it.buttonIndex == hitIndex }) {
                                    val btn = buttons[hitIndex]
                                    pointerOwners.add(PointerOwner(pid, hitIndex))
                                    onPress(btn.bit, true)
                                    view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                                }
                            }
                            change.changedToUp() -> {
                                val owner = pointerOwners.find { it.pointerId == pid }
                                if (owner != null) {
                                    pointerOwners.remove(owner)
                                    onPress(buttons[owner.buttonIndex].bit, false)
                                }
                            }
                        }
                        change.consume()
                    }
                }
            }
        }
    ) {
        val textMeasurer = rememberTextMeasurer()
        Canvas(Modifier.fillMaxSize()) {
            buttons.forEach { btn ->
                val pressed = pointerOwners.any { it.buttonIndex == buttons.indexOf(btn) }
                drawButtonChip(
                    bounds = btn.bounds,
                    isPressed = pressed,
                    tint = btn.tint,
                    isWide = btn.isWide,
                    opacity = opacity,
                    textMeasurer = textMeasurer,
                    label = btn.label,
                    fontSizeSp = btn.fontSizeSp
                )
            }
        }
    }
}

private data class ClusterButton(
    val bit: Int,
    val label: String,
    val bounds: Rect,
    val tint: Color? = null,
    val isWide: Boolean = false,
    val fontSizeSp: Float = 12f
)

// ─── DpadCross ───────────────────────────────────────────────────────────────

@Composable
@Suppress("unused")
fun DpadCross(
    modifier: Modifier = Modifier,
    sizeScale: Float = 1.0f,
    opacity: Float = 0.85f,
    onPress: (Int, Boolean) -> Unit
) {
    val baseSizeDp = 120.dp
    val density = LocalDensity.current
    val sizePx = with(density) { (baseSizeDp * sizeScale).toPx() }

    var dx by remember { mutableFloatStateOf(0f) }
    var dy by remember { mutableFloatStateOf(0f) }
    var isPressed by remember { mutableStateOf(false) }
    var activeDir by remember { mutableIntStateOf(0) }
    val activePointerId = remember { mutableLongStateOf(-1L) }
    val view = LocalView.current

    // Notify changes when direction switches
    fun setDir(newDir: Int) {
        if (newDir != activeDir) {
            if (activeDir != 0) onPress(activeDir, false)
            activeDir = newDir
            if (newDir != 0) onPress(newDir, true)
        }
    }

    Box(
        modifier = modifier
            .size(baseSizeDp * sizeScale)
            .alpha(opacity)
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        event.changes.forEach { change ->
                            when {
                                change.changedToDown() && activePointerId.longValue == -1L -> {
                                    activePointerId.longValue = change.id.value
                                    isPressed = true
                                    view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                                    dx = change.position.x - sizePx / 2f
                                    dy = change.position.y - sizePx / 2f
                                }
                                change.changedToUp() && change.id.value == activePointerId.longValue -> {
                                    activePointerId.longValue = -1L
                                    isPressed = false
                                    dx = 0f; dy = 0f
                                    setDir(0)
                                }
                                change.id.value == activePointerId.longValue -> {
                                    dx = change.position.x - sizePx / 2f
                                    dy = change.position.y - sizePx / 2f
                                }
                            }
                            change.consume()
                        }

                        if (isPressed) {
                            val threshold = sizePx * 0.18f
                            val dir = when {
                                dy < -threshold && abs(dy) >= abs(dx) -> PhobosCore.Input.UP
                                dy > threshold  && abs(dy) >= abs(dx) -> PhobosCore.Input.DOWN
                                dx < -threshold && abs(dx) >  abs(dy) -> PhobosCore.Input.LEFT
                                dx > threshold  && abs(dx) >  abs(dy) -> PhobosCore.Input.RIGHT
                                else -> 0
                            }
                            setDir(dir)
                        }
                    }
                }
            }
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val s = sizePx
            val arm = s * 0.30f
            val armLen = arm * 1.4f
            val c = s / 2f
            val cr = arm * 0.20f

            val face = Color(0xFFE6E6E6)
            val border = Color(0xFF1A1A1A)
            val press = Color(0xFF7AA8FF)

            // Solid cross: horizontal + vertical rounded bars
            val hRect = Rect(c - armLen, c - arm / 2f, c + armLen, c + arm / 2f)
            val vRect = Rect(c - arm / 2f, c - armLen, c + arm / 2f, c + armLen)
            drawRoundRect(border, hRect.topLeft, hRect.size, CornerRadius(cr))
            drawRoundRect(border, vRect.topLeft, vRect.size, CornerRadius(cr))
            val inset = 3f
            drawRoundRect(face, Offset(hRect.left + inset, hRect.top + inset),
                Size(hRect.width - inset * 2, hRect.height - inset * 2), CornerRadius(cr))
            drawRoundRect(face, Offset(vRect.left + inset, vRect.top + inset),
                Size(vRect.width - inset * 2, vRect.height - inset * 2), CornerRadius(cr))

            // Press highlights (pill-shaped overlay on the active arm)
            val pCr = CornerRadius(arm * 0.15f, arm * 0.15f)
            val m = arm * 0.14f
            val pl = armLen - m * 2f
            val ps = arm - m * 2f
            if (activeDir == PhobosCore.Input.UP)
                drawRoundRect(press, Offset(c - arm/2f + m, c - armLen + m), Size(ps, pl), pCr)
            if (activeDir == PhobosCore.Input.DOWN)
                drawRoundRect(press, Offset(c - arm/2f + m, c + m), Size(ps, pl), pCr)
            if (activeDir == PhobosCore.Input.LEFT)
                drawRoundRect(press, Offset(c - armLen + m, c - arm/2f + m), Size(pl, ps), pCr)
            if (activeDir == PhobosCore.Input.RIGHT)
                drawRoundRect(press, Offset(c + m, c - arm/2f + m), Size(pl, ps), pCr)

            // Center dot
            drawCircle(border, arm * 0.18f, Offset(c, c))
        }
    }
}

// ─── AnalogStick ─────────────────────────────────────────────────────────────

@Composable
fun AnalogStick(
    isLeft: Boolean,
    modifier: Modifier = Modifier,
    sizeScale: Float = 1.0f,
    opacity: Float = 0.85f
) {
    val baseSizeDp = 130.dp
    val density = LocalDensity.current
    val sizePx = with(density) { (baseSizeDp * sizeScale).toPx() }

    var thumbX by remember { mutableFloatStateOf(0f) }
    var thumbY by remember { mutableFloatStateOf(0f) }
    var isActive by remember { mutableStateOf(false) }
    val activePointerId = remember { mutableLongStateOf(-1L) }

    Box(
        modifier = modifier
            .size(baseSizeDp * sizeScale)
            .alpha(opacity)
            .drawBehind {
                val center = Offset(size.width / 2f, size.height / 2f)
                val baseRadius = size.minDimension / 2f

                drawCircle(Color(0x66000000), baseRadius, center)
                drawCircle(Color(0xFFCCCCCC), baseRadius, center, style = Stroke(4f))

                val thumbRadius = baseRadius * 0.18f
                val maxOffset = baseRadius - thumbRadius - 4f
                val tx = (thumbX * maxOffset).coerceIn(-maxOffset, maxOffset)
                val ty = (thumbY * maxOffset).coerceIn(-maxOffset, maxOffset)
                drawCircle(Color(0xFFE6E6E6), thumbRadius, Offset(center.x + tx, center.y + ty))
            }
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        event.changes.forEach { change ->
                            when {
                                change.changedToDown() && activePointerId.longValue == -1L -> {
                                    activePointerId.longValue = change.id.value
                                    isActive = true
                                }
                                change.changedToUp() && change.id.value == activePointerId.longValue -> {
                                    activePointerId.longValue = -1L
                                    isActive = false
                                    thumbX = 0f; thumbY = 0f
                                }
                                change.id.value == activePointerId.longValue -> {
                                    val cx = sizePx / 2f
                                    val cy = sizePx / 2f
                                    val maxDist = sizePx / 2f * 0.82f
                                    val rawX = change.position.x - cx
                                    val rawY = change.position.y - cy
                                    val dist = sqrt(rawX * rawX + rawY * rawY)
                                    val scale = if (dist > maxDist) maxDist / dist else 1f
                                    thumbX = (rawX * scale) / maxDist
                                    thumbY = (rawY * scale) / maxDist
                                }
                            }
                            change.consume()
                        }

                        if (isActive) {
                            if (isLeft) {
                                GameInputState.lx = thumbX
                                GameInputState.ly = thumbY
                            } else {
                                GameInputState.rx = thumbX
                                GameInputState.ry = thumbY
                            }
                            GameInputState.push()
                        } else {
                            if (isLeft) {
                                GameInputState.lx = 0f
                                GameInputState.ly = 0f
                            } else {
                                GameInputState.rx = 0f
                                GameInputState.ry = 0f
                            }
                            GameInputState.push()
                        }
                    }
                }
            }
    )
}

// ─── FaceCluster ─────────────────────────────────────────────────────────────

@Composable
fun FaceCluster(
    modifier: Modifier = Modifier,
    systemName: String = "",
    sizeScale: Float = 1.0f,
    opacity: Float = 0.85f,
    onPress: (Int, Boolean) -> Unit
) {
    val isPs1 = systemName.contains("PlayStation", ignoreCase = true)
    val baseSizeDp = 160.dp
    val density = LocalDensity.current
    val sizePx = with(density) { (baseSizeDp * sizeScale).toPx() }

    // Diamond: X(top) B(bottom) Y(left) A(right)
    val btnRadiusDp = 26.dp
    val btnDiameterPx = with(density) { (btnRadiusDp * sizeScale * 2f).toPx() }
    val centerPx = sizePx / 2f
    val offsetPx = sizePx * 0.28f

    fun rect(cx: Float, cy: Float) = Rect(
        Offset(cx - btnDiameterPx / 2f, cy - btnDiameterPx / 2f),
        Size(btnDiameterPx, btnDiameterPx)
    )

    val buttons = remember(isPs1, sizePx) {
        listOf(
            ClusterButton(
                bit = PhobosCore.Input.X,
                label = if (isPs1) "△" else "X",
                bounds = rect(centerPx, centerPx - offsetPx),
                tint = if (isPs1) Color(0xFF4CAF50) else Color(0xFF2196F3),
                fontSizeSp = 16f
            ),
            ClusterButton(
                bit = PhobosCore.Input.B,
                label = if (isPs1) "✕" else "B",
                bounds = rect(centerPx, centerPx + offsetPx),
                tint = if (isPs1) Color(0xFF2196F3) else Color(0xFFF44336),
                fontSizeSp = 16f
            ),
            ClusterButton(
                bit = PhobosCore.Input.Y,
                label = if (isPs1) "□" else "Y",
                bounds = rect(centerPx - offsetPx, centerPx),
                tint = if (isPs1) Color(0xFFE91E63) else Color(0xFFFFC107),
                fontSizeSp = 16f
            ),
            ClusterButton(
                bit = PhobosCore.Input.A,
                label = if (isPs1) "○" else "A",
                bounds = rect(centerPx + offsetPx, centerPx),
                tint = if (isPs1) Color(0xFFF44336) else Color(0xFF4CAF50),
                fontSizeSp = 16f
            )
        )
    }

    ClusterDispatcher(
        modifier = modifier.size(baseSizeDp * sizeScale),
        buttons = buttons,
        opacity = opacity,
        onPress = onPress
    )
}

// ─── ShoulderBar ─────────────────────────────────────────────────────────────

@Composable
fun ShoulderBar(
    modifier: Modifier = Modifier,
    sizeScale: Float = 1.0f,
    opacity: Float = 0.85f,
    onPress: (Int, Boolean) -> Unit
) {
    val density = LocalDensity.current
    val wPx = 80.dp
    val hPx = 36.dp
    val w = with(density) { (wPx * sizeScale).toPx() }
    val h = with(density) { (hPx * sizeScale).toPx() }
    val triggerWPx = 36.dp
    val triggerHPx = 60.dp
    val tw = with(density) { (triggerWPx * sizeScale).toPx() }
    val th = with(density) { (triggerHPx * sizeScale).toPx() }

    val view = LocalView.current
    val pointerOwners = remember { mutableStateListOf<Pair<Long, Int>>() }

    // Fixed-height box; L1/R1 top row, L2/R2 below, positioned absolutely.
    val totalHeightPx = h + 4f * density.density + th
    val totalH = with(density) { totalHeightPx.toDp() }

    Box(modifier = modifier.fillMaxWidth().height(totalH)) {
        // Build bounds using the measured size
        val textMeasurer = rememberTextMeasurer()
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(w, h, tw, th) {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            event.changes.forEach { change ->
                                val pid = change.id.value
                                val pos = change.position
                                val availW = size.width.toFloat()
                                val l1Rect = Rect(Offset(16f * density.density, 0f), Size(w, h))
                                val r1Rect = Rect(Offset(availW - w - 16f * density.density, 0f), Size(w, h))
                                val l2Rect = Rect(Offset(16f * density.density, h + 4f * density.density), Size(tw, th))
                                val r2Rect = Rect(Offset(availW - tw - 16f * density.density, h + 4f * density.density), Size(tw, th))
                                val btns = listOf(
                                    PhobosCore.Input.L1 to l1Rect,
                                    PhobosCore.Input.R1 to r1Rect,
                                    PhobosCore.Input.L2 to l2Rect,
                                    PhobosCore.Input.R2 to r2Rect,
                                )
                                when {
                                    change.changedToDown() -> {
                                        val idx = btns.indexOfFirst { it.second.contains(pos) }
                                        if (idx >= 0 && pointerOwners.none { it.second == idx }) {
                                            pointerOwners.add(pid to idx)
                                            onPress(btns[idx].first, true)
                                            view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                                        }
                                    }
                                    change.changedToUp() -> {
                                        val owner = pointerOwners.find { it.first == pid }
                                        if (owner != null) {
                                            pointerOwners.remove(owner)
                                            onPress(btns[owner.second].first, false)
                                        }
                                    }
                                }
                                change.consume()
                            }
                        }
                    }
                }
        ) {
            val availW = size.width
            val l1Rect = Rect(Offset(16f * density.density, 0f), Size(w, h))
            val r1Rect = Rect(Offset(availW - w - 16f * density.density, 0f), Size(w, h))
            val l2Rect = Rect(Offset(16f * density.density, h + 4f * density.density), Size(tw, th))
            val r2Rect = Rect(Offset(availW - tw - 16f * density.density, h + 4f * density.density), Size(tw, th))

            val btns = listOf(
                ClusterButton(PhobosCore.Input.L1, "L1", l1Rect, isWide = true, fontSizeSp = 11f),
                ClusterButton(PhobosCore.Input.R1, "R1", r1Rect, isWide = true, fontSizeSp = 11f),
                ClusterButton(PhobosCore.Input.L2, "L2", l2Rect, isWide = true, fontSizeSp = 11f),
                ClusterButton(PhobosCore.Input.R2, "R2", r2Rect, isWide = true, fontSizeSp = 11f),
            )
            btns.forEach { btn ->
                val idx = btns.indexOf(btn)
                val pressed = pointerOwners.any { it.second == idx }
                drawButtonChip(btn.bounds, pressed, btn.tint, btn.isWide, opacity, textMeasurer, btn.label, btn.fontSizeSp)
            }
        }
    }
}

// ─── SystemBar ───────────────────────────────────────────────────────────────

@Composable
fun SystemBar(
    modifier: Modifier = Modifier,
    sizeScale: Float = 1.0f,
    opacity: Float = 0.85f,
    onPress: (Int, Boolean) -> Unit
) {
    val density = LocalDensity.current
    val selW = with(density) { (56.dp * sizeScale).toPx() }
    val startW = with(density) { (68.dp * sizeScale).toPx() }
    val h = with(density) { (28.dp * sizeScale).toPx() }
    val gap = with(density) { (12.dp * sizeScale).toPx() }
    val totalW = selW + gap + startW

    val selRect = Rect(Offset(0f, 0f), Size(selW, h))
    val startRect = Rect(Offset(selW + gap, 0f), Size(startW, h))

    val buttons = listOf(
        ClusterButton(PhobosCore.Input.SELECT, "SEL", selRect, isWide = true, fontSizeSp = 10f),
        ClusterButton(PhobosCore.Input.START, "START", startRect, isWide = true, fontSizeSp = 10f),
    )

    ClusterDispatcher(
        modifier = modifier.size(with(density) { totalW.toDp() }, 28.dp * sizeScale),
        buttons = buttons,
        opacity = opacity,
        onPress = onPress
    )
}
