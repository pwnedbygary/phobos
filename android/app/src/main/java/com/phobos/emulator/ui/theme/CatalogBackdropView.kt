package com.phobos.emulator.ui.theme

import android.graphics.Bitmap
import androidx.compose.foundation.layout.Spacer
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.CatalogBackdrop
import com.phobos.emulator.data.CrtBackdropScene
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.GlassBackdropScene
import com.phobos.emulator.data.MangaBackdropScene
import com.phobos.emulator.data.PixelBackdropScene
import com.phobos.emulator.data.RetrowaveBackdropScene
import com.phobos.emulator.data.RpgBackdropScene
import com.phobos.emulator.data.XmbBackdropScene
import com.phobos.emulator.data.resolve
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Draws any [CatalogBackdrop] using the current theme colors, independent of which style effects
 * are active. Used by the screensaver and can preview backdrops from other families.
 */
@Composable
fun CatalogBackdropView(
    backdrop: CatalogBackdrop,
    settings: EmulatorSettings,
    modifier: Modifier = Modifier,
) {
    val scene = backdrop.resolve(settings)
    val theme = LocalPhobosTheme.current
    val scheme = MaterialTheme.colorScheme
    when (scene) {
        CatalogBackdrop.MATCH_APP -> Unit // resolved above
        CatalogBackdrop.PIXEL_SPACE -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.SPACE, modifier)
        CatalogBackdrop.PIXEL_NIGHT_DRIVE -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.NIGHT_DRIVE, modifier)
        CatalogBackdrop.PIXEL_PLAINS -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.PLAINS, modifier)
        CatalogBackdrop.PIXEL_SAKURA -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.SAKURA_CYCLE, modifier)
        CatalogBackdrop.PIXEL_SAKURA_NIGHT -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.SAKURA_NIGHT, modifier)
        CatalogBackdrop.PIXEL_SAKURA_DAY -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.SAKURA_DAY, modifier)
        CatalogBackdrop.PIXEL_UNDERWATER -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.UNDERWATER, modifier)
        CatalogBackdrop.PIXEL_CASTLE -> PixelBackdrop(scheme, theme.isDark, PixelBackdropScene.CASTLE, modifier)

        CatalogBackdrop.MANGA_TONE -> MangaBackdrop(scheme, theme.isDark, MangaBackdropScene.TONE, modifier)
        CatalogBackdrop.MANGA_SPEED_LINES -> MangaBackdrop(scheme, theme.isDark, MangaBackdropScene.SPEED_LINES, modifier)
        CatalogBackdrop.MANGA_SPLASH -> MangaBackdrop(scheme, theme.isDark, MangaBackdropScene.SPLASH, modifier)

        CatalogBackdrop.RPG_LATTICE -> RpgBackdrop(scheme, theme.isDark, RpgBackdropScene.LATTICE, modifier)
        CatalogBackdrop.RPG_STARS -> RpgBackdrop(scheme, theme.isDark, RpgBackdropScene.STARS, modifier)
        CatalogBackdrop.RPG_DUNGEON -> RpgBackdrop(scheme, theme.isDark, RpgBackdropScene.DUNGEON, modifier)

        CatalogBackdrop.RETRO_SUNSET -> RetrowaveBackdrop(modifier, RetrowaveBackdropScene.SUNSET)
        CatalogBackdrop.RETRO_GRID -> RetrowaveBackdrop(modifier, RetrowaveBackdropScene.GRID)
        CatalogBackdrop.RETRO_CITY -> RetrowaveBackdrop(modifier, RetrowaveBackdropScene.CITY)

        CatalogBackdrop.CRT_GREEN -> CrtBackdrop(scheme, theme.isDark, CrtBackdropScene.GREEN, modifier)
        CatalogBackdrop.CRT_AMBER -> CrtBackdrop(scheme, theme.isDark, CrtBackdropScene.AMBER, modifier)
        CatalogBackdrop.CRT_BLUE -> CrtBackdrop(scheme, theme.isDark, CrtBackdropScene.BLUE, modifier)

        CatalogBackdrop.GLASS_GLOWS -> GlassCatalogBackdrop(scheme, theme.isDark, GlassBackdropScene.GLOWS, modifier)
        CatalogBackdrop.GLASS_AURORA -> GlassCatalogBackdrop(scheme, theme.isDark, GlassBackdropScene.AURORA, modifier)
        CatalogBackdrop.GLASS_MESH -> GlassCatalogBackdrop(scheme, theme.isDark, GlassBackdropScene.MESH, modifier)

        CatalogBackdrop.XMB_WAVES -> XmbBackdrop(scheme, theme.isDark, XmbBackdropScene.WAVES, modifier)
        CatalogBackdrop.XMB_CALM -> XmbBackdrop(scheme, theme.isDark, XmbBackdropScene.CALM, modifier)
        CatalogBackdrop.XMB_DEEP -> XmbBackdrop(scheme, theme.isDark, XmbBackdropScene.DEEP, modifier)
    }
}

/** Glass family backdrops that do not need the live [GlassStyle] glows (screensaver / cross-style). */
@Composable
fun GlassCatalogBackdrop(
    scheme: ColorScheme,
    isDark: Boolean,
    scene: GlassBackdropScene,
    modifier: Modifier = Modifier,
) {
    when (scene) {
        GlassBackdropScene.GLOWS -> Spacer(
            modifier.drawWithCache {
                val strength = if (isDark) 0.55f else 0.35f
                val spots = listOf(
                    Triple(scheme.primary, Offset(0.18f * size.width, 0.22f * size.height), 0.55f),
                    Triple(scheme.secondary, Offset(0.82f * size.width, 0.28f * size.height), 0.5f),
                    Triple(scheme.tertiary, Offset(0.55f * size.width, 0.78f * size.height), 0.6f),
                )
                val brushes = spots.map { (color, center, radiusFrac) ->
                    val peak = color.copy(alpha = strength)
                    Brush.radialGradient(
                        0f to peak,
                        0.5f to peak.copy(alpha = peak.alpha * 0.45f),
                        1f to peak.copy(alpha = 0f),
                        center = center,
                        radius = radiusFrac * size.maxDimension,
                    )
                }
                onDrawBehind {
                    drawRect(scheme.background)
                    brushes.forEach { drawRect(it) }
                }
            },
        )
        GlassBackdropScene.AURORA -> Spacer(
            modifier.drawWithCache {
                val top = lerp(scheme.background, scheme.primary, if (isDark) 0.35f else 0.2f)
                val mid = lerp(scheme.background, scheme.tertiary, if (isDark) 0.28f else 0.18f)
                val bottom = lerp(scheme.background, scheme.secondary, if (isDark) 0.22f else 0.12f)
                val brush = Brush.verticalGradient(listOf(top, mid, bottom, scheme.background))
                onDrawBehind { drawRect(brush) }
            },
        )
        GlassBackdropScene.MESH -> Spacer(
            modifier.drawWithCache {
                val line = lerp(scheme.background, scheme.primary, if (isDark) 0.18f else 0.12f)
                val pitch = max(12f, min(size.width, size.height) / 24f)
                onDrawBehind {
                    drawRect(scheme.background)
                    var x = 0f
                    while (x <= size.width) {
                        drawLine(line, Offset(x, 0f), Offset(x, size.height), strokeWidth = 1f)
                        x += pitch
                    }
                    var y = 0f
                    while (y <= size.height) {
                        drawLine(line, Offset(0f, y), Offset(size.width, y), strokeWidth = 1f)
                        y += pitch
                    }
                }
            },
        )
    }
}
