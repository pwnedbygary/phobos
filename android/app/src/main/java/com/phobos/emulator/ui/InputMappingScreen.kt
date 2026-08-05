package com.phobos.emulator.ui

import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
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
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import com.phobos.emulator.LogLevel
import com.phobos.emulator.PhobosCore

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InputMappingScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    var mappingTarget by remember { mutableStateOf<Int?>(null) }
    val focusRequester = remember { FocusRequester() }
    val view = LocalView.current
    var lastInputTime by remember { mutableLongStateOf(0L) }

    val aresButtons = listOf(
        "Up" to PhobosCore.Input.UP,
        "Down" to PhobosCore.Input.DOWN,
        "Left" to PhobosCore.Input.LEFT,
        "Right" to PhobosCore.Input.RIGHT,
        "A" to PhobosCore.Input.A,
        "B" to PhobosCore.Input.B,
        "X" to PhobosCore.Input.X,
        "Y" to PhobosCore.Input.Y,
        "L1" to PhobosCore.Input.L1,
        "R1" to PhobosCore.Input.R1,
        "L2" to PhobosCore.Input.L2,
        "R2" to PhobosCore.Input.R2,
        "L3" to PhobosCore.Input.L3,
        "R3" to PhobosCore.Input.R3,
        "Select" to PhobosCore.Input.SELECT,
        "Start" to PhobosCore.Input.START,
        "Home" to PhobosCore.Input.HOME,
        "L-Up" to PhobosCore.Input.LS_UP,
        "L-Down" to PhobosCore.Input.LS_DOWN,
        "L-Left" to PhobosCore.Input.LS_LEFT,
        "L-Right" to PhobosCore.Input.LS_RIGHT,
        "R-Up" to PhobosCore.Input.RS_UP,
        "R-Down" to PhobosCore.Input.RS_DOWN,
        "R-Left" to PhobosCore.Input.RS_LEFT,
        "R-Right" to PhobosCore.Input.RS_RIGHT
    )

    // Capture Axis and Button Inputs
    DisposableEffect(mappingTarget) {
        if (mappingTarget == null) return@DisposableEffect onDispose {}
        
        val listener = View.OnGenericMotionListener { _, event ->
            if (event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK &&
                event.action == MotionEvent.ACTION_MOVE) {
                
                for (i in 0 until 64) {
                    val value = event.getAxisValue(i)
                    if (Math.abs(value) > 0.4f) { 
                        if (mappingTarget != null) {
                            val isDpadTarget = mappingTarget!! in (1 shl 0)..(1 shl 3)
                            val isMainStickAxis = i == MotionEvent.AXIS_X || i == MotionEvent.AXIS_Y ||
                                                  i == MotionEvent.AXIS_Z || i == MotionEvent.AXIS_RZ
                            val isHatAxis = i == 15 || i == 16

                            // PRIORITY GUARD:
                            // If we are mapping D-Pad (Up, Down, Left, Right), 
                            // ignore main stick axes (0, 1, 11, 14) and Hat axes (15, 16)
                            // so D-Pad is mapped only to key presses or actual hat buttons.
                            if (isDpadTarget && (isMainStickAxis || isHatAxis)) {
                                if (settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                                    Log.d("PhobosInputDebug", "IGNORE AXIS for D-Pad target: Axis=$i")
                                }
                                continue
                            }

                            if (settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                                Log.d("PhobosInputDebug", "POLL AXIS: Axis=$i, Value=$value, Target=$mappingTarget")
                            }
                            val isPos = if (value > 0) 1 else 0
                            viewModel.updateInputMapping(mappingTarget!!, "a:$i:$isPos")
                            mappingTarget = null
                            lastInputTime = System.currentTimeMillis()
                            return@OnGenericMotionListener true
                        }
                    }
                }
            }
            false
        }
        
        view.setOnGenericMotionListener(listener)
        onDispose {
            view.setOnGenericMotionListener(null)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Controller Mapping") },
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
                        val now = System.currentTimeMillis()

                        if (settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                            Log.d("PhobosInputDebug", "POLL KEY: Key=$keyCode, Source=${keyEvent.nativeKeyEvent.source}, Target=$mappingTarget")
                        }

                        // SMART PRIORITY:
                        // 1. If we're mapping D-Pad (Up, Down, Left, Right), we allow keys immediately.
                        // 2. If we're mapping Sticks, we ignore keys for a while to let Axis detection win.
                        val isDpadTarget = mappingTarget!! <= (1 shl 3)
                        
                        // COOLDOWN: If we just detected an axis, ignore keys for 1 second for analog stick mapping.
                        // This prevents stick-generated key events from overwriting analog axis bindings.
                        if (!isDpadTarget && now - lastInputTime < 1000) {
                            if (settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                                Log.d("PhobosInputDebug", "REJECT KEY: Cooldown active (Axis priority)")
                            }
                            return@onKeyEvent false
                        }

                        // Also ignore D-Pad keys when binding sticks (controllers often send both)
                        val isStickTarget = mappingTarget!! >= (1 shl 17)
                        val isDpadKey = keyCode in 19..22
                        if (isStickTarget && isDpadKey) {
                            if (settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                                Log.d("PhobosInputDebug", "REJECT KEY: Stick target + D-Pad key event")
                            }
                            return@onKeyEvent false
                        }

                        viewModel.updateInputMapping(mappingTarget!!, "k:$keyCode")
                        mappingTarget = null
                        true
                    } else false
                }
        ) {
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                item {
                    Text(
                        "Tap 'Bind' then press a key or move a stick. Long-press a row to clear it.",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(16.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }

                items(aresButtons) { (name, bit) ->
                    val binding = settings.inputMappings[bit]
                    
                    ListItem(
                        headlineContent = { Text(name) },
                        supportingContent = { 
                            if (binding != null) {
                                if (binding.startsWith("a:")) {
                                    val parts = binding.split(":")
                                    val axis = parts[1].toInt()
                                    val pos = parts[2] == "1"
                                    val axisName = when(axis) {
                                        MotionEvent.AXIS_X -> "L-Stick X"
                                        MotionEvent.AXIS_Y -> "L-Stick Y"
                                        MotionEvent.AXIS_Z -> "R-Stick X"
                                        MotionEvent.AXIS_RZ -> "R-Stick Y"
                                        MotionEvent.AXIS_HAT_X -> "D-Pad X (Axis 15)"
                                        MotionEvent.AXIS_HAT_Y -> "D-Pad Y (Axis 16)"
                                        else -> "Axis $axis"
                                    }
                                    Text("Bound to $axisName (${if (pos) "+" else "-"})")
                                } else if (binding.startsWith("k:")) {
                                    val keyCode = binding.removePrefix("k:").toInt()
                                    Text("Bound to Key: $keyCode (${KeyEvent.keyCodeToString(keyCode)})")
                                }
                            } else {
                                Text("Not bound")
                            }
                        },
                        trailingContent = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                if (binding != null) {
                                    IconButton(onClick = { viewModel.clearInputMapping(bit) }) {
                                        Icon(Icons.Default.Clear, contentDescription = "Clear")
                                    }
                                }
                                if (mappingTarget == bit) {
                                    CircularProgressIndicator(modifier = Modifier.size(24.dp))
                                } else {
                                    Button(onClick = { 
                                        mappingTarget = bit 
                                        focusRequester.requestFocus()
                                    }) {
                                        Text("Bind")
                                    }
                                }
                            }
                        },
                        modifier = Modifier.pointerInput(Unit) {
                            detectTapGestures(
                                onTap = { mappingTarget = bit; focusRequester.requestFocus() },
                                onLongPress = { viewModel.clearInputMapping(bit) }
                            )
                        }
                    )
                }
                
                item {
                    Spacer(modifier = Modifier.height(16.dp))
                    Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                        Button(
                            onClick = { viewModel.clearAllMappings() },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.errorContainer, contentColor = MaterialTheme.colorScheme.onErrorContainer)
                        ) {
                            Text("Clear All")
                        }
                        Spacer(modifier = Modifier.width(16.dp))
                        Button(
                            onClick = { viewModel.resetDefaultMapping() },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.secondaryContainer, contentColor = MaterialTheme.colorScheme.onSecondaryContainer)
                        ) {
                            Text("Reset Defaults")
                        }
                    }
                    Spacer(modifier = Modifier.height(32.dp))
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
                            Text("Mapping input for", style = MaterialTheme.typography.labelLarge)
                            val targetName = aresButtons.find { it.second == mappingTarget }?.first
                            Text(targetName ?: "", style = MaterialTheme.typography.headlineMedium)
                            Spacer(modifier = Modifier.height(16.dp))
                            Text("Press button or move stick...", style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
}
