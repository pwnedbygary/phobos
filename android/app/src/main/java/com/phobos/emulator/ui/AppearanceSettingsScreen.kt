package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.CatalogBackdrop
import com.phobos.emulator.data.CrtBackdropScene
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.GlassBackdropScene
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.MangaBackdropScene
import com.phobos.emulator.data.PixelBackdropScene
import com.phobos.emulator.data.PlatformIconPack
import com.phobos.emulator.data.RetrowaveBackdropScene
import com.phobos.emulator.data.RpgBackdropScene
import com.phobos.emulator.data.ScreensaverDelay
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.data.XmbBackdropScene
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.ThemeGroup
import com.phobos.emulator.ui.theme.ThemeRegistry
import com.phobos.emulator.ui.theme.SolidUi
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.ui.theme.style

@Composable
fun AppearanceSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val shown = LocalPhobosTheme.current

    PhobosScaffold(title = "Appearance", onBack = onBack) { innerPadding ->
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minSize = 156.dp),
            modifier = Modifier.padding(innerPadding).fillMaxSize(),
            contentPadding = pageContentPadding(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            fullWidth {
                CurrentThemeCard()
            }
            fullWidth {
                SettingsCategory("Effects") {
                    SettingsDropdownItem(
                        title = "Style effects",
                        description = settings.uiEffects.description,
                        current = settings.uiEffects,
                        options = UiEffects.entries,
                        label = { it.label },
                        onSelect = { viewModel.setUiEffects(it) },
                    )
                    BackdropDropdown(settings, viewModel)
                    SettingsDropdownItem(
                        title = "Platform icons",
                        description = settings.platformIconPack.description,
                        current = settings.platformIconPack,
                        options = PlatformIconPack.entries,
                        label = { it.label },
                        onSelect = { viewModel.setPlatformIconPack(it) },
                    )
                    SettingsSwitchItem(
                        title = "Intro",
                        description = "Mars and its moon as Phobos opens. Any button or a tap skips it.",
                        checked = settings.introAnimation,
                        onCheckedChange = { viewModel.setIntroAnimation(it) },
                    )
                    SettingsDropdownItem(
                        title = "Screensaver",
                        description = settings.screensaverDelay.description,
                        current = settings.screensaverDelay,
                        options = ScreensaverDelay.entries,
                        label = { it.label },
                        onSelect = { viewModel.setScreensaverDelay(it) },
                    )
                    SettingsDropdownItem(
                        title = "Screensaver backdrop",
                        description = settings.screensaverBackdrop.description,
                        current = settings.screensaverBackdrop,
                        options = CatalogBackdrop.screensaverChoices,
                        label = { it.menuLabel },
                        onSelect = { viewModel.setScreensaverBackdrop(it) },
                    )
                    SettingsClickableItem(
                        title = "Preview screensaver",
                        description = "Shows it now. Any button or a tap ends it.",
                        onClick = { viewModel.showScreensaver() },
                        trailingIcon = Icons.Rounded.PlayArrow,
                    )
                    val solidStyle = settings.uiEffects.style is SolidUi
                    SettingsDropdownItem(
                        title = "Glass effects",
                        description = if (solidStyle) "${settings.uiEffects.label} uses solid panels." else settings.glassEffects.description,
                        current = settings.glassEffects,
                        options = GlassEffects.entries,
                        label = { it.label },
                        onSelect = { viewModel.setGlassEffects(it) },
                        enabled = !solidStyle,
                    )
                }
            }
            ThemeGroup.entries.forEach { group ->
                val themes = ThemeRegistry.all.filter { it.group == group }
                fullWidth {
                    SectionHeader(
                        title = "${group.title} · ${themes.size}",
                        modifier = Modifier.padding(top = 12.dp),
                        trailing = if (group == ThemeGroup.RETROWAVE && settings.uiEffects != UiEffects.RETROWAVE) {
                            {
                                FilledTonalButton(onClick = { viewModel.setUiEffects(UiEffects.RETROWAVE) }, shape = pillShape()) {
                                    Icon(Icons.Rounded.AutoAwesome, contentDescription = null, modifier = Modifier.size(18.dp))
                                    Spacer(Modifier.width(6.dp))
                                    Text(if (settings.uiEffects == UiEffects.NONE) "Turn on effects" else "Switch to Retrowave")
                                }
                            }
                        } else null,
                    )
                }
                items(themes, key = { it.id }) { theme ->
                    ThemeSwatchCard(
                        theme = theme,
                        scheme = theme.colors.scheme,
                        selected = theme.id == shown.theme.id,
                        onClick = { viewModel.setTheme(theme.id) },
                    )
                }
            }
        }
    }
}

@Composable
private fun BackdropDropdown(settings: EmulatorSettings, viewModel: MainViewModel) {
    when (settings.uiEffects) {
        UiEffects.PIXEL_ART -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.pixelBackdrop.description,
            current = settings.pixelBackdrop,
            options = PixelBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setPixelBackdrop(it) },
        )
        UiEffects.MANGA -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.mangaBackdrop.description,
            current = settings.mangaBackdrop,
            options = MangaBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setMangaBackdrop(it) },
        )
        UiEffects.RPG -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.rpgBackdrop.description,
            current = settings.rpgBackdrop,
            options = RpgBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setRpgBackdrop(it) },
        )
        UiEffects.RETROWAVE -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.retrowaveBackdrop.description,
            current = settings.retrowaveBackdrop,
            options = RetrowaveBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setRetrowaveBackdrop(it) },
        )
        UiEffects.CRT -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.crtBackdrop.description,
            current = settings.crtBackdrop,
            options = CrtBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setCrtBackdrop(it) },
        )
        UiEffects.XMB -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.xmbBackdrop.description,
            current = settings.xmbBackdrop,
            options = XmbBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setXmbBackdrop(it) },
        )
        UiEffects.NONE -> SettingsDropdownItem(
            title = "Backdrop",
            description = settings.glassBackdrop.description,
            current = settings.glassBackdrop,
            options = GlassBackdropScene.entries,
            label = { it.label },
            onSelect = { viewModel.setGlassBackdrop(it) },
        )
    }
}

private fun LazyGridScope.fullWidth(content: @Composable () -> Unit) {
    item(span = { GridItemSpan(maxLineSpan) }) { content() }
}

/** The active theme with its palette. */
@Composable
private fun CurrentThemeCard() {
    val info = LocalPhobosTheme.current
    val subtitle = info.theme.tagline ?: if (info.isDark) "Dark theme" else "Light theme"
    Column {
        SectionHeader("Theme")
        SettingsCard {
            Column(Modifier.padding(horizontal = 16.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Column {
                    Text(info.theme.name, style = MaterialTheme.typography.headlineSmall)
                    Text(subtitle, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                PaletteStrip(MaterialTheme.colorScheme, info.success, info.warning)
            }
        }
    }
}
