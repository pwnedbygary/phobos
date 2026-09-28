package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.Color
import kotlin.math.atan2
import kotlin.math.cbrt
import kotlin.math.hypot
import kotlin.math.pow

/**
 * A color in Oklab, Björn Ottosson's perceptual color space: [l] is lightness from 0 (black) to 1
 * (white), [a] runs green to red and [b] blue to yellow. Equal distances look about equally
 * different, so lightness and hue can be compared and colors blended there.
 */
internal data class Oklab(val l: Float, val a: Float, val b: Float) {
    val chroma: Float get() = hypot(a, b)

    /** Hue angle in radians. */
    val hue: Float get() = atan2(b, a)

    fun mix(other: Oklab, fraction: Float) = Oklab(
        l + (other.l - l) * fraction,
        a + (other.a - a) * fraction,
        b + (other.b - b) * fraction,
    )

    /** The sRGB color, with channels outside the sRGB gamut clipped. */
    fun toColor(alpha: Float = 1f): Color {
        val l_ = l + 0.3963377774f * a + 0.2158037573f * b
        val m_ = l - 0.1055613458f * a - 0.0638541728f * b
        val s_ = l - 0.0894841775f * a - 1.2914855480f * b
        val l3 = l_ * l_ * l_
        val m3 = m_ * m_ * m_
        val s3 = s_ * s_ * s_
        val r = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3
        val g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3
        val bl = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3
        return Color(encode(r), encode(g), encode(bl), alpha)
    }

    companion object {
        fun of(color: Color): Oklab {
            val r = decode(color.red)
            val g = decode(color.green)
            val b = decode(color.blue)
            val l_ = cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b)
            val m_ = cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b)
            val s_ = cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b)
            return Oklab(
                0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
                1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
                0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
            )
        }

        /** sRGB channel to linear light. */
        private fun decode(c: Float) = if (c <= 0.04045f) c / 12.92f else ((c + 0.055f) / 1.055f).pow(2.4f)

        /** Linear light to an sRGB channel, clipped to 0..1. */
        private fun encode(c: Float): Float {
            val v = c.coerceIn(0f, 1f)
            return if (v <= 0.0031308f) 12.92f * v else 1.055f * v.pow(1f / 2.4f) - 0.055f
        }
    }
}
