package com.phobos.emulator.ui.touch

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathOperation
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Draws touch elements with a translucent "glass" material: soft drop shadow, dark gradient body,
 * a lit rim and a gloss highlight. Pressed controls fill with their accent color and glow.
 *
 * Text layouts and D-pad outlines are cached, so redraws during a gesture don't re-measure or
 * rebuild paths.
 */
class TouchPainter(private val textMeasurer: TextMeasurer) {
    // Pinch-resizing in the editor produces new sizes continuously, so both caches are capped.
    private val textCache = HashMap<String, TextLayoutResult>()
    private val dpadPathCache = HashMap<Int, Path>()

    fun DrawScope.drawElement(el: PlacedElement, engine: TouchEngine?, opacity: Float) {
        when (val element = el.element) {
            is DpadElement -> drawDpad(el, engine?.dpadBits(el) ?: 0, opacity)
            is AnalogElement -> drawStick(el, element, engine?.stickVisual(el), opacity, engine?.prefs?.analogMode)
            is ButtonCluster -> for (b in el.buttons) drawButton(b, engine?.isPressed(b) ?: false, opacity)
        }
    }

    // ── Buttons ────────────────────────────────────────────────────────────

    private fun DrawScope.drawButton(b: PlacedButton, pressed: Boolean, opacity: Float) {
        val accent = Color(b.button.accent)
        val shrink = if (pressed) PRESS_SCALE else 1f
        val halfW = b.halfW * shrink
        val halfH = b.halfH * shrink
        val rect = Rect(b.cx - halfW, b.cy - halfH, b.cx + halfW, b.cy + halfH)
        val shape = if (b.owner.mirrored) b.button.shape.mirrored() else b.button.shape
        val outline = buttonOutline(shape, rect)
        val minSide = min(b.halfW, b.halfH) * 2f

        drawSoftShadow(outline, minSide, opacity)
        if (pressed) drawGlow(b.cx, b.cy, maxOf(b.halfW, b.halfH), accent, opacity)
        drawBody(outline, rect, accent, pressed, opacity)
        drawRim(outline, rect, minSide, accent, pressed, opacity)
        drawGloss(rect, shape, opacity)

        val glyphColor = if (pressed) Color.White else labelColor(accent)
        drawGlyph(b.button, b.cx, b.cy, min(halfW, halfH), glyphColor, opacity)
    }

    private fun buttonOutline(shape: ButtonShape, rect: Rect): Path = Path().apply {
        when (shape) {
            ButtonShape.CIRCLE -> addOval(rect)
            ButtonShape.PILL -> addRoundRect(RoundRect(rect, CornerRadius(rect.height / 2f)))
            ButtonShape.SHOULDER_LEFT -> addRoundRect(
                RoundRect(
                    rect,
                    topLeft = CornerRadius(rect.height * 0.9f),
                    topRight = CornerRadius(rect.height * 0.35f),
                    bottomRight = CornerRadius(rect.height * 0.35f),
                    bottomLeft = CornerRadius(rect.height * 0.35f),
                )
            )
            ButtonShape.SHOULDER_RIGHT -> addRoundRect(
                RoundRect(
                    rect,
                    topLeft = CornerRadius(rect.height * 0.35f),
                    topRight = CornerRadius(rect.height * 0.9f),
                    bottomRight = CornerRadius(rect.height * 0.35f),
                    bottomLeft = CornerRadius(rect.height * 0.35f),
                )
            )
        }
    }

    // ── D-pad ──────────────────────────────────────────────────────────────

