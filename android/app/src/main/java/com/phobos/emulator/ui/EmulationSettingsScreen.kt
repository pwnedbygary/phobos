package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EmulationSettingsScreen(
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onNavigateToFirmware: () -> Unit,
    onNavigateToDrivers: () -> Unit
) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Emulation") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                SettingsCategory("General") {
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
                    // Fast Boot moved to the pause menu (per-core Boot Options) —
                    // ares only supports it on GB/GBC, NGP/NGPC, PS1, so the
                    // global toggle was a silent no-op on other cores.
                    SettingsSwitchItem(
                        title = "Run-Ahead",
                        description = "Removes one frame of input lag",
                        checked = settings.runAhead,
                        onCheckedChange = { viewModel.setRunAhead(it) }
                    )
                    SettingsSwitchItem(
                        title = "Auto-Save State",
                        description = "Save a state snapshot automatically when you quit a game",
                        checked = settings.autoSaveState,
                        onCheckedChange = { viewModel.setAutoSaveState(it) }
                    )
                    SettingsSwitchItem(
                        title = "Auto-Load State",
                        description = "Restore the auto-saved state when a game is loaded",
                        checked = settings.autoLoadState,
                        onCheckedChange = { viewModel.setAutoLoadState(it) }
                    )
                    FastForwardSpeedSelectorItem(settings.fastForwardSpeed) { viewModel.setFastForwardSpeed(it) }
                }
            }
        }
    }
}
