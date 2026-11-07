package com.phobos.emulator.ui.diorama

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import java.io.File
import java.io.InputStream

/**
 * Decodes a game's cover and puts it on the CRT: fit into the 4:3 face over a dark neutral glow, so the whole cover
 * stays legible instead of being cropped. The texture upload itself happens on the diorama render thread; this class
 * is meant for a background thread, never the frame loop.
 */
internal object DioramaArtLoader {

    /** Decode bound: box art can be 1000+ px tall, and the CRT texture is only 512x384. */
    private const val MAX_DECODE_EDGE = 1024

    /** Decodes a game's box art file and prepares it for the CRT; null when it is missing or unreadable. */
    fun loadFile(file: File): Bitmap? =
        decode { file.inputStream() }?.let { prepare(it) }

    /**
     * Draws [source] fitted (not cropped) onto the CRT's 4:3 dark screen and returns the 512x384 texture bitmap.
     * The source is recycled; the returned bitmap belongs to the caller.
     */
    fun prepare(source: Bitmap): Bitmap {
        val target = neutral(DioramaScreenArt.WIDTH, DioramaScreenArt.HEIGHT)
        val fit = DioramaScreenArt.fit(source.width, source.height)
        if (fit.width > 0 && fit.height > 0) {
            val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
            Canvas(target).drawBitmap(
                source,
                Rect(0, 0, source.width, source.height),
                RectF(
                    fit.x.toFloat(),
                    fit.y.toFloat(),
                    (fit.x + fit.width).toFloat(),
                    (fit.y + fit.height).toFloat(),
                ),
                paint,
            )
        }
        runCatching { source.recycle() }
        return target
    }

    /** The no-art CRT: a dark neutral screen with a soft centre glow, drawn once per hero. */
    fun neutral(): Bitmap = neutral(DioramaScreenArt.WIDTH, DioramaScreenArt.HEIGHT)

    fun neutral(width: Int, height: Int): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            shader = RadialGradient(
                width * 0.5f,
                height * 0.42f,
                height * 0.95f,
                intArrayOf(0xFF273240.toInt(), 0xFF11161F.toInt(), 0xFF06080C.toInt()),
                floatArrayOf(0f, 0.58f, 1f),
                Shader.TileMode.CLAMP,
            )
        }
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        // A faint CRT sheen across the top, so the dark screen still reads as glass rather than a hole.
        val sheen = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            shader = LinearGradient(
                0f, 0f, 0f, height * 0.55f,
                intArrayOf(0x1AFFFFFF, 0x00FFFFFF),
                floatArrayOf(0f, 1f),
                Shader.TileMode.CLAMP,
            )
        }
        canvas.drawRect(0f, 0f, width.toFloat(), height * 0.55f, sheen)
        return bitmap
    }

    private inline fun decode(open: () -> InputStream): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        runCatching { open().use { BitmapFactory.decodeStream(it, null, bounds) } }.getOrNull()
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= MAX_DECODE_EDGE || bounds.outHeight / (sample * 2) >= MAX_DECODE_EDGE) {
            sample *= 2
        }
        val options = BitmapFactory.Options().apply {
            inSampleSize = sample
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        return runCatching { open().use { BitmapFactory.decodeStream(it, null, options) } }.getOrNull()
    }
}
