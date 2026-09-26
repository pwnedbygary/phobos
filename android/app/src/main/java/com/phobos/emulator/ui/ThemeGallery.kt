package com.phobos.emulator.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BrightnessAuto
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.DarkMode
import androidx.compose.material.icons.rounded.LightMode
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.AppTheme
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neonGlow

/** Label and icon for a theme's appearance: adaptive themes follow the mode, others are fixed. */
fun AppTheme.appearanceLabel(): Pair<String, ImageVector> = when {
    adaptive -> "Auto" to Icons.Rounded.BrightnessAuto
    isDark -> "Dark" to Icons.Rounded.DarkMode
    else -> "Light" to Icons.Rounded.LightMode
}

/**
 * Gallery card drawn entirely in [scheme], the theme's own colors, so it previews the theme: its
 * background with a mini panel, text lines, accent dots and a switch. Selection uses the app's
 * current primary color.
 */
@Composable
fun ThemeSwatchCard(theme: AppTheme, scheme: ColorScheme, selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier) {
    val app = MaterialTheme.colorScheme
    val retrowave = LocalPhobosTheme.current.retrowave
    val shape = MaterialTheme.shapes.large
    val borderColor by animateColorAsState(if (selected) app.primary else app.outlineVariant.copy(alpha = 0.6f), label = "swatchBorder")
    val borderWidth by animateDpAsState(if (selected) 2.dp else 1.dp, label = "swatchBorderWidth")
    Surface(
        onClick = onClick,
        modifier = modifier
            .fillMaxWidth()
            .then(if (retrowave && selected) Modifier.neonGlow(app.primary, shape) else Modifier),
        shape = shape,
        color = scheme.surfaceContainer,
        contentColor = scheme.onSurface,
        border = BorderStroke(borderWidth, borderColor),
    ) {
        Column {
            Box(Modifier.fillMaxWidth().height(88.dp).background(scheme.background).padding(10.dp)) {
                MiniWindow(scheme, Modifier.fillMaxHeight().fillMaxWidth(0.72f))
                Column(Modifier.align(Alignment.TopEnd), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                    listOf(scheme.primary, scheme.secondary, scheme.tertiary).forEach { Dot(it, 12.dp) }
                }
                MiniSwitch(scheme, Modifier.align(Alignment.BottomEnd))
                if (selected) {
                    Box(
                        Modifier.align(Alignment.Center).size(26.dp).clip(CircleShape).background(app.primary),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(Icons.Rounded.Check, contentDescription = "Selected", tint = app.onPrimary, modifier = Modifier.size(18.dp))
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    theme.name,
                    style = MaterialTheme.typography.titleSmall,
                    color = scheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                val (label, icon) = theme.appearanceLabel()
                Icon(icon, contentDescription = label, tint = scheme.onSurfaceVariant, modifier = Modifier.size(16.dp))
            }
        }
    }
}

@Composable
private fun MiniWindow(scheme: ColorScheme, modifier: Modifier) {
    Column(
        modifier.clip(MaterialTheme.shapes.small).background(scheme.surfaceContainerHigh).padding(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Bar(scheme.primary, 0.45f)
        Bar(scheme.onSurface, 0.85f)
        Bar(scheme.onSurfaceVariant, 0.6f)
        Bar(scheme.onSurfaceVariant, 0.35f)
    }
}

@Composable
private fun Bar(color: Color, fraction: Float) {
    Box(Modifier.fillMaxWidth(fraction).height(5.dp).clip(CircleShape).background(color))
}

@Composable
private fun Dot(color: Color, size: androidx.compose.ui.unit.Dp) {
    Box(Modifier.size(size).clip(CircleShape).background(color))
}

@Composable
private fun MiniSwitch(scheme: ColorScheme, modifier: Modifier) {
    Box(modifier.size(width = 26.dp, height = 14.dp).clip(CircleShape).background(scheme.primary).padding(2.dp)) {
        Box(Modifier.align(Alignment.CenterEnd).size(10.dp).clip(CircleShape).background(scheme.onPrimary))
    }
}

/** Row of the active theme's key colors, shown in the Appearance hero. */
@Composable
fun PaletteStrip(scheme: ColorScheme, success: Color, warning: Color, modifier: Modifier = Modifier) {
    val colors = listOf(
        scheme.background, scheme.surfaceContainer, scheme.surfaceContainerHighest, scheme.primary,
        scheme.secondary, scheme.tertiary, scheme.error, success, warning,
    )
    Row(modifier.fillMaxWidth().height(22.dp).clip(CircleShape)) {
        colors.forEach { Box(Modifier.weight(1f).fillMaxHeight().background(it)) }
    }
}