    private fun DrawScope.drawDpad(el: PlacedElement, bits: Int, opacity: Float) {
        val r = el.radius
        if (dpadPathCache.size > CACHE_LIMIT) dpadPathCache.clear()
        val cross = dpadPathCache.getOrPut(r.roundToInt()) { dpadOutline(r) }
        val bounds = Rect(el.cx - r, el.cy - r, el.cx + r, el.cy + r)
        val origin = Offset(el.cx, el.cy)
        val arm = r * DPAD_ARM

        translate(origin.x, origin.y) {
            drawSoftShadow(cross, r * 2f, opacity)
            drawBody(cross, bounds.translate(-origin), Color(TouchPalette.NEUTRAL), false, opacity)
            if (bits != 0) {
                val accent = Color(TouchPalette.GOLD)
                clipPath(cross) {
                    for ((bit, armRect) in dpadArms(r, arm)) {
                        if (bits and bit == 0) continue
                        drawRect(
                            brush = Brush.radialGradient(
                                listOf(lerp(accent, Color.White, 0.25f), accent.copy(alpha = 0.55f)),
                                center = armRect.center, radius = r,
                            ),
                            topLeft = armRect.topLeft, size = armRect.size, alpha = 0.9f * opacity,
                        )
                    }
                }
            }
            drawRim(cross, bounds.translate(-origin), r * 2f, Color(TouchPalette.NEUTRAL), false, opacity)

            // Center recess.
            drawCircle(
                brush = Brush.radialGradient(
                    listOf(Color.Black.copy(alpha = 0.45f), Color.Transparent),
                    center = Offset.Zero, radius = arm * 0.55f,
                ),
                radius = arm * 0.55f, center = Offset.Zero, alpha = opacity,
            )

            // Direction arrows.
            for ((bit, angle) in DPAD_ARROWS) {
                val lit = bits and bit != 0
                drawArrow(
                    cx = cos(angle) * r * 0.66f, cy = -sin(angle) * r * 0.66f, size = arm * 0.42f,
                    angle = angle, color = if (lit) Color.White else Color.White.copy(alpha = 0.55f),
                    opacity = opacity,
                )
            }
        }
    }

    private fun dpadOutline(r: Float): Path {
        val arm = r * DPAD_ARM
        val corner = CornerRadius(arm * 0.28f)
        val horizontal = Path().apply { addRoundRect(RoundRect(Rect(-r, -arm / 2f, r, arm / 2f), corner)) }
        val vertical = Path().apply { addRoundRect(RoundRect(Rect(-arm / 2f, -r, arm / 2f, r), corner)) }
        return Path().apply { op(horizontal, vertical, PathOperation.Union) }
    }

    private fun dpadArms(r: Float, arm: Float): List<Pair<Int, Rect>> = listOf(
        TouchEngine.UP to Rect(-arm / 2f, -r, arm / 2f, -arm * 0.15f),
        TouchEngine.DOWN to Rect(-arm / 2f, arm * 0.15f, arm / 2f, r),
        TouchEngine.LEFT to Rect(-r, -arm / 2f, -arm * 0.15f, arm / 2f),
        TouchEngine.RIGHT to Rect(arm * 0.15f, -arm / 2f, r, arm / 2f),
    )

    // ── Sticks ─────────────────────────────────────────────────────────────

