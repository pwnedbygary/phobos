package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/**
 * Performance Monitor configuration (Task 42). Lets the user toggle which
 * metrics the in-game overlay shows, plus the master show/hide switch.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PerformanceMonitorSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Performance Monitor") },
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
                SettingsCategory("Overlay") {
                    SettingsSwitchItem(
                        title = "Show Performance Monitor",
                        description = "Display the in-game FPS/system stats overlay",
                        checked = settings.showPerformanceMonitor,
                        onCheckedChange = { viewModel.setShowPerformanceMonitor(it) }
                    )
                }
            }
            item {
                SettingsCategory("Metrics") {
                    SettingsSwitchItem(
                        title = "FPS",
                        description = "Current frames per second",
                        checked = settings.perfShowFps,
                        onCheckedChange = { viewModel.setPerfShowFps(it) }
                    )
                    SettingsSwitchItem(
                        title = "Frame Time",
                        description = "Milliseconds per frame",
                        checked = settings.perfShowFrameTime,
                        onCheckedChange = { viewModel.setPerfShowFrameTime(it) }
                    )
                    SettingsSwitchItem(
                        title = "RAM",
                        description = "JVM heap usage",
                        checked = settings.perfShowRam,
                        onCheckedChange = { viewModel.setPerfShowRam(it) }
                    )
                    SettingsSwitchItem(
                        title = "CPU Core Affinity",
                        description = "Active core index",
                        checked = settings.perfShowCore,
                        onCheckedChange = { viewModel.setPerfShowCore(it) }
                    )
                    SettingsSwitchItem(
                        title = "Shader Failures",
                        description = "Warn when the GPU driver fails to compile shaders",
                        checked = settings.perfShowShaderFails,
                        onCheckedChange = { viewModel.setPerfShowShaderFails(it) }
                    )
                }
            }
        }
    }
}
