package com.phobos.emulator.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.phobos.emulator.input.ControlLevel

@Composable
fun InputMappingScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    var level by remember { mutableStateOf<ControlLevel>(ControlLevel.AllConsoles) }
    PhobosScaffold(title = "Controller Mapping", onBack = onBack) { innerPadding ->
        Box(Modifier.padding(innerPadding).fillMaxSize()) {
            ControllerButtonsList(viewModel, level, consoleNames = emptyMap(), inMenu = false, header = {
                SettingsLevelPicker(level, changed = { settings.controlOverrides.hasMappings(it) }, onLevel = { viewModel.controlCapture.cancel(); level = it })
            })
            CaptureOverlay(viewModel.controlCapture)
        }
    }
}
