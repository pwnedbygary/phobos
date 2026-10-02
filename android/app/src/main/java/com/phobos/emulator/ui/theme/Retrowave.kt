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
import com.phobos.emulator.data.RetrowaveBackdropScene
import kotlin.math.min
import kotlin.random.Random

/**
 * Synthwave backdrop behind the app's screens: a sunset, an endless grid, or a city skyline.
 * Colors come from the active theme, so it suits any palette.
 */
@Composable
fun RetrowaveBackdrop(
    modifier: Modifier = Modifier,
    scene: RetrowaveBackdropScene = RetrowaveBackdropScene.SUNSET,
) {
    val scheme = MaterialTheme.colorScheme
    val isDark = LocalPhobosTheme.current.isDark
    Spacer(modifier.drawWithCache { sunsetGrid(scheme, isDark, scene) })
}

/**
 * A soft plate of [color] at [alpha] behind text drawn straight on the sunset (headers, notes, empty
 * states), so it stays readable where it passes over the sun ([GlassStyle.backdropPlateAlpha]). Draws
 * nothing at zero.
 */
fun Modifier.sunsetPlate(color: Color, alpha: Float): Modifier = if (alpha <= 0f) this else drawWithCache {
    val reach = PLATE_REACH.toPx()
    val paint = Paint().apply {
        this.color = color.copy(alpha = alpha)
        // Hardware-accelerated canvases ignore mask filters before Android 9, which get a hard-edged plate.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            asFrameworkPaint().maskFilter = BlurMaskFilter(PLATE_BLUR.toPx(), BlurMaskFilter.Blur.NORMAL)
        }
    }
    val plate = Rect(-reach, -reach, size.width + reach, size.height + reach)
    onDrawBehind { drawIntoCanvas { it.drawRoundRect(plate.left, plate.top, plate.right, plate.bottom, reach, reach, paint) } }
}

private val PLATE_BLUR = 6.dp

/** Past the text by more than the blur's falloff (about 1.7 times its radius), so the plate is at full alpha behind every glyph. */
private val PLATE_REACH = 14.dp

/** The sunset's colors, shared with the glass panels' contrast search ([GlassStyle]). */
internal class SunsetColors(scheme: ColorScheme, val isDark: Boolean) {
    // Light themes get a paler scene so text over it stays crisp.
    private val strength = if (isDark) 1f else 0.6f
    val skyTop = scheme.background
    val skyMiddle = lerp(scheme.background, scheme.secondary, 0.06f * strength)
    val skyBottom = lerp(scheme.background, scheme.primary, 0.34f * strength)
    val sunTop = scheme.tertiary
    val sunBottom = scheme.primary
    val sunAlpha = 0.9f * strength
    val sunGlow = scheme.primary.copy(alpha = 0.3f * strength)
    val floorTop = lerp(scheme.background, scheme.primary, 0.14f * strength)
    val floorBottom = scheme.background
    val grid = scheme.secondary.copy(alpha = 0.5f * strength)
    val horizonGlow = scheme.primary.copy(alpha = 0.35f * strength)
    val horizonLine = scheme.primary.copy(alpha = 0.9f * strength)
    val star = scheme.onBackground
    val starMaxAlpha = 0.6f
}

private fun CacheDrawScope.sunsetGrid(scheme: ColorScheme, isDark: Boolean, scene: RetrowaveBackdropScene): DrawResult {
    val width = size.width
    val height = size.height
    val colors = SunsetColors(scheme, isDark)
    val horizon = height * 0.62f

    val sky = Brush.verticalGradient(
        0f to colors.skyTop,
        0.5f to colors.skyMiddle,
        1f to colors.skyBottom,
        startY = 0f,
        endY = horizon,
    )
    val sunRadius = min(width, height) * 0.2f
    val sunCenter = Offset(width / 2f, horizon - sunRadius * 0.3f)
    val sun = Brush.verticalGradient(listOf(colors.sunTop, colors.sunBottom), startY = sunCenter.y - sunRadius, endY = horizon)
    val sunGlow = Brush.radialGradient(
        listOf(colors.sunGlow, Color.Transparent),
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
    val floor = Brush.verticalGradient(listOf(colors.floorTop, colors.floorBottom), startY = horizon, endY = height)
    val grid = Brush.verticalGradient(listOf(colors.grid.copy(alpha = 0f), colors.grid), startY = horizon, endY = height)
    val glowHalf = 18.dp.toPx()
    val horizonGlow = Brush.verticalGradient(
        listOf(Color.Transparent, colors.horizonGlow, Color.Transparent),
        startY = horizon - glowHalf,
        endY = horizon + glowHalf,
    )
    val lineWidth = 1.2.dp.toPx()
    val columnSpacing = width / 7f
    val random = Random(84)
    val stars = if (isDark) List(48) { Star(Offset(random.nextFloat() * width, random.nextFloat() * horizon * 0.85f), random.nextFloat()) } else emptyList()

    return onDrawBehind {
        drawRect(sky, size = Size(width, horizon))
        stars.forEach { star ->
            drawCircle(colors.star, radius = (0.6f + star.twinkle).dp.toPx(), center = star.position, alpha = 0.15f + (colors.starMaxAlpha - 0.15f) * star.twinkle)
        }
        if (scene == RetrowaveBackdropScene.SUNSET) {
            drawCircle(sunGlow, radius = sunRadius * 2.4f, center = sunCenter)
            clipRect(bottom = horizon) {
                clipPath(bands, ClipOp.Difference) {
                    drawCircle(sun, radius = sunRadius, center = sunCenter, alpha = colors.sunAlpha)
                }
            }
        }
        if (scene == RetrowaveBackdropScene.CITY) {
            val silhouette = lerp(colors.skyBottom, Color.Black, if (isDark) 0.72f else 0.48f)
            var x = -width * 0.04f
            var building = 0
            while (x < width) {
                val buildingWidth = width * (0.06f + (building % 4) * 0.025f)
                val buildingHeight = height * (0.10f + (building % 5) * 0.035f)
                drawRect(silhouette, Offset(x, horizon - buildingHeight), Size(buildingWidth, buildingHeight))
                if (building % 3 == 0) {
                    drawRect(scheme.secondary.copy(alpha = 0.25f), Offset(x + buildingWidth * 0.45f, horizon - buildingHeight - height * 0.025f), Size(1.dp.toPx(), height * 0.025f))
                }
                x += buildingWidth * 0.86f
                building++
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
        drawLine(colors.horizonLine, Offset(0f, horizon), Offset(width, horizon), 1.5.dp.toPx())
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
