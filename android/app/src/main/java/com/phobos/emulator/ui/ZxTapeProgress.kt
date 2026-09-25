package com.phobos.emulator.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

// ZX Spectrum rainbow stops (red -> yellow -> green -> blue).
private val ZxRainbow = listOf(Color(0xFFC3463A), Color(0xFFE2C332), Color(0xFF64A34A), Color(0xFF64ADD0))

/** Tape-loading bar for the ZX Spectrum: a segmented rainbow with a shimmer sweep. */
@Composable
fun ZxTapeProgressBar(percent: Float, modifier: Modifier = Modifier) {
    val shimmer by rememberInfiniteTransition(label = "tapeShimmer").animateFloat(
        initialValue = -1f, targetValue = 2f,
        animationSpec = infiniteRepeatable(tween(1600, easing = LinearEasing), RepeatMode.Restart),
        label = "tapeShimmerOffset",
    )

    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            "Loading tape… ${percent.toInt()}%",
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = Color.White,
            modifier = Modifier
                .shadow(6.dp, RoundedCornerShape(8.dp))
                .background(Color(0xCC101010), RoundedCornerShape(8.dp))
                .border(1.dp, Color.White.copy(alpha = 0.25f), RoundedCornerShape(8.dp))
                .padding(horizontal = 14.dp, vertical = 6.dp)
        )
        Spacer(Modifier.height(8.dp))
        // Segmented rainbow bar in a dark rounded frame so it reads on any background.
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(22.dp)
                .shadow(10.dp, RoundedCornerShape(11.dp))
                .background(Color(0xCC101010), RoundedCornerShape(11.dp))
                .border(2.dp, Color(0xE6151515), RoundedCornerShape(11.dp))
                .padding(2.dp)
        ) {
            Canvas(Modifier.fillMaxSize()) {
                val segGap = 1.5.dp.toPx()
                val segW = (size.width - segGap * 9) / 10f
                val h = size.height
                val corner = CornerRadius(5.dp.toPx(), 5.dp.toPx())
                val filledSegs = (percent / 10f).toInt().coerceIn(0, 10)
                for (i in 0 until 10) {
                    drawRoundRect(Color(0x66101010), Offset(i * (segW + segGap), 0f), Size(segW, h), corner)
                }
                for (i in 0 until filledSegs) {
                    val x = i * (segW + segGap)
                    drawRoundRect(lerpRainbow(i.toFloat() / filledSegs), Offset(x, 0f), Size(segW, h), corner)
                    // Top shine.
                    drawRoundRect(
                        Color.White.copy(alpha = 0.28f), Offset(x, 1.dp.toPx()), Size(segW, h * 0.35f),
                        CornerRadius(4.dp.toPx(), 4.dp.toPx()),
                    )
                }
                if (filledSegs > 0) {
                    val sweepX = (shimmer + 0.5f) * (segW + segGap) * filledSegs
                    val streakW = (segW + segGap) * 2f
                    drawRoundRect(Color.White.copy(alpha = 0.25f), Offset(sweepX - streakW / 2, 0f), Size(streakW, h), corner)
                }
            }
        }
    }
}

/** Interpolates along the ZX rainbow by [t] in 0..1. */
private fun lerpRainbow(t: Float): Color {
    val seg = t.coerceIn(0f, 1f) * (ZxRainbow.size - 1)
    val idx = seg.toInt().coerceIn(0, ZxRainbow.size - 2)
    val frac = seg - idx
    val a = ZxRainbow[idx]
    val b = ZxRainbow[idx + 1]
    return Color(
        red = a.red + (b.red - a.red) * frac,
        green = a.green + (b.green - a.green) * frac,
        blue = a.blue + (b.blue - a.blue) * frac,
        alpha = 1f,
    )
}
