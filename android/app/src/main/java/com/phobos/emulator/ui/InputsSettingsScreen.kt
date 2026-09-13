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
fun InputsSettingsScreen(
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onNavigateToInputs: () -> Unit,
    onNavigateToHotkeys: () -> Unit
) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Inputs & Hotkeys") },
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
                SettingsCategory("Inputs") {
                    SettingsSwitchItem(
                        title = "Show Touch Controls",
                        description = "Display virtual on-screen buttons",
                        checked = settings.showTouchControls,
                        onCheckedChange = { viewModel.setShowTouchControls(it) }
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
        }
    }
}
