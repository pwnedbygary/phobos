package com.phobos.emulator.art

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * Turns a downloaded label scan into the cropped label image, ported from Cartridge's `downloadLabel`
 * (electron/scraper.js:121-157): decode the scan, hand its RGBA pixels to [LabelCrop.labelRect] (the same
 * rectangle Cartridge finds), crop, scale so the largest side is [ArtKind.LABEL]'s width (downscale only, as the
 * picture path does), and for a round UMD scan cut it to the disc circle with transparency outside (the port of
 * Cartridge's round-scan handling, via [LabelCrop.isRoundScan]).
 *
 * Format decision (Cartridge's): a square crop is written JPEG at quality 88, a round UMD crop as PNG with the
 * corners transparent. Matching Cartridge keeps the store about seven times smaller for the same pixels (the
 * coordinator measured 551,515 B as PNG against 75,867 B as JPEG for one real 800x522 scan), which matters for the
 * owner's offline carts: roughly 2,000 labels fit the 256 MiB art store as JPEG against about 280 as PNG. The
 * store therefore names a label with the format its crop actually used.
 *
 * Returns null when the scan isn't a usable label for [shape] - the caller treats that as an [ArtGone] and tries
 * the next scan; otherwise the format the file was written in.
 */
object LabelPicture {

    enum class Format { JPEG, PNG }

    /** The largest side of a stored label, [ArtKind.LABEL].width, resolved once. */
    private val maxSide: Int get() = ArtKind.LABEL.width

    /** Cartridge's round-UMD rule: only a `umd` crop whose corners are background is masked to a disc. */
    fun isRoundLabel(shape: String, rgba: ByteArray, w: Int, h: Int, rect: LabelCrop.Rect): Boolean =
        shape == "umd" && LabelCrop.isRoundScan(rgba, w, h, rect)

    fun write(bytes: ByteArray, shape: String, file: File): Format? {
        val bitmap = try {
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        } catch (e: OutOfMemoryError) {
            return null
        } ?: return null
        try {
            val w = bitmap.width
            val h = bitmap.height
            if (w < 8 || h < 8) return null
            val pixels = IntArray(w * h)
            bitmap.getPixels(pixels, 0, w, 0, 0, w, h)
            val rgba = ByteArray(w * h * 4)
            for (i in pixels.indices) {
                val p = pixels[i]
                rgba[i * 4] = ((p ushr 16) and 0xFF).toByte()
                rgba[i * 4 + 1] = ((p ushr 8) and 0xFF).toByte()
                rgba[i * 4 + 2] = (p and 0xFF).toByte()
                rgba[i * 4 + 3] = ((p ushr 24) and 0xFF).toByte()
            }
            val rect = LabelCrop.labelRect(rgba, w, h, shape) ?: return null
            val round = isRoundLabel(shape, rgba, w, h, rect)
            var cropped = try {
                Bitmap.createBitmap(bitmap, rect.x, rect.y, rect.w, rect.h)
            } catch (e: IllegalArgumentException) {
                return null
            }
            try {
                val (sw, sh) = scaledSize(cropped.width, cropped.height, maxSide)
                if (sw != cropped.width || sh != cropped.height) {
                    val scaled = Bitmap.createScaledBitmap(cropped, sw, sh, true)
                    if (scaled !== cropped) cropped.recycle()
                    cropped = scaled
                }
                val out = if (round) masked(cropped) else cropped
                try {
                    FileOutputStream(file).use { stream ->
                        val ok = if (round) {
                            out.compress(Bitmap.CompressFormat.PNG, 100, stream)
                        } else {
                            out.compress(Bitmap.CompressFormat.JPEG, 88, stream)
                        }
                        if (!ok) throw IOException("not written")
                    }
                } finally {
                    if (out !== cropped) out.recycle()
                }
            } finally {
                if (cropped !== bitmap) cropped.recycle()
            }
            return if (round) Format.PNG else Format.JPEG
        } finally {
            bitmap.recycle()
        }
    }

    /** Clears everything outside the inscribed circle, keeping alpha inside it (Cartridge's round UMD cut). */
    private fun masked(source: Bitmap): Bitmap {
        val w = source.width
        val h = source.height
        val pixels = IntArray(w * h)
        source.getPixels(pixels, 0, w, 0, 0, w, h)
        for (y in 0 until h) for (x in 0 until w) {
            if (!inCircle(x, y, w, h)) pixels[y * w + x] = pixels[y * w + x] and 0x00FFFFFF
        }
        return Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888)
    }

    /** Downscale only: the cropped label's largest side becomes [max], smaller labels keep their size. */
    fun scaledSize(width: Int, height: Int, max: Int): Pair<Int, Int> {
        val largest = maxOf(width, height)
        if (largest <= max) return width to height
        val factor = max.toDouble() / largest
        return maxOf(1, (width * factor).toInt()) to maxOf(1, (height * factor).toInt())
    }

    /** True for a pixel whose centre lies inside the circle inscribed in the [w] x [h] image. */
    fun inCircle(x: Int, y: Int, w: Int, h: Int): Boolean {
        val cx = w / 2.0
        val cy = h / 2.0
        val r = minOf(w, h) / 2.0
        val dx = x + 0.5 - cx
        val dy = y + 0.5 - cy
        return dx * dx + dy * dy <= r * r
    }
}
