package com.phobos.emulator.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import com.phobos.emulator.data.CrtBackdropScene
import com.phobos.emulator.data.GlassBackdropScene
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.MangaBackdropScene
import com.phobos.emulator.data.PixelBackdropScene
import com.phobos.emulator.data.RetrowaveBackdropScene
import com.phobos.emulator.data.RpgBackdropScene
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.data.XmbBackdropScene

/** What screens need to know about the active theme beyond [MaterialTheme]. */
@Immutable
data class PhobosThemeInfo(
    /** The theme being shown. */
    val theme: AppTheme,
    val isDark: Boolean,
    /** The style layered over the theme's colors (Settings → Appearance → Style effects). */
    val effects: UiEffects,
    val success: Color,
    val warning: Color,
    /** The theme's final colors; [MaterialTheme.colorScheme] cross-fades to them after a change. */
    val scheme: ColorScheme,
    /** Glass panel and backdrop values for [scheme]; they switch at once while the colors cross-fade. */
    val glass: GlassStyle,
    /** The scene behind the pages in the Pixel art style. */
    val pixelBackdrop: PixelBackdropScene = PixelBackdropScene.SAKURA_CYCLE,
    val mangaBackdrop: MangaBackdropScene = MangaBackdropScene.TONE,
    val rpgBackdrop: RpgBackdropScene = RpgBackdropScene.LATTICE,
    val retrowaveBackdrop: RetrowaveBackdropScene = RetrowaveBackdropScene.SUNSET,
    val crtBackdrop: CrtBackdropScene = CrtBackdropScene.GREEN,
    val glassBackdrop: GlassBackdropScene = GlassBackdropScene.GLOWS,
    val xmbBackdrop: XmbBackdropScene = XmbBackdropScene.WAVES,
) {
    val retrowave: Boolean get() = effects == UiEffects.RETROWAVE

    /** The look of [effects]. */
    val style: UiStyle get() = effects.style

    /** [style] when it draws solid panels, headers and controls of its own; null for the glass look. */
    val solid: SolidUi? get() = style as? SolidUi
}

val LocalPhobosTheme = compositionLocalOf {
    val fallback = ThemeRegistry.find(ThemeRegistry.DEFAULT_ID)
    val colors = fallback.colors
    PhobosThemeInfo(
        fallback, fallback.isDark, UiEffects.NONE, colors.success, colors.warning, colors.scheme,
        GlassStyle.of(colors.scheme, colors.success, colors.warning, fallback.isDark, GlassScene.GLOWS),
    )
}

@Composable
fun PhobosTheme(
    themeId: String = ThemeRegistry.DEFAULT_ID,
    effects: UiEffects = UiEffects.NONE,
    glassEffects: GlassEffects = GlassEffects.FULL,
    pixelBackdrop: PixelBackdropScene = PixelBackdropScene.SAKURA_CYCLE,
    mangaBackdrop: MangaBackdropScene = MangaBackdropScene.TONE,
    rpgBackdrop: RpgBackdropScene = RpgBackdropScene.LATTICE,
    retrowaveBackdrop: RetrowaveBackdropScene = RetrowaveBackdropScene.SUNSET,
    crtBackdrop: CrtBackdropScene = CrtBackdropScene.GREEN,
    glassBackdrop: GlassBackdropScene = GlassBackdropScene.GLOWS,
    xmbBackdrop: XmbBackdropScene = XmbBackdropScene.WAVES,
    content: @Composable () -> Unit,
) {
    val scene = glassScene(effects, glassBackdrop, retrowaveBackdrop, xmbBackdrop)
    val theme = ThemeRegistry.find(themeId)
    val colors = theme.colors
    val colorScheme = animateColorScheme(colors.scheme)
    SystemBarsEffect(colorScheme, theme.isDark)
    val glass = remember(colors, theme.isDark, scene, glassEffects) {
        GlassStyle.of(colors.scheme, colors.success, colors.warning, theme.isDark, scene, glassEffects)
    }

    val info = PhobosThemeInfo(
        theme, theme.isDark, effects, colors.success, colors.warning, colors.scheme, glass,
        pixelBackdrop, mangaBackdrop, rpgBackdrop, retrowaveBackdrop, crtBackdrop, glassBackdrop, xmbBackdrop,
    )
    CompositionLocalProvider(LocalPhobosTheme provides info) {
        MaterialTheme(
            colorScheme = colorScheme,
            typography = effects.style.typography,
            shapes = effects.style.shapes,
            content = content,
        )
    }
}

