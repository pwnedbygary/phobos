package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.phobos.emulator.LogLevel
import com.phobos.emulator.R
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.data.ThemeMode

@Composable
fun SettingsScreen(
    viewModel: MainViewModel,
    onNavigateToVisibility: () -> Unit,
    onNavigateToFirmware: () -> Unit,
    onNavigateToInputs: () -> Unit,
    onNavigateToHotkeys: () -> Unit,
    onNavigateToShaders: () -> Unit,
    onNavigateToPaths: () -> Unit,
    onNavigateToDrivers: () -> Unit,
    onNavigateToAbout: () -> Unit
) {
    val settings by viewModel.settings.collectAsState()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            SettingsCategory("Appearance") {
                ThemeSelectorItem(settings.themeMode) { viewModel.setThemeMode(it) }
            }
        }

        item {
            SettingsCategory("Logging") {
                LogVerbositySelectorItem(settings.logVerbosity) { viewModel.setLogVerbosity(it) }
            }
        }

        item {
            SettingsCategory("Emulation") {
                RegionSelectorItem(settings.regionPreference) { viewModel.setRegionPreference(it) }
                SettingsClickableItem(
                    title = "Firmware (BIOS)",
                    description = "Manage BIOS and system firmware",
                    onClick = onNavigateToFirmware
                )
                SettingsClickableItem(
                    title = "GPU Driver Manager",
                    description = "Install and select custom Adreno drivers",
                    onClick = onNavigateToDrivers
                )
                SettingsSwitchItem(
                    title = "Fast Boot",
                    description = "Skip BIOS and intro animations",
                    checked = settings.fastBoot,
                    onCheckedChange = { viewModel.setFastBoot(it) }
                )
                SettingsSwitchItem(
                    title = "Run-Ahead",
                    description = "Removes one frame of input lag",
                    checked = settings.runAhead,
                    onCheckedChange = { viewModel.setRunAhead(it) }
                )
                SettingsSwitchItem(
                    title = "Auto-Save Memory",
                    description = "Safeguard game saves from being lost",
                    checked = settings.autoSaveMemory,
                    onCheckedChange = { viewModel.setAutoSaveMemory(it) }
                )
                FastForwardSpeedSelectorItem(settings.fastForwardSpeed) { viewModel.setFastForwardSpeed(it) }
            }
        }

        item {
            SettingsCategory("Video") {
                SettingsSwitchItem(
                    title = "Color Emulation",
                    description = "Matches colors to how they look on real hardware",
                    checked = settings.colorEmulation,
                    onCheckedChange = { viewModel.setColorEmulation(it) }
                )
                SettingsSwitchItem(
                    title = "Interframe Blending",
                    description = "Emulates LCD translucency effects",
                    checked = settings.interframeBlending,
                    onCheckedChange = { viewModel.setInterframeBlending(it) }
                )
                SettingsSwitchItem(
                    title = "Overscan",
                    description = "Displays the full frame without cropping borders",
                    checked = settings.overscan,
                    onCheckedChange = { viewModel.setOverscan(it) }
                )
                SettingsSwitchItem(
                    title = "Full Screen Mode",
                    description = "Hides system bars during emulation",
                    checked = settings.fullScreenMode,
                    onCheckedChange = { viewModel.setFullScreenMode(it) }
                )
                SettingsClickableItem(
                    title = "Shaders",
                    description = "Apply Slang shader presets (CRT, LCD, etc)",
                    onClick = onNavigateToShaders
                )
            }
        }

        item {
            SettingsCategory("Experimental (N64 Vulkan)") {
                var upscaleExpanded by remember { mutableStateOf(false) }
                val currentUpscaleText = when(settings.n64Upscale) {
                    2 -> "2x HD (480p/960i)"
                    4 -> "4x UHD (4K)"
                    else -> "1x Native (SD)"
                }
                ListItem(
                    headlineContent = { Text("N64 Resolution / Upscaling") },
                    supportingContent = { Text("Parallel-RDP internal rendering resolution") },
                    trailingContent = {
                        Box {
                            TextButton(onClick = { upscaleExpanded = true }) { Text(currentUpscaleText) }
                            DropdownMenu(expanded = upscaleExpanded, onDismissRequest = { upscaleExpanded = false }) {
                                DropdownMenuItem(text = { Text("1x Native (SD)") }, onClick = { viewModel.setN64Upscale(1); upscaleExpanded = false })
                                DropdownMenuItem(text = { Text("2x HD (480p/960i)") }, onClick = { viewModel.setN64Upscale(2); upscaleExpanded = false })
                                DropdownMenuItem(text = { Text("4x UHD (4K)") }, onClick = { viewModel.setN64Upscale(4); upscaleExpanded = false })
                            }
                        }
                    }
                )
                SettingsSwitchItem(
                    title = "Disable VI Processing",
                    description = "Bypass VI post-processing. May fix shader compile errors on some GPUs.",
                    checked = settings.n64DisableVIProcessing,
                    onCheckedChange = { viewModel.setN64DisableVIProcessing(it) }
                )
                SettingsSwitchItem(
                    title = "Supersample Scanout",
                    description = "Downscale output from internal upscale for native-res display. Changes deinterlacing pipeline.",
                    checked = settings.n64SupersampleScanout,
                    onCheckedChange = { viewModel.setN64SupersampleScanout(it) }
                )
                SettingsSwitchItem(
                    title = "Weave Deinterlacing",
                    description = "Blend previous frame instead of upscale deinterlacing. Requires Supersample Scanout OFF.",
                    checked = settings.n64WeaveDeinterlacing,
                    onCheckedChange = { viewModel.setN64WeaveDeinterlacing(it) }
                )
            }
        }

        item {
            SettingsCategory("Audio") {
                SettingsSwitchItem(
                    title = "Mute Audio",
                    description = "Silence all emulator output",
                    checked = settings.muteAudio,
                    onCheckedChange = { viewModel.setMuteAudio(it) }
                )
            }
        }

        item {
            SettingsCategory("Inputs & Hotkeys") {
                SettingsSwitchItem(
                    title = "Show Touch Controls",
                    description = "Display virtual on-screen buttons",
                    checked = settings.showTouchControls,
                    onCheckedChange = { viewModel.setShowTouchControls(it) }
                )
                SettingsSwitchItem(
                    title = "Performance Monitor",
                    description = "Show real-time FPS and system stats",
                    checked = settings.showPerformanceMonitor,
                    onCheckedChange = { viewModel.setShowPerformanceMonitor(it) }
                )
                SettingsClickableItem(
                    title = "Controller Mapping",
                    description = "Configure physical or virtual controllers",
                    onClick = onNavigateToInputs
                )
                SettingsClickableItem(
                    title = "Hotkeys",
                    description = "Map emulator functions to buttons",
                    onClick = onNavigateToHotkeys
                )
            }
        }

        item {
            SettingsCategory("Paths") {
                SettingsClickableItem(
                    title = "File Paths",
                    description = "Configure firmware and save directories",
                    onClick = onNavigateToPaths
                )
            }
        }

        item {
            SettingsCategory("Platform Visibility") {
                SettingsClickableItem(
                    title = "Visible Systems",
                    description = "Choose which systems to show in your library",
                    onClick = onNavigateToVisibility
                )
            }
        }

        item {
            SettingsCategory("About") {
                SettingsClickableItem(
                    title = "About Phobos",
                    description = "Version, licenses, and info",
                    onClick = onNavigateToAbout
                )
            }
        }
    }
}

