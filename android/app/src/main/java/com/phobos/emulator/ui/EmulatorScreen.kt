package com.phobos.emulator.ui

import android.app.Activity
import android.content.Context
import android.net.Uri
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.*
import androidx.compose.ui.input.pointer.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.phobos.emulator.LogLevel
import com.phobos.emulator.PerformanceStats
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.R
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.input.InputProcessor
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EmulatorScreen(viewModel: MainViewModel, systemName: String, romName: String, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val isPaused by viewModel.isPaused.collectAsState()
    val isLoaded by viewModel.isLoaded.collectAsState()
    val perfStats by viewModel.perfStats.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    
    val ps1AnalogMode by remember { derivedStateOf { settings.ps1AnalogMode } }
    
    var showQuitDialog by remember { mutableStateOf(false) }
    var showControls by remember { mutableStateOf(true) }
    
    val focusRequester = remember { FocusRequester() }
    val view = LocalView.current
    val window = (view.context as? Activity)?.window

    var hwButtons by remember { mutableStateOf(0) }
    var virtualButtons by remember { mutableStateOf(0) }
    val mergedButtons = remember(hwButtons, virtualButtons) { hwButtons or virtualButtons }
    
    var pressedKeys by remember { mutableStateOf(setOf<Int>()) }
    var ffToggled by remember { mutableStateOf(false) }
    var lx by remember { mutableStateOf(0f) }
    var ly by remember { mutableStateOf(0f) }
    var rx by remember { mutableStateOf(0f) }
    var ry by remember { mutableStateOf(0f) }
    var lastJoystickMotionTime by remember { mutableLongStateOf(0L) }
    
    val inputProcessor = remember(settings.inputMappings) { InputProcessor(settings.inputMappings) }

    // Sync input to native
    LaunchedEffect(mergedButtons, lx, ly, rx, ry, isPaused, ps1AnalogMode) {
        if (!isPaused) {
            val finalLx = if (ps1AnalogMode) lx else {
                if (Math.abs(lx) < 0.1f) {
                    if (mergedButtons and (1 shl 19) != 0) -1f else if (mergedButtons and (1 shl 20) != 0) 1f else lx
                } else lx
            }
            val finalLy = if (ps1AnalogMode) ly else {
                if (Math.abs(ly) < 0.1f) {
                    if (mergedButtons and (1 shl 17) != 0) -1f else if (mergedButtons and (1 shl 18) != 0) 1f else ly
                } else ly
            }
            val finalRx = if (ps1AnalogMode) rx else {
                if (Math.abs(rx) < 0.1f) {
                    if (mergedButtons and (1 shl 23) != 0) -1f else if (mergedButtons and (1 shl 24) != 0) 1f else rx
                } else rx
            }
            val finalRy = if (ps1AnalogMode) ry else {
                if (Math.abs(ry) < 0.1f) {
                    if (mergedButtons and (1 shl 21) != 0) -1f else if (mergedButtons and (1 shl 22) != 0) 1f else ry
                } else ry
            }

            if (Math.abs(finalLx) > 0.05f || Math.abs(finalLy) > 0.05f || Math.abs(finalRx) > 0.05f || Math.abs(finalRy) > 0.05f) {
                Log.i("PhobosInputTrace", "setInput: LS($finalLx, $finalLy) RS($finalRx, $finalRy) BTNS=${Integer.toHexString(mergedButtons)}")
            }

            PhobosCore.setInput(finalLx, finalLy, finalRx, finalRy, mergedButtons)
        }
    }

    BackHandler {
        viewModel.setPause(true)
        showQuitDialog = true
    }

    // Capture hardware input
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    // Full Screen Mode handling
    DisposableEffect(settings.fullScreenMode) {
        if (settings.fullScreenMode && window != null) {
            val controller = WindowCompat.getInsetsController(window, view)
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        
        onDispose {
            if (window != null) {
                val controller = WindowCompat.getInsetsController(window, view)
                controller.show(WindowInsetsCompat.Type.systemBars())
            }
        }
    }

    // Start native emulation thread when loaded
    LaunchedEffect(isLoaded) {
        if (isLoaded) {
            PhobosCore.setEmulationRunning(true)
        }
    }

    // Auto-hide controls after 3 seconds
    LaunchedEffect(showControls, isPaused) {
        if (showControls && !isPaused) {
            delay(3000)
            showControls = false
        }
    }

    // Cleanup when leaving screen
    DisposableEffect(Unit) {
        onDispose {
            PhobosCore.setEmulationRunning(false)
            viewModel.unloadSystem()
        }
    }

    if (showQuitDialog) {
        AlertDialog(
            onDismissRequest = { 
                showQuitDialog = false 
                viewModel.setPause(false)
            },
            title = { Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating $romName?") },
            confirmButton = {
                TextButton(onClick = { 
                    showQuitDialog = false
                    onBack() 
                }) {
                    Text("Quit")
                }
            },
            dismissButton = {
                TextButton(onClick = { 
                    showQuitDialog = false
                    viewModel.setPause(false)
                }) {
                    Text("Cancel")
                }
            }
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .focusRequester(focusRequester)
            .focusable()
            .onKeyEvent { keyEvent ->
                val keyCode = keyEvent.nativeKeyEvent.keyCode
                val isDown = keyEvent.type == KeyEventType.KeyDown
                
                pressedKeys = if (isDown) pressedKeys + keyCode else pressedKeys - keyCode

                // 1. Check Hotkey Combos
                var hotkeyTriggered = false
                
                settings.hotkeys.forEach { (action, combo) ->
                    if (combo.isNotEmpty() && combo.size == pressedKeys.size && combo.all { pressedKeys.contains(it) }) {
                        hotkeyTriggered = true
                        if (isDown) { // Trigger on press down
                            when (action) {
                                "pause" -> viewModel.togglePause()
                                "ff_hold" -> PhobosCore.setFastForward(true)
                                "ff_toggle" -> {
                                    ffToggled = !ffToggled
                                    PhobosCore.setFastForward(ffToggled)
                                }
                                "save" -> viewModel.saveState(systemName, romName, currentSlot)
                                "load" -> viewModel.loadState(systemName, romName, currentSlot)
                                "inc_slot" -> viewModel.incrementSlot()
                                "dec_slot" -> viewModel.decrementSlot()
                                "reset" -> viewModel.resetSystem()
                                "frame_advance" -> {
                                    if (isPaused) PhobosCore.frameAdvance()
                                }
                                "mute" -> viewModel.setMuteAudio(!settings.muteAudio)
                                "screenshot" -> viewModel.takeScreenshot(systemName, romName)
                                "reload" -> {
                                    viewModel.roms.value.find { it.name == romName }?.let { 
                                        viewModel.loadRom(view.context, systemName, it)
                                    }
                                }
                                "quit" -> {
                                    showQuitDialog = true
                                    viewModel.setPause(true)
                                }
                            }
                        }
                    } else if (action == "ff_hold" && !isDown && combo.contains(keyCode)) {
                        if (!ffToggled) PhobosCore.setFastForward(false)
                    }
                }

                if (hotkeyTriggered) return@onKeyEvent true

                // IGNORE KEY REPEAT: Android sends continuous KeyDown events on hold which causes rapid-fire.
                if (keyEvent.nativeKeyEvent.repeatCount > 0) {
                    return@onKeyEvent true
                }
                
                // 2. Check Input Mappings
                var bitmask = 0
                val isDpadKey = keyCode in 19..22
                val isJoystickSource = keyEvent.nativeKeyEvent.source and InputDevice.SOURCE_JOYSTICK != 0

                // Reject synthetic DPAD key events emitted by joystick analog sticks
                if (isDpadKey && isJoystickSource) {
                    val dpadMask = (1 shl 0) or (1 shl 1) or (1 shl 2) or (1 shl 3)
                    hwButtons = hwButtons and dpadMask.inv()
                    return@onKeyEvent true
                }

                settings.inputMappings.forEach { (bit, binding) ->
                    if (binding == "k:$keyCode") {
                        bitmask = bitmask or bit
                    }
                }

                if (bitmask != 0) {
                    hwButtons = if (isDown) {
                        hwButtons or bitmask
                    } else {
                        hwButtons and bitmask.inv()
                    }
                    true
                } else if (keyCode == KeyEvent.KEYCODE_BACK) {
                    // Handled by BackHandler
                    false
                } else {
                    // Log unmapped keys to help debugging
                    if (isDown && settings.logVerbosity.ordinal <= LogLevel.DEBUG.ordinal) {
                        Log.d("PhobosInput", "Unmapped KeyCode: $keyCode")
                    }
                    false
                }
            }
        ) {
            BoxWithConstraints(
                modifier = Modifier.fillMaxSize()
            ) {
                val ratio = 4f / 3f
                val maxWidthDp = this.maxWidth
                val maxHeightDp = this.maxHeight
                
                val viewWidth: Dp
                val viewHeight: Dp
                
                if (maxWidthDp / maxHeightDp > ratio) {
                    viewHeight = maxHeightDp
                    viewWidth = maxWidthDp * ratio
                } else {
                    viewWidth = maxWidthDp
                    viewHeight = maxWidthDp / ratio
                }

                Box(modifier = Modifier.size(viewWidth, viewHeight)) {
                    AndroidView(
                        factory = { ctx ->
                            SurfaceView(ctx).apply {
                                setZOrderMediaOverlay(true)
                                
                                // Handle Joysticks
                                setOnGenericMotionListener { _, event ->
                                    if (event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK &&
                                        event.action == MotionEvent.ACTION_MOVE) {
                                        
                                        val processedButtons = inputProcessor.processMotionEvent(event, hwButtons)
                                        hwButtons = processedButtons
                                        lx = inputProcessor.lx
                                        ly = inputProcessor.ly
                                        rx = inputProcessor.rx
                                        ry = inputProcessor.ry
                                        
                                        true
                                    } else false
                                }

                                holder.addCallback(object : SurfaceHolder.Callback {
                                    override fun surfaceCreated(holder: SurfaceHolder) {
                                        Log.d("Phobos", "Surface created: ${holder.surface}")
                                        PhobosCore.setSurface(holder.surface)
                                    }
                                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                                        Log.d("Phobos", "Surface changed: ${width}x${height}")
                                        PhobosCore.setSurface(holder.surface)
                                    }
                                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                                        Log.d("Phobos", "Surface destroyed")
                                        PhobosCore.setSurface(null)
                                    }
                                })
                            }
                        },
                        modifier = Modifier.size(viewWidth, viewHeight)
                    )
                }
            }

            if (!isLoaded && !isPaused) {
                Column(
                    modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.8f)),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        "Initializing $systemName...",
                        style = MaterialTheme.typography.bodyLarge,
                        color = Color.White
                    )
                }
            }

            // Virtual Gamepad
            if (isLoaded && !isPaused && settings.showTouchControls) {
                VirtualGamepad(
                    modifier = Modifier.fillMaxSize(),
                    settings = settings,
                    systemName = systemName,
                    onInputChanged = { buttons ->
                        virtualButtons = buttons
                    }
                )
            }

            // Performance Overlay
            if (isLoaded && !isPaused && settings.showPerformanceMonitor) {
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(top = 100.dp, end = 16.dp)
                        .background(Color.Black.copy(alpha = 0.4f), RoundedCornerShape(4.dp))
                        .padding(4.dp)
                ) {
                    Column {
                        Text(
                            "FPS: %.1f".format(perfStats.fps),
                            color = if (perfStats.fps > 55) Color.Green else Color.Yellow,
                            style = MaterialTheme.typography.labelSmall
                        )
                        Text(
                            "Frame: %.2fms".format(perfStats.frameTime),
                            color = Color.White,
                            style = MaterialTheme.typography.labelSmall
                        )
                        Text(
                            "Core: ${perfStats.activeCore}",
                            color = if (perfStats.activeCore >= 4) Color.Cyan else Color.LightGray,
                            style = MaterialTheme.typography.labelSmall
                        )
                    }
                }
            }

            // Overlay Controls
            if (showControls || isPaused || !isLoaded) {
                Box(modifier = Modifier.fillMaxSize()) {
                    // Top Bar
                    Surface(
                        color = Color.Black.copy(alpha = 0.6f),
                        modifier = Modifier.fillMaxWidth().align(Alignment.TopCenter)
                    ) {
                        Row(
                            modifier = Modifier.statusBarsPadding().padding(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            IconButton(onClick = { 
                                viewModel.setPause(true)
                                showQuitDialog = true 
                            }) {
                                Icon(Icons.Default.ArrowBack, contentDescription = "Back", tint = Color.White)
                            }
                            Text(
                                text = romName,
                                style = MaterialTheme.typography.titleMedium,
                                color = Color.White,
                                modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                                maxLines = 1
                            )
                            IconButton(onClick = { viewModel.togglePause() }) {
                                if (isPaused) {
                                    Icon(Icons.Default.PlayArrow, contentDescription = "Resume", tint = Color.White)
                                } else {
                                    Icon(painterResource(R.drawable.ic_pause), contentDescription = "Pause", tint = Color.White)
                                }
                            }
                        }
                    }
                    
                    if (isPaused) {
                        EmulationMenu(
                            viewModel = viewModel,
                            systemName = systemName,
                            romName = romName,
                            onResume = { viewModel.togglePause() },
                            onQuit = { showQuitDialog = true }
                        )
                    }
                }
            }
        }
    }
}
"