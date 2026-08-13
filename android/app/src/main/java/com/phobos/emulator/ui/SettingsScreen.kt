package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

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
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        item { SettingsClickableItem(title = "Appearance", description = "Theme and visual styling", onClick = onNavigateToAppearance) }
        item { SettingsClickableItem(title = "Emulation", description = "Region, fast boot, run-ahead, auto-save", onClick = onNavigateToEmulation) }
        item { SettingsClickableItem(title = "Video", description = "Color emulation, interframe blending, shaders", onClick = onNavigateToVideo) }
        item { SettingsClickableItem(title = "N64 Experimental", description = "Rendering, overclocking, debug logging", onClick = onNavigateToN64Experimental) }
        item { SettingsClickableItem(title = "Audio", description = "Mute and audio options", onClick = onNavigateToAudio) }
        item { SettingsClickableItem(title = "Performance Monitor", description = "In-game FPS overlay and metrics", onClick = onNavigateToPerformance) }
        item { SettingsClickableItem(title = "Inputs & Hotkeys", description = "Touch controls, controller mapping, hotkeys", onClick = onNavigateToInputs) }
        item { SettingsClickableItem(title = "Paths", description = "Firmware and save directories", onClick = onNavigateToPaths) }
        item { SettingsClickableItem(title = "Platform Visibility", description = "Choose which systems to show in your library", onClick = onNavigateToVisibility) }
        item { SettingsClickableItem(title = "About", description = "Version, licenses, and info", onClick = onNavigateToAbout) }
    }
}
