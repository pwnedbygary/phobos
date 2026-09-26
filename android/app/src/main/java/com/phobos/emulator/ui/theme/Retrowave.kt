package com.phobos.emulator.ui.theme

import android.graphics.BlurMaskFilter
import android.os.Build
import androidx.compose.foundation.layout.Spacer
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.CacheDrawScope
import androidx.compose.ui.draw.DrawResult
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.ClipOp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.PaintingStyle
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.dp
import kotlin.math.min
import kotlin.random.Random

/**
 * Synthwave sunset behind the app's screens: a sky fading into the theme's primary color, a banded
 * sun and a perspective grid floor. Colors come from the active theme, so it suits any palette, and
 * the drawing is cached until the size or colors change.
 */
@Composable
fun RetrowaveBackdrop(modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    val isDark = LocalPhobosTheme.current.isDark
    Spacer(modifier.drawWithCache { sunsetGrid(scheme, isDark) })
}

private fun CacheDrawScope.sunsetGrid(scheme: ColorScheme, isDark: Boolean): DrawResult {
    val width = size.width
    val height = size.height
    // Light themes get a paler scene so text over it stays crisp.
    val strength = if (isDark) 1f else 0.6f
    val background = scheme.background
    val primary = scheme.primary
    val horizon = height * 0.62f

    val sky = Brush.verticalGradient(
        0f to background,
        0.5f to lerp(background, scheme.secondary, 0.06f * strength),
        1f to lerp(background, primary, 0.34f * strength),
        startY = 0f,
        endY = horizon,
    )
    val sunRadius = min(width, height) * 0.2f
    val sunCenter = Offset(width / 2f, horizon - sunRadius * 0.3f)
    val sun = Brush.verticalGradient(listOf(scheme.tertiary, primary), startY = sunCenter.y - sunRadius, endY = horizon)
    val sunGlow = Brush.radialGradient(
        listOf(primary.copy(alpha = 0.3f * strength), Color.Transparent),
        center = sunCenter,
        radius = sunRadius * 2.4f,
    )
    // Bands cut out of the sun's lower part, thickening toward the horizon.
    val bands = Path().apply {
        val top = sunCenter.y - sunRadius * 0.25f
        repeat(5) { i ->
            val t = i / 5f
            val y = top + (horizon - top) * t
            addRect(Rect(sunCenter.x - sunRadius, y, sunCenter.x + sunRadius, y + sunRadius * (0.02f + 0.045f * t)))
        }
    }
    val floor = Brush.verticalGradient(listOf(lerp(background, primary, 0.14f * strength), background), startY = horizon, endY = height)
    val gridColor = scheme.secondary.copy(alpha = 0.5f * strength)
    val grid = Brush.verticalGradient(listOf(gridColor.copy(alpha = 0f), gridColor), startY = horizon, endY = height)
    val glowHalf = 18.dp.toPx()
    val horizonGlow = Brush.verticalGradient(
        listOf(Color.Transparent, primary.copy(alpha = 0.35f * strength), Color.Transparent),
        startY = horizon - glowHalf,
        endY = horizon + glowHalf,
    )
    val lineWidth = 1.2.dp.toPx()
    val columnSpacing = width / 7f
    val random = Random(84)
    val stars = if (isDark) List(48) { Star(Offset(random.nextFloat() * width, random.nextFloat() * horizon * 0.85f), random.nextFloat()) } else emptyList()
    val starColor = scheme.onBackground

    return onDrawBehind {
        drawRect(sky, size = Size(width, horizon))
        stars.forEach { star ->
            drawCircle(starColor, radius = (0.6f + star.twinkle).dp.toPx(), center = star.position, alpha = 0.15f + 0.45f * star.twinkle)
        }
        drawCircle(sunGlow, radius = sunRadius * 2.4f, center = sunCenter)
        clipRect(bottom = horizon) {
            clipPath(bands, ClipOp.Difference) {
                drawCircle(sun, radius = sunRadius, center = sunCenter, alpha = 0.9f * strength)
            }
        }
        drawRect(floor, topLeft = Offset(0f, horizon), size = Size(width, height - horizon))
        for (row in 1..12) {
            val t = row / 12f
            val y = horizon + (height - horizon) * t * t
            drawLine(grid, Offset(0f, y), Offset(width, y), lineWidth)
        }
        for (column in -12..12) {
            val top = Offset(width / 2f + column * columnSpacing * 0.08f, horizon)
            drawLine(grid, top, Offset(width / 2f + column * columnSpacing, height), lineWidth)
        }
        drawRect(horizonGlow, topLeft = Offset(0f, horizon - glowHalf), size = Size(width, glowHalf * 2))
        drawLine(primary.copy(alpha = 0.9f * strength), Offset(0f, horizon), Offset(width, horizon), 1.5.dp.toPx())
    }
}

