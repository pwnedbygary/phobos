package com.phobos.emulator.ui.carts

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import com.phobos.emulator.art.LabelCrop
import java.io.File

/**
 * ONE label source for the grid cards and the live focused cart: the real LaunchBox scan when the art store has
 * one, else the box art cropped to the cart's label crop shape, else a generated label painted with the app's
 * current UI face. Rendered pixels of the generated branch are verified on the RP6 (the JVM suite has no graphics);
 * its layout is the tested [LabelPainter] one.
 */
object CartLabelSource {

    private const val GENERATED_WIDTH = 512
    private const val GENERATED_HEIGHT = 320
    private const val MAX_LABEL = 1024
    private const val GENERATED_COLOR = 0xFF33405C.toInt()

    fun label(
        context: Context,
        system: String,
        title: String,
        box: File?,
        scan: File?,
        style: String?,
    ): Bitmap? {
        decode(scan)?.let { return it }
        boxCrop(box, system)?.let { return it }
        return generated(context, style, title)
    }

    private fun decode(file: File?): Bitmap? =
        file?.let { runCatching { BitmapFactory.decodeFile(it.absolutePath) }.getOrNull() }

    /** The box art cropped to the label shape's rectangle, scaled down to the label cap (no upscaling). */
    private fun boxCrop(box: File?, system: String): Bitmap? {
        val source = decode(box) ?: return null
        val shape = CartShapes.labelShape(system)
        if (shape == null) {
            source.recycle()
            return null
        }
        return try {
            val w = source.width
            val h = source.height
            if (w < 8 || h < 8) return null
            val pixels = IntArray(w * h)
            source.getPixels(pixels, 0, w, 0, 0, w, h)
            val rgba = ByteArray(w * h * 4)
            for (i in pixels.indices) {
                val p = pixels[i]
                rgba[i * 4] = ((p ushr 16) and 0xFF).toByte()
                rgba[i * 4 + 1] = ((p ushr 8) and 0xFF).toByte()
                rgba[i * 4 + 2] = (p and 0xFF).toByte()
                rgba[i * 4 + 3] = ((p ushr 24) and 0xFF).toByte()
            }
            val rect = LabelCrop.labelRect(rgba, w, h, shape) ?: return null
            scaleDown(Bitmap.createBitmap(source, rect.x, rect.y, rect.w, rect.h))
        } catch (e: IllegalArgumentException) {
            null
        } finally {
            source.recycle()
        }
    }

    /** Only downscale, like Cartridge: the largest side is the label width (1024). */
    private fun scaleDown(bitmap: Bitmap): Bitmap {
        val max = maxOf(bitmap.width, bitmap.height)
        if (max <= MAX_LABEL) return bitmap
        val s = MAX_LABEL.toFloat() / max
        val out = Bitmap.createScaledBitmap(
            bitmap,
            (bitmap.width * s).toInt().coerceAtLeast(1),
            (bitmap.height * s).toInt().coerceAtLeast(1),
            true,
        )
        if (out !== bitmap) bitmap.recycle()
        return out
    }

    /** The generated label: [LabelPainter]'s tested layout on a Canvas with the app's bundled face for [style]. */
    private fun measurer(face: android.graphics.Typeface): (String, Float) -> Float {
        val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
        paint.typeface = face
        return { text, size ->
            paint.textSize = size
            paint.measureText(text)
        }
    }

    private fun generated(context: Context, style: String?, title: String): Bitmap? = runCatching {
        val bitmap = Bitmap.createBitmap(GENERATED_WIDTH, GENERATED_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val face = CartFaceResolver.typeface(context, style)
        val measure = measurer(face)
        val layout = LabelPainter.layout(
            LabelPainter.Spec(GENERATED_COLOR, null, title),
            GENERATED_WIDTH,
            GENERATED_HEIGHT,
        ) { text, size -> measure(text, size) }
        LabelPainter.paint(CanvasLabelSurface(canvas, face), layout)
        bitmap
    }.getOrNull()
}
