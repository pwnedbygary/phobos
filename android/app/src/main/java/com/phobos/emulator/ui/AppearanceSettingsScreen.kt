package com.phobos.emulator.ui

import android.os.Build
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.ThemeMode
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.ThemeGroup
import com.phobos.emulator.ui.theme.ThemeRegistry

@Composable
fun AppearanceSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val shown = LocalPhobosTheme.current
    val dynamicAvailable = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
    // The System card previews the live scheme when it is the one showing (wallpaper colors).
    val liveScheme = MaterialTheme.colorScheme

    PhobosScaffold(title = "Appearance", onBack = onBack) { innerPadding ->
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minSize = 156.dp),
            modifier = Modifier.padding(innerPadding).fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            fullWidth {
                CurrentThemeCard(settings, viewModel, dynamicAvailable)
            }
            fullWidth {
                SettingsCategory("Effects") {
                    SettingsSwitchItem(
                        title = "Retrowave effects",
                        description = "Synthwave sunset backdrop, neon glow on cards and selections, and glowing bars. Works with any theme; best with Synthwave '84.",
                        checked = settings.retrowaveEffects,
                        onCheckedChange = { viewModel.setRetrowaveEffects(it) },
                    )
                }
            }
            ThemeGroup.entries.forEach { group ->
                val themes = ThemeRegistry.all.filter { it.group == group }
                fullWidth {
                    SectionHeader(
                        title = "${group.title} · ${themes.size}",
                        modifier = Modifier.padding(top = 12.dp),
                        trailing = if (group == ThemeGroup.RETROWAVE && !settings.retrowaveEffects) {
                            {
                                FilledTonalButton(onClick = { viewModel.setRetrowaveEffects(true) }) {
                                    Icon(Icons.Rounded.AutoAwesome, contentDescription = null, modifier = Modifier.size(18.dp))
                                    Spacer(Modifier.width(6.dp))
                                    Text("Turn on effects")
                                }
                            }
                        } else null,
                    )
                }
                items(themes, key = { it.id }) { theme ->
                    val scheme = if (theme.id == shown.theme.id && theme.dynamic && dynamicAvailable) liveScheme
                    else theme.colors(if (theme.adaptive) shown.isDark else theme.isDark).scheme
                    ThemeSwatchCard(
                        theme = theme,
                        scheme = scheme,
                        selected = theme.id == shown.theme.id,
                        onClick = { viewModel.setTheme(theme.id) },
                    )
                }
            }
        }
    }
}

private fun LazyGridScope.fullWidth(content: @Composable () -> Unit) {
    item(span = { GridItemSpan(maxLineSpan) }) { content() }
}

/** The active theme with its palette and the Light / Dark / Auto control where the theme has both. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CurrentThemeCard(settings: EmulatorSettings, viewModel: MainViewModel, dynamicAvailable: Boolean) {
    val info = LocalPhobosTheme.current
    val selected = ThemeRegistry.find(settings.themeId)
    val sibling = ThemeRegistry.sibling(selected)
    val subtitle = when {
        selected.dynamic && !dynamicAvailable -> "Material You needs Android 12; showing Phobos colors"
        selected.tagline != null -> selected.tagline
        info.theme.id != selected.id -> "Following the system: showing ${info.theme.name}"
        else -> if (info.isDark) "Dark theme" else "Light theme"
    }
    Column {
        SectionHeader("Theme")
        SettingsCard {
            Column(Modifier.padding(horizontal = 16.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Column {
                    Text(info.theme.name, style = MaterialTheme.typography.headlineSmall)
                    Text(subtitle, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                PaletteStrip(MaterialTheme.colorScheme, info.success, info.warning)
                when {
                    selected.adaptive -> ModeSelector(
                        selectedIndex = settings.themeMode.ordinalForSelector(),
                        labels = listOf("Light", "Dark", "Auto"),
                        onSelect = { viewModel.setThemeMode(listOf(ThemeMode.LIGHT, ThemeMode.DARK, ThemeMode.AUTO)[it]) },
                    )
                    sibling != null -> {
                        val light = if (selected.isDark) sibling else selected
                        val dark = if (selected.isDark) selected else sibling
                        ModeSelector(
                            selectedIndex = if (settings.themeFollowSystem) 2 else if (selected.isDark) 1 else 0,
                            labels = listOf(light.name, dark.name, "Auto"),
                            onSelect = { index ->
                                when (index) {
                                    0 -> viewModel.setTheme(light.id)
                                    1 -> viewModel.setTheme(dark.id)
                                    else -> viewModel.setTheme(selected.id, followSystem = true)
                                }
                            },
                        )
                    }
                    else -> Text(
                        "${selected.name} has a single ${if (selected.isDark) "dark" else "light"} variant.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

private fun ThemeMode.ordinalForSelector() = when (this) {
    ThemeMode.LIGHT -> 0
    ThemeMode.DARK -> 1
    ThemeMode.AUTO -> 2
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ModeSelector(selectedIndex: Int, labels: List<String>, onSelect: (Int) -> Unit) {
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        labels.forEachIndexed { index, label ->
            SegmentedButton(
                selected = index == selectedIndex,
                onClick = { onSelect(index) },
                shape = SegmentedButtonDefaults.itemShape(index, labels.size),
            ) {
                Text(label, maxLines = 1)
            }
        }
    }
}