private class Star(val position: Offset, val twinkle: Float)

/**
 * Neon light-pipe outline modeled on the Mupen64Plus-AE Turnip card glow: a blurred halo behind
 * the element, then a bright tube with a white-hot filament drawn on its edge. [intensity] scales
 * every layer.
 */
fun Modifier.neonGlow(color: Color, shape: Shape, intensity: Float = 1f): Modifier = drawWithCache {
    val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawWithCache)) }
    val halo = Paint().apply {
        style = PaintingStyle.Stroke
        strokeWidth = 8.dp.toPx()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            this.color = color.copy(alpha = (0.55f * intensity).coerceIn(0f, 1f))
            asFrameworkPaint().maskFilter = BlurMaskFilter(6.dp.toPx(), BlurMaskFilter.Blur.NORMAL)
        } else {
            // Hardware-accelerated canvases ignore mask filters before Android 9; a faint wide stroke stands in.
            this.color = color.copy(alpha = (0.18f * intensity).coerceIn(0f, 1f))
        }
    }
    val tube = Stroke(width = 1.6.dp.toPx())
    val filament = Stroke(width = 0.7.dp.toPx())
    val filamentColor = lerp(color, Color.White, 0.65f)
    val alpha = intensity.coerceIn(0f, 1f)
    onDrawWithContent {
        drawIntoCanvas { it.drawPath(path, halo) }
        drawContent()
        drawPath(path, color, alpha = 0.95f * alpha, style = tube)
        drawPath(path, filamentColor, alpha = 0.9f * alpha, style = filament)
    }
}

/** Retrowave bar chrome: a faint accent wash with a glowing gradient line along one edge. */
fun Modifier.neonBar(scheme: ColorScheme, lineAtBottom: Boolean): Modifier = drawWithCache {
    val primary = scheme.primary
    val wash = Brush.horizontalGradient(
        listOf(primary.copy(alpha = 0.14f), scheme.secondary.copy(alpha = 0.05f), scheme.tertiary.copy(alpha = 0.12f)),
    )
    val line = Brush.horizontalGradient(listOf(primary, scheme.secondary, scheme.tertiary))
    val lineHeight = 1.5.dp.toPx()
    val glowHeight = 14.dp.toPx()
    val edge = if (lineAtBottom) size.height else 0f
    val glowTop = if (lineAtBottom) edge else edge - glowHeight
    val glow = Brush.verticalGradient(
        if (lineAtBottom) listOf(primary.copy(alpha = 0.3f), Color.Transparent) else listOf(Color.Transparent, primary.copy(alpha = 0.3f)),
        startY = glowTop,
        endY = glowTop + glowHeight,
    )
    onDrawBehind {
        drawRect(wash)
        drawRect(glow, topLeft = Offset(0f, glowTop), size = Size(size.width, glowHeight))
        drawRect(line, topLeft = Offset(0f, if (lineAtBottom) edge - lineHeight else 0f), size = Size(size.width, lineHeight))
    }
}

/** Soft radial bloom behind an icon or dot. */
fun Modifier.neonBloom(color: Color): Modifier = drawBehind {
    val radius = size.maxDimension
    drawCircle(Brush.radialGradient(listOf(color.copy(alpha = 0.5f), Color.Transparent), center = center, radius = radius), radius = radius)
}
