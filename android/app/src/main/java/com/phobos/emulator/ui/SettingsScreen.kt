package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Science
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Tv
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.LocalPhobosTheme

/**
 * Top-level Settings page. Each entry is a clickable row that opens its own
 * sub-menu (back-arrow screen), mirroring the Firmware (BIOS) / GPU Driver
 * pop-out pattern — so the main page stays clean instead of one long list.
 */
@Composable
fun SettingsScreen(
    viewModel: MainViewModel,
    onNavigateToAppearance: () -> Unit,
    onNavigateToEmulation: () -> Unit,
    onNavigateToVideo: () -> Unit,
    onNavigateToN64Experimental: () -> Unit,
    onNavigateToAudio: () -> Unit,
    onNavigateToPerformance: () -> Unit,
    onNavigateToInputs: () -> Unit,
    onNavigateToPaths: () -> Unit,
    onNavigateToVisibility: () -> Unit,
    onNavigateToAbout: () -> Unit
) {
    val themeName = LocalPhobosTheme.current.theme.name
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item { ScreenHeader("Settings") }
        item {
            SettingsCategory("Personalize") {
                SettingsClickableItem(title = "Appearance", description = "Theme: $themeName · colors, retrowave effects", onClick = onNavigateToAppearance, icon = Icons.Rounded.Palette)
            }
        }
        item {
            SettingsCategory("System") {
                SettingsClickableItem(title = "Emulation", description = "Region, firmware, GPU drivers, auto-save, CPU core", onClick = onNavigateToEmulation, icon = Icons.Rounded.Memory)
                SettingsClickableItem(title = "Video", description = "Color emulation, interframe blending, shaders", onClick = onNavigateToVideo, icon = Icons.Rounded.Tv)
                SettingsClickableItem(title = "N64 Experimental", description = "Rendering, overclocking, debug logging", onClick = onNavigateToN64Experimental, icon = Icons.Rounded.Science)
                SettingsClickableItem(title = "Audio", description = "Mute and audio options", onClick = onNavigateToAudio, icon = Icons.AutoMirrored.Rounded.VolumeUp)
                SettingsClickableItem(title = "Performance Monitor", description = "In-game HUD: FPS, frame times, CPU/GPU, battery", onClick = onNavigateToPerformance, icon = Icons.Rounded.Speed)
            }
        }
        item {
            SettingsCategory("Controls & Library") {
                SettingsClickableItem(title = "Inputs & Hotkeys", description = "Touch controls, controller mapping, hotkeys", onClick = onNavigateToInputs, icon = Icons.Rounded.SportsEsports)
                SettingsClickableItem(title = "Paths", description = "Firmware and save directories", onClick = onNavigateToPaths, icon = Icons.Rounded.Folder)
                SettingsClickableItem(title = "Platform Visibility", description = "Choose which systems to show in your library", onClick = onNavigateToVisibility, icon = Icons.Rounded.Visibility)
            }
        }
        item {
            SettingsCategory("About") {
                SettingsClickableItem(title = "About", description = "Version, licenses, and info", onClick = onNavigateToAbout, icon = Icons.Rounded.Info)
            }
        }
    }
}
