package com.phobos.emulator.ui

import android.view.KeyEvent
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import com.phobos.emulator.LogLevel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HotkeyMappingScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    var mappingTarget by remember { mutableStateOf<String?>(null) }
    val focusRequester = remember { FocusRequester() }
    
    // For capturing multi-key combos
    val currentCombo = remember { mutableStateListOf<Int>() }
    var lastKeyPressTime by remember { mutableLongStateOf(0L) }

    val hotkeys = listOf(
        "Fast Forward (Hold)" to "ff_hold",
        "Fast Forward (Toggle)" to "ff_toggle",
        "Save State" to "save",
        "Load State" to "load",
        "Next Slot" to "inc_slot",
        "Previous Slot" to "dec_slot",
        "Pause Emulation" to "pause",
        "Reset System" to "reset",
        "Reload Current Game" to "reload",
        "Quit Emulator" to "quit",
        "Capture Screenshot" to "screenshot",
        "Mute Audio" to "mute",
        "Frame Advance" to "frame_advance"
    )

    // Finalize combo after 1 second of no activity
    LaunchedEffect(lastKeyPressTime, mappingTarget) {
        if (mappingTarget != null && currentCombo.isNotEmpty()) {
            delay(1000)
            viewModel.setHotkey(mappingTarget!!, currentCombo.toList())
            currentCombo.clear()
            mappingTarget = null
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Hotkeys") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize()
                .focusRequester(focusRequester)
                .focusable()
                .onKeyEvent { keyEvent ->
                    if (mappingTarget != null && keyEvent.type == KeyEventType.KeyDown) {
                        val keyCode = keyEvent.nativeKeyEvent.keyCode
                        if (!currentCombo.contains(keyCode)) {
                            currentCombo.add(keyCode)
                        }
                        lastKeyPressTime = System.currentTimeMillis()
                        true
                    } else false
                }
        ) {
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                items(hotkeys) { (name, key) ->
                    val boundCombo = settings.hotkeys[key] ?: emptyList()
                    val boundKeyNames = boundCombo.joinToString(" + ") { 
                        KeyEvent.keyCodeToString(it).removePrefix("KEYCODE_")
                    }

                    ListItem(
                        headlineContent = { Text(name) },
                        supportingContent = { 
                            Text(if (boundKeyNames.isNotEmpty()) "Bound to: $boundKeyNames" else "Not bound") 
                        },
                        trailingContent = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                if (boundCombo.isNotEmpty()) {
                                    IconButton(onClick = { viewModel.setHotkey(key, emptyList()) }) {
                                        Icon(Icons.Default.Clear, contentDescription = "Clear")
                                    }
                                }
                                if (mappingTarget == key) {
                                    CircularProgressIndicator(modifier = Modifier.size(24.dp))
                                } else {
                                    Button(onClick = { 
                                        currentCombo.clear()
                                        mappingTarget = key 
                                        focusRequester.requestFocus()
                                    }) {
                                        Text("Bind")
                                    }
                                }
                            }
                        },
                        modifier = Modifier.pointerInput(Unit) {
                            detectTapGestures(
                                onTap = { 
                                    currentCombo.clear()
                                    mappingTarget = key 
                                    focusRequester.requestFocus()
                                },
                                onLongPress = { viewModel.setHotkey(key, emptyList()) }
                            )
                        }
                    )
                }
            }

            if (mappingTarget != null) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.5f))
                        .clickable { mappingTarget = null },
                    contentAlignment = Alignment.Center
                ) {
                    Card {
                        Column(modifier = Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("Capturing Combo for", style = MaterialTheme.typography.labelLarge)
                            Text(hotkeys.find { it.second == mappingTarget }?.first ?: "", style = MaterialTheme.typography.headlineMedium)
                            Spacer(modifier = Modifier.height(16.dp))
                            
                            val comboText = currentCombo.joinToString(" + ") { 
                                KeyEvent.keyCodeToString(it).removePrefix("KEYCODE_")
                            }
                            Text(if (comboText.isEmpty()) "Waiting for input..." else comboText, 
                                style = MaterialTheme.typography.titleLarge,
                                color = MaterialTheme.colorScheme.primary)
                            
                            Spacer(modifier = Modifier.height(8.dp))
                            Text("Hold all keys together then release", style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
}
