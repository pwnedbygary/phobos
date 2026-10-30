package com.phobos.emulator.ui.carts

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.graphics.Typeface
import kotlin.math.roundToInt

/**
 * Generated cart labels, ported from Cartridge's `paintGeneratedLabel` (src/carts/pipeline.js:1261) and
 * `paintGameBoyStrip`.
 *
 * Parity note: port parity is not pixel parity - Cartridge paints with the browser's font rasteriser, this
 * paints with Skia. The JVM tests therefore assert the LAYOUT exactly (geometry, sizes, colours, wrapping with a
 * fixed measurer) and the draw sequence structurally through [LabelSurface] (see LabelPainterTest); rendered
 * pixels are verified on the RP6 with a stated tolerance, not here (the unit-test stack has no android.graphics).
 *
 * The Canvas application is deliberately behind [LabelSurface] so a future golden-image test can be added
 * without refactoring.
 */
object LabelPainter {

    data class Spec(
        val color: Int,
        val band: String?,
        val title: String,
        val sub: String? = null,
        val round: Boolean = false,
    )

    data class Text(
        val text: String,
        val x: Float,
        val baseline: Float,
        val size: Float,
        val weight: Int,
        val color: Int,
        val centered: Boolean = false,
    )

    data class Layout(
        val width: Int,
        val height: Int,
        val gradientTop: Int,
        val gradientMiddle: Int,
        val gradientBottom: Int,
        val stripeStep: Float,
        val stripeWidth: Float,
        val stripeXs: List<Float>,
        val band: Text?,
        val title: List<Text>,
        val sub: Text?,
        val gbStripWidth: Float,
        val gbStripColor: Int,
        val gbStripText: Text?,
    )