    private fun DrawScope.drawStick(
        el: PlacedElement, element: AnalogElement, visual: StickVisual?, opacity: Float, mode: AnalogMode?,
    ) {
        val r = el.radius
        val floating = mode == AnalogMode.FLOATING
        val baseX = visual?.originX ?: el.cx
        val baseY = visual?.originY ?: el.cy
        val baseOpacity = if (floating && visual == null) opacity * 0.55f else opacity
        val center = Offset(baseX, baseY)
        val accent = Color(TouchPalette.GOLD)

        // Base well.
        drawCircle(
            brush = Brush.radialGradient(
                0f to Color.Black.copy(alpha = 0.35f), 0.8f to Color.Black.copy(alpha = 0.2f), 1f to Color.Transparent,
                center = center + Offset(0f, r * 0.05f), radius = r * 1.12f,
            ),
            radius = r * 1.12f, center = center + Offset(0f, r * 0.05f), alpha = baseOpacity,
        )
        drawCircle(
            brush = Brush.radialGradient(listOf(BODY_BOTTOM, BODY_TOP), center = center, radius = r),
            radius = r, center = center, alpha = 0.72f * baseOpacity,
        )
        drawCircle(
            brush = Brush.verticalGradient(
                listOf(Color.White.copy(alpha = 0.4f), Color.White.copy(alpha = 0.08f)),
                startY = baseY - r, endY = baseY + r,
            ),
            radius = r - 1.2f * density, center = center, alpha = baseOpacity,
            style = Stroke(width = 1.5f * density),
        )
        // Travel ring and optional octagonal gate.
        drawCircle(
            color = Color.White.copy(alpha = 0.08f), radius = r * TouchEngine.STICK_TRAVEL, center = center,
            alpha = baseOpacity, style = Stroke(width = 1f * density),
        )
        if (element.gate == StickGate.OCTAGON) {
            drawPath(
                octagon(center, r * 0.86f), color = Color.White.copy(alpha = 0.18f), alpha = baseOpacity,
                style = Stroke(width = 1.3f * density, join = StrokeJoin.Round),
            )
        }

        // Thumb.
        val thumbR = r * THUMB_RADIUS
        val thumb = Offset(baseX + (visual?.thumbDx ?: 0f), baseY + (visual?.thumbDy ?: 0f))
        if (visual != null) {
            drawLine(
                color = accent.copy(alpha = 0.35f), start = center, end = thumb,
                strokeWidth = thumbR * 0.35f, cap = StrokeCap.Round, alpha = opacity,
            )
            drawGlow(thumb.x, thumb.y, thumbR, accent, opacity)
        }
        drawCircle(
            brush = Brush.radialGradient(
                0f to Color.Black.copy(alpha = 0.5f), 1f to Color.Transparent,
                center = thumb + Offset(0f, thumbR * 0.12f), radius = thumbR * 1.25f,
            ),
            radius = thumbR * 1.25f, center = thumb + Offset(0f, thumbR * 0.12f), alpha = baseOpacity,
        )
        drawCircle(
            brush = Brush.verticalGradient(
                listOf(Color(0xFF474E60), Color(0xFF1D212B)), startY = thumb.y - thumbR, endY = thumb.y + thumbR,
            ),
            radius = thumbR, center = thumb, alpha = 0.95f * baseOpacity,
        )
        drawCircle(
            color = if (visual != null) accent else Color.White.copy(alpha = 0.45f),
            radius = thumbR - 0.8f * density, center = thumb, alpha = baseOpacity,
            style = Stroke(width = 1.6f * density),
        )
        drawCircle(
            color = Color.White.copy(alpha = 0.10f), radius = thumbR * 0.55f, center = thumb, alpha = baseOpacity,
            style = Stroke(width = 1f * density),
        )
    }

    private fun octagon(center: Offset, r: Float): Path = Path().apply {
        for (i in 0 until 8) {
            val a = Math.toRadians(22.5 + 45.0 * i)
            val x = center.x + (cos(a) * r).toFloat()
            val y = center.y - (sin(a) * r).toFloat()
            if (i == 0) moveTo(x, y) else lineTo(x, y)
        }
        close()
    }

    // ── Shared material ────────────────────────────────────────────────────

    private fun DrawScope.drawSoftShadow(outline: Path, size: Float, opacity: Float) {
        val dy = size * 0.04f
        translate(0f, dy) {
            drawPath(outline, Color.Black, alpha = 0.28f * opacity)
        }
        translate(0f, dy * 2f) {
            drawPath(outline, Color.Black, alpha = 0.12f * opacity)
        }
    }

    private fun DrawScope.drawGlow(cx: Float, cy: Float, r: Float, accent: Color, opacity: Float) {
        drawCircle(
            brush = Brush.radialGradient(
                0.55f to accent.copy(alpha = 0.55f), 1f to Color.Transparent,
                center = Offset(cx, cy), radius = r * 1.45f,
            ),
            radius = r * 1.45f, center = Offset(cx, cy), alpha = opacity,
        )
    }

