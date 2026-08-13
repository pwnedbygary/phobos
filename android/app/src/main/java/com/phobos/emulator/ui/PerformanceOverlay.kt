package com.phobos.emulator.ui

import android.app.ActivityManager
import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.PerformanceStats
import kotlin.math.roundToInt

/**
 * Draggable and resizable performance overlay.
 *
 * Tap-hold-anywhere and drag to reposition.
 * Drag the corner handle to resize.
 *
 * Position (0..1 fraction of display) and scale are persisted via settings.
 */
@Composable
fun PerformanceOverlay(
    perfStats: PerformanceStats,
    savedScale: Float,
    savedPosX: Float,
    savedPosY: Float,
    screenWidth: Int,
    screenHeight: Int,
    onScaleChanged: (Float) -> Unit,
    onPositionChanged: (Float, Float) -> Unit,
    modifier: Modifier = Modifier,
    showFps: Boolean = true,
    showFrameTime: Boolean = true,
    showRam: Boolean = true,
    showCore: Boolean = true,
    showShaderFails: Boolean = false,
) {
    val baseFontSizeSp = 10f
    var scale by remember { mutableFloatStateOf(savedScale.coerceIn(0.5f, 3.0f)) }
    val fontSizeSp = baseFontSizeSp * scale

    // Position from saved 0..1 fraction
    var offsetX by remember { mutableFloatStateOf(
        (savedPosX * (screenWidth - 200 * scale)).coerceIn(0f, maxOf(1f, screenWidth - 100f))
    )}
    var offsetY by remember { mutableFloatStateOf(
        (savedPosY * (screenHeight - 200 * scale)).coerceIn(0f, maxOf(1f, screenHeight - 100f))
    )}

    // RAM measurement
    var ramLabel by remember { mutableStateOf("") }
    LaunchedEffect(perfStats.fps) {
        ramLabel = getRamLabel()
    }

    Box(modifier = modifier) {
        Box(
            modifier = Modifier
                .offset { IntOffset(offsetX.roundToInt(), offsetY.roundToInt()) }
                .pointerInput(Unit) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        offsetX = (offsetX + dragAmount.x).coerceIn(0f, maxOf(1f, (screenWidth - 100 * scale)))
                        offsetY = (offsetY + dragAmount.y).coerceIn(0f, maxOf(1f, (screenHeight - 100 * scale)))
                        onPositionChanged(
                            offsetX / maxOf(1f, screenWidth - 200 * scale),
                            offsetY / maxOf(1f, screenHeight - 200 * scale),
                        )
                    }
                }
        ) {
            Column(
                modifier = Modifier
                    .background(Color.Black.copy(alpha = 0.55f), RoundedCornerShape(6.dp))
                    .padding(horizontal = (6 * scale).dp, vertical = (4 * scale).dp),
                horizontalAlignment = Alignment.Start,
            ) {
                // ── FPS (color-coded) ────────────────────────────────────────
                if (showFps) {
                    val fpsColor = when {
                        perfStats.fps <= 0 -> Color.LightGray
                        perfStats.fps >= 59.5 -> Color(0xFF4CAF50)
                        perfStats.fps >= 45 -> Color(0xFFFFEB3B)
                        perfStats.fps >= 30 -> Color(0xFFFF9800)
                        else -> Color(0xFFF44336)
                    }
                    Text(
                        "%.1f".format(perfStats.fps),
                        color = fpsColor,
                        fontSize = (fontSizeSp * 1.8).sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                    )
                    Text(
                        "FPS",
                        color = fpsColor.copy(alpha = 0.6f),
                        fontSize = (fontSizeSp * 0.7).sp,
                        fontFamily = FontFamily.Monospace,
                    )

                    Spacer(Modifier.height((2 * scale).dp))
                }

                // ── Frame time ───────────────────────────────────────────────
                if (showFrameTime) {
                    val ftColor = if (perfStats.frameTime > 16.67) Color(0xFFFFEB3B) else Color(0xFFB0BEC5)
                    Text(
                        "%.1fms".format(perfStats.frameTime),
                        color = ftColor,
                        fontSize = (fontSizeSp * 0.85).sp,
                        fontFamily = FontFamily.Monospace,
                    )
                }

                // ── RAM ──────────────────────────────────────────────────────
                if (showRam && ramLabel.isNotEmpty()) {
                    Text(
                        ramLabel,
                        color = Color(0xFF80CBC4),
                        fontSize = (fontSizeSp * 0.78).sp,
                        fontFamily = FontFamily.Monospace,
                    )
                }

                // ── Affinity core ────────────────────────────────────────────
                if (showCore) {
                    val coreColor = when {
                        perfStats.activeCore >= 6 -> Color(0xFFCE93D8)
                        perfStats.activeCore >= 4 -> Color(0xFF64B5F6)
                        else -> Color(0xFF78909C)
                    }
                    Text(
                        "Core %d".format(perfStats.activeCore),
                        color = coreColor,
                        fontSize = (fontSizeSp * 0.78).sp,
                        fontFamily = FontFamily.Monospace,
                    )
                }

                // ── Pipeline failures (N64 only) ─────────────────────────────
                if (showShaderFails && perfStats.pipelineFailures > 0) {
                    Spacer(Modifier.height((1 * scale).dp))
                    Text(
                        "\u26A0 %d shaders failed".format(perfStats.pipelineFailures),
                        color = Color(0xFFFF5722),
                        fontSize = (fontSizeSp * 0.72).sp,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }

            // ── Resize handle (bottom-right corner) ──────────────────────────
            Box(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .size((20 * scale).dp)
                    .background(Color.White.copy(alpha = 0.25f), RoundedCornerShape(bottomEnd = 6.dp))
                    .pointerInput(Unit) {
                        detectDragGestures { change, dragAmount ->
                            change.consume()
                            val delta = (dragAmount.x + dragAmount.y) * 0.01f
                            scale = (scale + delta).coerceIn(0.5f, 3.0f)
                            onScaleChanged(scale)
                        }
                    },
            )
        }
    }
}

/** Read JVM heap usage on the calling thread. */
private fun getRamLabel(): String {
    val runtime = Runtime.getRuntime()
    val usedMB = (runtime.totalMemory() - runtime.freeMemory()) / (1024 * 1024)
    val maxMB = runtime.maxMemory() / (1024 * 1024)
    return "RAM %d/%d MB".format(usedMB, maxMB)
}