/** Cross-fades every color role when [target] changes; the first scheme is shown without animating. */
@Composable
private fun animateColorScheme(target: ColorScheme): ColorScheme {
    var from by remember { mutableStateOf(target) }
    var to by remember { mutableStateOf(target) }
    val progress = remember { Animatable(1f) }
    LaunchedEffect(target) {
        if (target === to) return@LaunchedEffect
        from = lerp(from, to, progress.value)
        to = target
        progress.snapTo(0f)
        progress.animateTo(1f, tween(durationMillis = 450, easing = FastOutSlowInEasing))
    }
    val fraction = progress.value
    return if (fraction >= 1f) to else lerp(from, to, fraction)
}

@Suppress("DEPRECATION")
@Composable
private fun SystemBarsEffect(colorScheme: ColorScheme, isDark: Boolean) {
    val view = LocalView.current
    if (view.isInEditMode) return
    val statusBar = colorScheme.background
    // Pages run under the floating dock to the bottom of the screen and show through the navigation bar; from
    // Android 10 the system keeps three-button navigation legible over them with its own scrim.
    val navigationBar = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) Color.Transparent else colorScheme.background
    SideEffect {
        val window = (view.context as Activity).window
        // From Android 15 the app is edge-to-edge and draws behind transparent system bars itself.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            window.statusBarColor = statusBar.toArgb()
            window.navigationBarColor = navigationBar.toArgb()
        }
        WindowCompat.getInsetsController(window, view).apply {
            isAppearanceLightStatusBars = !isDark
            isAppearanceLightNavigationBars = !isDark
        }
    }
}

private fun lerp(a: ColorScheme, b: ColorScheme, t: Float): ColorScheme {
    if (t <= 0f) return a
    if (t >= 1f) return b
    return ColorScheme(
        primary = lerp(a.primary, b.primary, t),
        onPrimary = lerp(a.onPrimary, b.onPrimary, t),
        primaryContainer = lerp(a.primaryContainer, b.primaryContainer, t),
        onPrimaryContainer = lerp(a.onPrimaryContainer, b.onPrimaryContainer, t),
        inversePrimary = lerp(a.inversePrimary, b.inversePrimary, t),
        secondary = lerp(a.secondary, b.secondary, t),
        onSecondary = lerp(a.onSecondary, b.onSecondary, t),
        secondaryContainer = lerp(a.secondaryContainer, b.secondaryContainer, t),
        onSecondaryContainer = lerp(a.onSecondaryContainer, b.onSecondaryContainer, t),
        tertiary = lerp(a.tertiary, b.tertiary, t),
        onTertiary = lerp(a.onTertiary, b.onTertiary, t),
        tertiaryContainer = lerp(a.tertiaryContainer, b.tertiaryContainer, t),
        onTertiaryContainer = lerp(a.onTertiaryContainer, b.onTertiaryContainer, t),
        background = lerp(a.background, b.background, t),
        onBackground = lerp(a.onBackground, b.onBackground, t),
        surface = lerp(a.surface, b.surface, t),
        onSurface = lerp(a.onSurface, b.onSurface, t),
        surfaceVariant = lerp(a.surfaceVariant, b.surfaceVariant, t),
        onSurfaceVariant = lerp(a.onSurfaceVariant, b.onSurfaceVariant, t),
        surfaceTint = lerp(a.surfaceTint, b.surfaceTint, t),
        inverseSurface = lerp(a.inverseSurface, b.inverseSurface, t),
        inverseOnSurface = lerp(a.inverseOnSurface, b.inverseOnSurface, t),
        error = lerp(a.error, b.error, t),
        onError = lerp(a.onError, b.onError, t),
        errorContainer = lerp(a.errorContainer, b.errorContainer, t),
        onErrorContainer = lerp(a.onErrorContainer, b.onErrorContainer, t),
        outline = lerp(a.outline, b.outline, t),
        outlineVariant = lerp(a.outlineVariant, b.outlineVariant, t),
        scrim = lerp(a.scrim, b.scrim, t),
        surfaceBright = lerp(a.surfaceBright, b.surfaceBright, t),
        surfaceContainer = lerp(a.surfaceContainer, b.surfaceContainer, t),
        surfaceContainerHigh = lerp(a.surfaceContainerHigh, b.surfaceContainerHigh, t),
        surfaceContainerHighest = lerp(a.surfaceContainerHighest, b.surfaceContainerHighest, t),
        surfaceContainerLow = lerp(a.surfaceContainerLow, b.surfaceContainerLow, t),
        surfaceContainerLowest = lerp(a.surfaceContainerLowest, b.surfaceContainerLowest, t),
        surfaceDim = lerp(a.surfaceDim, b.surfaceDim, t),
    )
}