    private fun DrawScope.drawBody(outline: Path, rect: Rect, accent: Color, pressed: Boolean, opacity: Float) {
        val brush = if (pressed) {
            Brush.verticalGradient(
                listOf(lerp(accent, Color.White, 0.2f), lerp(accent, Color.Black, 0.35f)),
                startY = rect.top, endY = rect.bottom,
            )
        } else {
            Brush.verticalGradient(listOf(BODY_TOP, BODY_BOTTOM), startY = rect.top, endY = rect.bottom)
        }
        drawPath(outline, brush, alpha = (if (pressed) 0.95f else BODY_ALPHA) * opacity)
    }

    private fun DrawScope.drawRim(outline: Path, rect: Rect, size: Float, accent: Color, pressed: Boolean, opacity: Float) {
        val width = maxOf(1.2f * density, size * 0.022f)
        val rim = if (pressed) {
            Brush.verticalGradient(
                listOf(Color.White.copy(alpha = 0.85f), accent.copy(alpha = 0.6f)),
                startY = rect.top, endY = rect.bottom,
            )
        } else {
            Brush.verticalGradient(
                listOf(Color.White.copy(alpha = 0.5f), Color.White.copy(alpha = 0.07f)),
                startY = rect.top, endY = rect.bottom,
            )
        }
        drawPath(outline, rim, alpha = opacity, style = Stroke(width = width))
    }

    private fun DrawScope.drawGloss(rect: Rect, shape: ButtonShape, opacity: Float) {
        val inset = rect.height * 0.12f
        val gloss = Rect(rect.left + inset, rect.top + inset * 0.6f, rect.right - inset, rect.center.y)
        val brush = Brush.verticalGradient(
            listOf(Color.White.copy(alpha = 0.16f), Color.Transparent),
            startY = gloss.top, endY = gloss.bottom,
        )
        if (shape == ButtonShape.CIRCLE) {
            drawOval(brush, gloss.topLeft, gloss.size, alpha = opacity)
        } else {
            drawRoundRect(brush, gloss.topLeft, gloss.size, CornerRadius(gloss.height), alpha = opacity)
        }
    }

    // ── Glyphs ─────────────────────────────────────────────────────────────

    private fun DrawScope.drawGlyph(button: TouchButton, cx: Float, cy: Float, half: Float, color: Color, opacity: Float) {
        val s = half * 0.48f
        val stroke = Stroke(width = maxOf(1.5f * density, half * 0.14f), cap = StrokeCap.Round, join = StrokeJoin.Round)
        when (button.glyph) {
            Glyph.TEXT -> drawLabel(button.label, cx, cy, half * 2f * button.labelScale, color, opacity)
            Glyph.PS_TRIANGLE -> drawPath(
                Path().apply {
                    moveTo(cx, cy - s)
                    lineTo(cx + s * 0.94f, cy + s * 0.62f)
                    lineTo(cx - s * 0.94f, cy + s * 0.62f)
                    close()
                },
                color, alpha = opacity, style = stroke,
            )
            Glyph.PS_CIRCLE -> drawCircle(color, radius = s * 0.9f, center = Offset(cx, cy), alpha = opacity, style = stroke)
            Glyph.PS_CROSS -> {
                val d = s * 0.78f
                drawLine(color, Offset(cx - d, cy - d), Offset(cx + d, cy + d), stroke.width, StrokeCap.Round, alpha = opacity)
                drawLine(color, Offset(cx + d, cy - d), Offset(cx - d, cy + d), stroke.width, StrokeCap.Round, alpha = opacity)
            }
            Glyph.PS_SQUARE -> {
                val d = s * 0.74f
                drawRect(color, Offset(cx - d, cy - d), Size(d * 2f, d * 2f), alpha = opacity, style = stroke)
            }
            Glyph.ARROW_UP -> drawArrow(cx, cy, half * 0.62f, HALF_PI, color, opacity)
            Glyph.ARROW_DOWN -> drawArrow(cx, cy, half * 0.62f, -HALF_PI, color, opacity)
            Glyph.ARROW_LEFT -> drawArrow(cx, cy, half * 0.62f, PI, color, opacity)
            Glyph.ARROW_RIGHT -> drawArrow(cx, cy, half * 0.62f, 0f, color, opacity)
            Glyph.MENU -> {
                val w = half * 0.5f
                val gap = half * 0.3f
                for (i in -1..1) {
                    drawLine(color, Offset(cx - w, cy + i * gap), Offset(cx + w, cy + i * gap), stroke.width * 0.8f, StrokeCap.Round, alpha = opacity)
                }
            }
            Glyph.FAST_FORWARD -> {
                val a = half * 0.36f
                drawArrow(cx - a * 0.55f, cy, a * 1.6f, 0f, color, opacity)
                drawArrow(cx + a * 0.55f, cy, a * 1.6f, 0f, color, opacity)
            }
            Glyph.KEYBOARD -> {
                val w = half * 0.62f
                val h = half * 0.42f
                drawRoundRect(color, Offset(cx - w, cy - h), Size(w * 2f, h * 2f), CornerRadius(h * 0.3f), alpha = opacity, style = Stroke(stroke.width * 0.7f))
                val key = half * 0.1f
                for (row in 0..1) for (col in -2..2) {
                    drawCircle(color, key, Offset(cx + col * w * 0.36f, cy - h * 0.35f + row * h * 0.6f), alpha = opacity)
                }
            }
        }
    }