    /** Cartridge's `C(r, f, base)`: a linear mix of [color] toward white ("#ffffff") or black ("#000000"). */
    fun mix(color: Int, factor: Float, towardWhite: Boolean): Int {
        val target = if (towardWhite) 255 else 0
        fun ch(c: Int) = (c + (target - c) * factor).toInt().coerceIn(0, 255)
        val r = ch((color ushr 16) and 0xFF)
        val g = ch((color ushr 8) and 0xFF)
        val b = ch(color and 0xFF)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    /** Cartridge's `h(color)`: black or white text for contrast (threshold ported as luminance < 0.5 -> white). */
    fun textOn(color: Int): Int {
        val r = (color ushr 16) and 0xFF
        val g = (color ushr 8) and 0xFF
        val b = color and 0xFF
        val lum = (0.11f * r + 0.59f * g + 0.30f * b) / 255f
        return if (lum < 0.5f) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
    }

    private const val GB_STRIP_COLOR = 0xFFC9C8CE.toInt()
    private const val GB_STRIP_TEXT = 0xFF2A2A2E.toInt()

    /**
     * The pure layout. [measure] returns the width of [text] at [size] pixels; tests pass fixed metrics so the
     * numbers are deterministic across machines.
     */
    fun layout(spec: Spec, width: Int, height: Int, measure: (String, Float) -> Float): Layout {
        val w = width.toFloat()
        val h = height.toFloat()
        val textColor = textOn(spec.color)
        val stripeStep = w * 0.07f
        val stripes = generateSequence(-h) { it + stripeStep }.takeWhile { it < w + h }.toList()

        val l = w * 0.08f
        val round = spec.round
        val titleSize = if (round) (w * 0.10f).roundToInt().toFloat() else (minOf(w * 0.13f, h * 0.2f)).roundToInt().toFloat()

        if (round) {
            val title = Text(spec.title, w / 2f, h * 0.3f, titleSize, 800, textColor, centered = true)
            val sub = spec.sub?.let {
                Text(it, w / 2f, h * 0.78f, (w * 0.038f).roundToInt().toFloat(), 700, textColor, centered = true)
            }
            return Layout(width, height, mix(spec.color, 0.35f, true), spec.color, mix(spec.color, 0.5f, false),
                stripeStep, w * 0.02f, stripes, null, listOf(title), sub, 0f, GB_STRIP_COLOR, null)
        }

        val band = spec.band?.takeIf { it.isNotEmpty() }?.let {
            Text(it, l, l + minOf(w, h) * 0.06f, (minOf(w, h) * 0.075f).roundToInt().toFloat(), 700, alpha(textColor, 0.85f))
        }

        val lines = wrap(spec.title, w - l * 2, titleSize, measure, maxLines = 3)
        val lineHeight = titleSize * 1.02f
        var baseline = h - l - (if (spec.sub != null) titleSize * 0.55f else 0f) - (lines.size - 1) * lineHeight
        val title = lines.map {
            Text(it, l, baseline, titleSize, 800, textColor).also { _ -> baseline += lineHeight }
        }
        val sub = spec.sub?.let {
            Text(it, l, h - l * 0.8f, (titleSize * 0.42f).roundToInt().toFloat(), 600, alpha(textColor, 0.8f))
        }

        val gbText: Text? = null // the Game Boy side strip belongs to the art-crop path, not generated labels

        return Layout(width, height, mix(spec.color, 0.35f, true), spec.color, mix(spec.color, 0.5f, false),
            stripeStep, w * 0.02f, stripes, band, title, sub, 0f, GB_STRIP_COLOR, gbText)
    }

    /** Greedy word wrap measuring at [size]; at most [maxLines] lines (an over-long line is clipped by the caller). */
    fun wrap(text: String, maxWidth: Float, size: Float, measure: (String, Float) -> Float, maxLines: Int): List<String> {
        val words = text.split(' ').filter { it.isNotEmpty() }
        val lines = mutableListOf<String>()
        var line = ""
        for (word in words) {
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (line.isNotEmpty() && measure(candidate, size) > maxWidth) {
                lines += line
                line = word
                if (lines.size == maxLines) break
            } else {
                line = candidate
            }
        }
        if (lines.size < maxLines && line.isNotEmpty()) lines += line
        return lines
    }

    private fun alpha(color: Int, a: Float): Int =
        (((a * 255).toInt().coerceIn(0, 255)) shl 24) or (color and 0xFFFFFF)

    private fun Float.roundToInt(): Int = kotlin.math.round(this).toInt()

    /** The draw surface seam: Canvas in the app, a recording fake in tests. */
    interface LabelSurface {
        val width: Int
        val height: Int
        fun background(top: Int, middle: Int, bottom: Int, endX: Float, endY: Float)
        fun line(x0: Float, y0: Float, x1: Float, y1: Float, width: Float, color: Int)
        fun fillRect(x: Float, y: Float, w: Float, h: Float, color: Int)
        fun text(t: Text)
        fun rotate(degrees: Float, cx: Float, cy: Float, block: () -> Unit)
    }

    /** The Canvas application, in the same order as Cartridge's painter. */
    fun paint(surface: LabelSurface, layout: Layout) {
        surface.background(layout.gradientTop, layout.gradientMiddle, layout.gradientBottom,
            layout.width * 0.4f, layout.height.toFloat())
        for (x in layout.stripeXs) {
            surface.line(x, layout.height.toFloat(), x + layout.height, 0f, layout.stripeWidth, 0x14FFFFFF)
        }
        if (layout.gbStripWidth > 0f) {
            surface.fillRect(surface.width - layout.gbStripWidth, 0f, layout.gbStripWidth, layout.height.toFloat(), layout.gbStripColor)
            layout.gbStripText?.let { t ->
                surface.rotate(-90f, t.x, t.baseline) { surface.text(t) }
            }
        }
        layout.band?.let { surface.text(it) }
        for (t in layout.title) surface.text(t)
        layout.sub?.let { surface.text(it) }
    }
}

/** [LabelPainter.LabelSurface] over an Android Canvas; the typeface is the app's current UI font. */
class CanvasLabelSurface(
    private val canvas: Canvas,
    private val typeface: Typeface,
) : LabelPainter.LabelSurface {
    override val width = canvas.width
    override val height = canvas.height
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun background(top: Int, middle: Int, bottom: Int, endX: Float, endY: Float) {
        paint.shader = LinearGradient(0f, 0f, endX, endY, intArrayOf(top, middle, bottom), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        paint.style = Paint.Style.FILL
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        paint.shader = null
    }

    override fun line(x0: Float, y0: Float, x1: Float, y1: Float, w: Float, color: Int) {
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = w
        paint.color = color
        canvas.drawLine(x0, y0, x1, y1, paint)
    }

    override fun fillRect(x: Float, y: Float, w: Float, h: Float, color: Int) {
        paint.style = Paint.Style.FILL
        paint.color = color
        canvas.drawRect(x, y, x + w, y + h, paint)
    }

    override fun text(t: LabelPainter.Text) {
        paint.style = Paint.Style.FILL
        paint.color = t.color
        paint.textSize = t.size
        paint.typeface = Typeface.create(typeface, t.weight)
        paint.textAlign = if (t.centered) Paint.Align.CENTER else Paint.Align.LEFT
        canvas.drawText(t.text, t.x, t.baseline, paint)
    }

    override fun rotate(degrees: Float, cx: Float, cy: Float, block: () -> Unit) {
        canvas.save()
        canvas.rotate(degrees, cx, cy)
        block()
        canvas.restore()
    }
}
