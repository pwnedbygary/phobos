package com.phobos.emulator.ui.theme

import android.app.Activity
import android.content.Context
import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import com.phobos.emulator.data.ThemeMode

/** What screens need to know about the active theme beyond [MaterialTheme]. */
@Immutable
data class PhobosThemeInfo(
    /** The theme being shown (the sibling when Auto switched to it). */
    val theme: AppTheme,
    val isDark: Boolean,
    val retrowave: Boolean,
    val success: Color,
    val warning: Color,
)

val LocalPhobosTheme = compositionLocalOf {
    val fallback = ThemeRegistry.resolve(ThemeRegistry.SYSTEM_ID, ThemeMode.DARK, followSystem = false, systemDark = true)
    PhobosThemeInfo(fallback.theme, fallback.isDark, retrowave = false, fallback.colors.success, fallback.colors.warning)
}

@Composable
fun PhobosTheme(
    themeId: String = ThemeRegistry.SYSTEM_ID,
    themeMode: ThemeMode = ThemeMode.AUTO,
    followSystem: Boolean = false,
    retrowave: Boolean = false,
    content: @Composable () -> Unit,
) {
    val resolved = ThemeRegistry.resolve(themeId, themeMode, followSystem, isSystemInDarkTheme())
    val context = LocalContext.current
    val useDynamic = resolved.theme.dynamic && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
    val colors = remember(resolved, useDynamic) {
        if (useDynamic) dynamicColors(context, resolved.isDark) else resolved.colors
    }
    val colorScheme = animateColorScheme(colors.scheme)
    SystemBarsEffect(colorScheme, resolved.isDark)

    val info = PhobosThemeInfo(resolved.theme, resolved.isDark, retrowave, colors.success, colors.warning)
    CompositionLocalProvider(LocalPhobosTheme provides info) {
        MaterialTheme(
            colorScheme = colorScheme,
            typography = PhobosTypography,
            shapes = PhobosShapes,
            content = content,
        )
    }
}

@RequiresApi(Build.VERSION_CODES.S)
private fun dynamicColors(context: Context, dark: Boolean): ThemeColors {
    val scheme = if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
    return SchemeBuilder.withStatusColors(scheme, dark)
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
    val navigationBar = colorScheme.surfaceContainer
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
