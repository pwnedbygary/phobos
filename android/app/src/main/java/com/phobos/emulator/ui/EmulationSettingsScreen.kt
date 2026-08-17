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
                    SettingsSwitchItem(
                        title = "Auto-Load Memory",
                        description = "Restore the auto-saved state when a game is loaded",
                        checked = settings.autoLoadMemory,
                        onCheckedChange = { viewModel.setAutoLoadMemory(it) }
                    )
                    FastForwardSpeedSelectorItem(settings.fastForwardSpeed) { viewModel.setFastForwardSpeed(it) }
                }
            }
        }
    }
}