    /** Filled triangle pointing along [angle] (radians, 0 = right, counter-clockwise). */
    private fun DrawScope.drawArrow(cx: Float, cy: Float, size: Float, angle: Float, color: Color, opacity: Float) {
        val path = Path()
        for (i in 0 until 3) {
            val a = angle + i * (2f * PI / 3f)
            val scale = if (i == 0) 0.62f else 0.5f
            val x = cx + cos(a) * size * scale
            val y = cy - sin(a) * size * scale
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        path.close()
        drawPath(path, color, alpha = opacity)
    }

    private fun DrawScope.drawLabel(label: String, cx: Float, cy: Float, fontPx: Float, color: Color, opacity: Float) {
        if (label.isEmpty()) return
        if (textCache.size > CACHE_LIMIT) textCache.clear()
        val layout = textCache.getOrPut("$label|${fontPx.toInt()}") {
            textMeasurer.measure(
                label,
                TextStyle(
                    fontSize = fontPx.toSp(),
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 0.5.sp,
                ),
                softWrap = false,
            )
        }
        drawText(
            layout,
            color = color,
            topLeft = Offset(cx - layout.size.width / 2f, cy - layout.size.height / 2f),
            alpha = opacity,
            shadow = Shadow(Color.Black.copy(alpha = 0.55f), Offset(0f, density), blurRadius = 2f * density),
        )
    }

    private fun labelColor(accent: Color): Color =
        if (accent == Color(TouchPalette.NEUTRAL)) Color(0xFFEFF2F7) else lerp(accent, Color.White, 0.15f)

    companion object {
        private val BODY_TOP = Color(0xFF2E3444)
        private val BODY_BOTTOM = Color(0xFF12151C)
        private const val CACHE_LIMIT = 96
        private const val BODY_ALPHA = 0.78f
        private const val PRESS_SCALE = 0.94f
        private const val DPAD_ARM = 0.66f
        private const val THUMB_RADIUS = 0.42f
        private const val PI = 3.1415927f
        private const val HALF_PI = PI / 2f
        private val DPAD_ARROWS = listOf(
            TouchEngine.UP to HALF_PI, TouchEngine.DOWN to -HALF_PI,
            TouchEngine.LEFT to PI, TouchEngine.RIGHT to 0f,
        )
    }
}
