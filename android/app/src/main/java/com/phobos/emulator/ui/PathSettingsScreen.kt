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
fun PathSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Global Paths") },
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
                SettingsCategory("Directory Paths") {
                    PathSelectorItem("Firmware Path", settings.firmwarePath) { viewModel.setFirmwarePath(it) }
                    PathSelectorItem("Saves Path", settings.savesPath) { viewModel.setSavesPath(it) }
                    PathSelectorItem("States Path", settings.statesPath) { viewModel.setStatesPath(it) }
                    PathSelectorItem("Screenshots Path", settings.screenshotsPath) { viewModel.setScreenshotsPath(it) }
                    PathSelectorItem("Vulkan Cache Path", settings.vulkanCachePath) { viewModel.setVulkanCachePath(it) }
                }
            }
            
            item {
                Text(
                    "Note: These paths define where common emulator files are stored. The internal system files (Home Path) are managed automatically for stability.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
            }
        }
    }
}
