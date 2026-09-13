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
fun VideoSettingsScreen(
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onNavigateToShaders: () -> Unit
) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Video") },
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
        }
    }
}
