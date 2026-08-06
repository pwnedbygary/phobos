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
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
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
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EmulatorScreen(viewModel: MainViewModel, systemName: String, romName: String, onBack: () -> Unit) {
    val settingsState by viewModel.settings.collectAsState()
    val isPaused by viewModel.isPaused.collectAsState()
    val isLoaded by viewModel.isLoaded.collectAsState()
    val perfStats by viewModel.perfStats.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    
    var showQuitDialog by remember { mutableStateOf(false) }
    var showControls by remember { mutableStateOf(true) }
    
    val focusRequester = remember { FocusRequester() }
    val view = LocalView.current
    val window = (view.context as? Activity)?.window

    // State Ref to prevent stale closures in listeners
    val stateRef = remember { object {
        var hwButtons = 0
        var virtualButtons = 0
        var lx = 0f
        var ly = 0f
        var rx = 0f
        var ry = 0f
        var logCounter = 0
        var settings = settingsState

        fun push(force: Boolean = false) {
            var fLx = lx; var fLy = ly; var fRx = rx; var fRy = ry
            val btn = hwButtons or virtualButtons
            if (Math.abs(fLx) < 0.01f) {
                if (btn and (1 shl 19) != 0) fLx = -1.0f else if (btn and (1 shl 20) != 0) fLx = 1.0f
            }
            if (Math.abs(fLy) < 0.01f) {
                if (btn and (1 shl 17) != 0) fLy = -1.0f else if (btn and (1 shl 18) != 0) fLy = 1.0f
            }
            var fBtn = btn
            if (Math.abs(lx) > 0.15f || Math.abs(ly) > 0.15f || Math.abs(rx) > 0.15f || Math.abs(ry) > 0.15f) {
                fBtn = fBtn and ((1 shl 0) or (1 shl 1) or (1 shl 2) or (1 shl 3)).inv()
            }
            if (force || logCounter++ % 120 == 0) {
                if (Math.abs(fLx) > 0.05f || Math.abs(fLy) > 0.05f || btn != 0) {
                    Log.i("PhobosInput", "JNI Push: LS(%.2f, %.2f) BTNS=%08x".format(fLx, fLy, fBtn))
                }
            }
            PhobosCore.setInput(fLx, fLy, fRx, fRy, fBtn)
        }
    } }
    stateRef.settings = settingsState

    var pressedKeys by remember { mutableStateOf(setOf<Int>()) }
    var ffToggled by remember { mutableStateOf(false) }

    LaunchedEffect(isPaused) { stateRef.push(true) }
    BackHandler { viewModel.setPause(true); showQuitDialog = true }
    LaunchedEffect(Unit) { focusRequester.requestFocus() }

    DisposableEffect(settingsState.fullScreenMode) {
        if (settingsState.fullScreenMode && window != null) {
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

    LaunchedEffect(isLoaded) {
        if (isLoaded) {
            viewModel.setPause(false)
            PhobosCore.setEmulationRunning(true)
        }
    }
    LaunchedEffect(showControls, isPaused) { if (showControls && !isPaused) { delay(3000); showControls = false } }
    DisposableEffect(Unit) { onDispose { PhobosCore.setEmulationRunning(false); viewModel.unloadSystem() } }

    if (showQuitDialog) {
        AlertDialog(
            onDismissRequest = { showQuitDialog = false; viewModel.setPause(false) },
            title = { Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating $romName?") },
            confirmButton = { TextButton(onClick = { showQuitDialog = false; onBack() }) { Text("Quit") } },
            dismissButton = { TextButton(onClick = { showQuitDialog = false; viewModel.setPause(false) }) { Text("Cancel") } }
        )
    }

    Box(
        modifier = Modifier.fillMaxSize().background(Color.Black).focusRequester(focusRequester).focusable()
            .onKeyEvent { keyEvent ->
                val keyCode = keyEvent.nativeKeyEvent.keyCode
                val isDown = keyEvent.type == KeyEventType.KeyDown
                if (keyEvent.nativeKeyEvent.repeatCount > 0) return@onKeyEvent true
                pressedKeys = if (isDown) pressedKeys + keyCode else pressedKeys - keyCode
                var hotkeyTriggered = false
                stateRef.settings.hotkeys.forEach { (action, combo) ->
                    if (combo.isNotEmpty() && combo.size == pressedKeys.size && combo.all { pressedKeys.contains(it) }) {
                        hotkeyTriggered = true
                        if (isDown) {
                            when (action) {
                                "pause" -> viewModel.togglePause()
                                "ff_hold" -> PhobosCore.setFastForward(true)
                                "ff_toggle" -> { ffToggled = !ffToggled; PhobosCore.setFastForward(ffToggled) }
                                "save" -> viewModel.saveState(systemName, romName, currentSlot)
                                "load" -> viewModel.loadState(systemName, romName, currentSlot)
                                "inc_slot" -> viewModel.incrementSlot()
                                "dec_slot" -> viewModel.decrementSlot()
                                "reset" -> viewModel.resetSystem()
                                "frame_advance" -> if (isPaused) PhobosCore.frameAdvance()
                                "mute" -> viewModel.setMuteAudio(!settingsState.muteAudio)
                                "screenshot" -> viewModel.takeScreenshot(systemName, romName)
                                "reload" -> viewModel.roms.value.find { it.name == romName }?.let { viewModel.loadRom(view.context, systemName, it) }
                                "quit" -> { showQuitDialog = true; viewModel.setPause(true) }
                            }
                        }
                    } else if (action == "ff_hold" && !isDown && combo.contains(keyCode)) {
                        if (!ffToggled) PhobosCore.setFastForward(false)
                    }
                }
                if (hotkeyTriggered) return@onKeyEvent true
                var bitmask = 0
                stateRef.settings.inputMappings.forEach { (bit, binding) -> if (binding == "k:$keyCode") bitmask = bitmask or bit }
                if (bitmask != 0) {
                    stateRef.hwButtons = if (isDown) stateRef.hwButtons or bitmask else stateRef.hwButtons and bitmask.inv()
                    stateRef.push()
                    return@onKeyEvent true
                } else if (keyCode == KeyEvent.KEYCODE_BACK) return@onKeyEvent false
                false
            }
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            val ratio = 4f / 3f; val maxWidthDp = this.maxWidth; val maxHeightDp = this.maxHeight
            var vW: Dp; var vH: Dp
            when(settingsState.aspectRatioMode) {
                AspectRatioMode.STRETCHED -> { vW = maxWidthDp; vH = maxHeightDp }
                AspectRatioMode.CORE_PROVIDED -> {
                    if (maxWidthDp / maxHeightDp > ratio) { vH = maxHeightDp; vW = vH * ratio }
                    else { vW = maxWidthDp; vH = vW / ratio }
                }
                AspectRatioMode.INTEGER_SCALED -> {
                    val scale = Math.min(maxWidthDp.value / 320f, maxHeightDp.value / 240f).toInt().coerceAtLeast(1)
                    vW = (320f * scale).dp; vH = (240f * scale).dp
                }
            }

            Box(modifier = Modifier.fillMaxSize().pointerInput(Unit) { detectTapGestures(onTap = { showControls = !showControls }) }, contentAlignment = Alignment.Center) {
                AndroidView(
                    factory = { ctx ->
                        SurfaceView(ctx).apply {
                            setZOrderMediaOverlay(true)
                            setOnGenericMotionListener { _, event ->
                                if (event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK &&
                                    event.action == MotionEvent.ACTION_MOVE) {
                                    var nLx = 0f; var nLy = 0f; var nRx = 0f; var nRy = 0f
                                    stateRef.settings.inputMappings.forEach { (bit, binding) ->
                                        if (binding.startsWith("a:")) {
                                            val parts = binding.split(":"); val axisId = parts[1].toInt()
                                            val pos = parts[2] == "1"; val value = event.getAxisValue(axisId)
                                            val isStick = bit >= (1 shl 17) && bit <= (1 shl 24)
                                            if (isStick) {
                                                when(bit) {
                                                    1 shl 17 -> if (!pos && value < 0f) nLy += value; 1 shl 18 -> if (pos && value > 0f) nLy += value
                                                    1 shl 19 -> if (!pos && value < 0f) nLx += value; 1 shl 20 -> if (pos && value > 0f) nLx += value
                                                    1 shl 21 -> if (!pos && value < 0f) nRy += value; 1 shl 22 -> if (pos && value > 0f) nRy += value
                                                    1 shl 23 -> if (!pos && value < 0f) nRx += value; 1 shl 24 -> if (pos && value > 0f) nRx += value
                                                }
                                            } else {
                                                val pressed = if (pos) value > 0.4f else value < -0.4f
                                                stateRef.hwButtons = if (pressed) stateRef.hwButtons or bit else stateRef.hwButtons and bit.inv()
                                            }
                                        }
                                    }
                                    stateRef.lx = nLx.coerceIn(-1f, 1f); stateRef.ly = nLy.coerceIn(-1f, 1f)
                                    stateRef.rx = nRx.coerceIn(-1f, 1f); stateRef.ry = nRy.coerceIn(-1f, 1f)
                                    stateRef.push()
                                    true
                                } else false
                            }
                            holder.addCallback(object : SurfaceHolder.Callback {
                                override fun surfaceCreated(h: SurfaceHolder) { PhobosCore.setSurface(h.surface) }
                                override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, h2: Int) { PhobosCore.setSurface(h.surface) }
                                override fun surfaceDestroyed(h: SurfaceHolder) { viewModel.setPause(true); PhobosCore.setSurface(null) }
                            })
                        }
                    },
                    modifier = Modifier.size(vW, vH)
                )
            }
        }

        if (!isLoaded && !isPaused) {
            Column(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.8f)), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
                Spacer(modifier = Modifier.height(16.dp))
                Text("Initializing $systemName...", style = MaterialTheme.typography.bodyLarge, color = Color.White)
            }
        }

        if (isLoaded && !isPaused && settingsState.showTouchControls) {
            VirtualGamepad(modifier = Modifier.fillMaxSize(), settings = settingsState, systemName = systemName, onInputChanged = { stateRef.virtualButtons = it; stateRef.push() })
        }

        if (isLoaded && !isPaused && settingsState.showPerformanceMonitor) {
            Box(modifier = Modifier.align(Alignment.TopEnd).padding(top = 100.dp, end = 16.dp).background(Color.Black.copy(alpha = 0.4f), RoundedCornerShape(4.dp)).padding(4.dp)) {
                Column {
                    Text("FPS: %.1f".format(perfStats.fps), color = if (perfStats.fps > 55) Color.Green else Color.Yellow, style = MaterialTheme.typography.labelSmall)
                    Text("Frame: %.2fms".format(perfStats.frameTime), color = Color.White, style = MaterialTheme.typography.labelSmall)
                    Text("Core: ${perfStats.activeCore}", color = if (perfStats.activeCore >= 4) Color.Cyan else Color.LightGray, style = MaterialTheme.typography.labelSmall)
                }
            }
        }

        if (showControls || isPaused || !isLoaded) {
            Box(modifier = Modifier.fillMaxSize()) {
                Surface(color = Color.Black.copy(alpha = 0.6f), modifier = Modifier.fillMaxWidth().align(Alignment.TopCenter) ) {
                    Row(modifier = Modifier.statusBarsPadding().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = { viewModel.setPause(true); showQuitDialog = true }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back", tint = Color.White) }
                        Text(text = romName, style = MaterialTheme.typography.titleMedium, color = Color.White, modifier = Modifier.weight(1f).padding(horizontal = 8.dp), maxLines = 1)
                        IconButton(onClick = { viewModel.togglePause() }) {
                            if (isPaused) Icon(Icons.Default.PlayArrow, contentDescription = "Resume", tint = Color.White)
                            else Icon(painterResource(R.drawable.ic_pause), contentDescription = "Pause", tint = Color.White)
                        }
                    }
                }
                if (isPaused) EmulationMenu(viewModel = viewModel, systemName = systemName, romName = romName, onResume = { viewModel.togglePause() }, onQuit = { showQuitDialog = true })
            }
        }
    }
}

@Composable
fun EmulationMenu(viewModel: MainViewModel, systemName: String, romName: String, onResume: () -> Unit, onQuit: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val context = LocalContext.current
    val diskLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.loadSecondaryRom(context, systemName, RomFile(uri.lastPathSegment ?: "Disk", uri))
    }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.7f)).clickable(enabled = false) {}, contentAlignment = Alignment.Center) {
        Card(modifier = Modifier.fillMaxWidth(0.85f).fillMaxHeight(0.8f), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(text = "Emulation Paused", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 16.dp))
                LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    item {
                        MenuSection("Save / Load States") {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    IconButton(onClick = { viewModel.decrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = "Prev") }
                                    Text("Slot $currentSlot", style = MaterialTheme.typography.bodyLarge)
                                    IconButton(onClick = { viewModel.incrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = "Next") }
                                }
                                Row {
                                    Button(onClick = { viewModel.saveState(systemName, romName, currentSlot); onResume() }) { Text("Save") }
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Button(onClick = { viewModel.loadState(systemName, romName, currentSlot); onResume() }) { Text("Load") }
                                }
                            }
                        }
                    }
                    if (systemName.contains("Nintendo 64") || systemName.contains("PlayStation") || systemName.contains("Sega Saturn")) {
                        item {
                            MenuSection("Disc Management") {
                                Button(onClick = { diskLauncher.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) {
                                    Icon(Icons.Default.Add, contentDescription = null); Spacer(modifier = Modifier.width(8.dp))
                                    Text(if (systemName.contains("Nintendo 64")) "Insert 64DD Disk" else "Change Disc")
                                }
                            }
                        }
                    }
                    if (systemName.contains("Nintendo 64", ignoreCase = true)) {
                        item {
                            MenuSection("N64 Settings") {
                                SettingsSwitchItem("Expansion Pak", "Increase RDRAM to 8MB.", settings.n64ExpansionPak) { viewModel.setN64ExpansionPak(it) }
                                SettingsSwitchItem("CPU Recompiler", "Use JIT recompiler.", settings.n64Recompiler) { viewModel.setN64Recompiler(it) }
                            }
                        }
                    }
                    item {
                        MenuSection("Settings Toggles") {
                            SettingsSwitchItem("Full Screen", "", settings.fullScreenMode) { viewModel.setFullScreenMode(it) }
                            SettingsSwitchItem("Touch Controls", "", settings.showTouchControls) { viewModel.setShowTouchControls(it) }
                            SettingsSwitchItem("Performance Monitor", "", settings.showPerformanceMonitor) { viewModel.setShowPerformanceMonitor(it) }
                            var aspectExpanded by remember { mutableStateOf(false) }
                            ListItem(
                                headlineContent = { Text("Aspect Ratio") },
                                trailingContent = {
                                    Box {
                                        TextButton(onClick = { aspectExpanded = true }) { Text(settings.aspectRatioMode.label) }
                                        DropdownMenu(expanded = aspectExpanded, onDismissRequest = { aspectExpanded = false }) {
                                            AspectRatioMode.entries.forEach { mode ->
                                                DropdownMenuItem(text = { Text(mode.label) }, onClick = { viewModel.setAspectRatioMode(mode); aspectExpanded = false })
                                            }
                                        }
                                    }
                                }
                            )
                        }
                    }
                    item {
                        MenuSection("System") {
                            Button(onClick = { viewModel.resetSystem(); onResume() }, modifier = Modifier.fillMaxWidth(), colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
                                Icon(Icons.Default.Refresh, contentDescription = null); Spacer(modifier = Modifier.width(8.dp)); Text("Reset System")
                            }
                        }
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    OutlinedButton(onClick = onQuit, modifier = Modifier.weight(1f)) { Text("Quit Game") }
                    Button(onClick = onResume, modifier = Modifier.weight(1f)) { Text("Resume") }
                }
            }
        }
    }
}

@Composable
fun MenuSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier = Modifier.padding(vertical = 8.dp)) {
        Text(title, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
        Spacer(modifier = Modifier.height(4.dp)); content()
        HorizontalDivider(modifier = Modifier.padding(top = 8.dp), thickness = 0.5.dp)
    }
}

@Composable
fun VirtualGamepad(modifier: Modifier = Modifier, settings: EmulatorSettings, systemName: String, onInputChanged: (Int) -> Unit) {
    var pressedButtons by remember { mutableIntStateOf(0) }
    fun updateInput(button: Int, pressed: Boolean) {
        val newButtons = if (pressed) pressedButtons or button else pressedButtons and button.inv()
        if (newButtons != pressedButtons) { pressedButtons = newButtons; onInputChanged(newButtons) }
    }
    val isVertical = settings.orientationVertical && systemName.contains("WonderSwan", ignoreCase = true)
    Box(modifier = modifier) {
        if (!isVertical) {
            DPad(Modifier.align(Alignment.BottomStart).padding(start = 32.dp, bottom = 48.dp).size(150.dp)) { b, p -> updateInput(b, p) }
            ActionButtons(Modifier.align(Alignment.BottomEnd).padding(end = 32.dp, bottom = 48.dp).size(180.dp)) { b, p -> 
                val mapped = when(b) { PhobosCore.Input.A -> PhobosCore.Input.B; PhobosCore.Input.B -> PhobosCore.Input.A; PhobosCore.Input.X -> PhobosCore.Input.Y; PhobosCore.Input.Y -> PhobosCore.Input.X; else -> b }
                updateInput(mapped, p) 
            }
        } else {
            DPad(Modifier.align(Alignment.BottomCenter).padding(bottom = 80.dp).size(150.dp)) { b, p -> updateInput(b, p) }
            ActionButtons(Modifier.align(Alignment.TopCenter).padding(top = 100.dp).size(180.dp)) { b, p -> 
                val mapped = when(b) { PhobosCore.Input.A -> PhobosCore.Input.B; PhobosCore.Input.B -> PhobosCore.Input.A; PhobosCore.Input.X -> PhobosCore.Input.Y; PhobosCore.Input.Y -> PhobosCore.Input.X; else -> b }
                updateInput(mapped, p) 
            }
        }
        Row(Modifier.align(Alignment.BottomCenter).padding(bottom = 32.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            VirtualButton("SEL", Modifier.size(60.dp, 35.dp)) { updateInput(PhobosCore.Input.SELECT, it) }
            VirtualButton("START", Modifier.size(70.dp, 35.dp)) { updateInput(PhobosCore.Input.START, it) }
        }
    }
}

@Composable
fun DPad(modifier: Modifier = Modifier, onPress: (Int, Boolean) -> Unit) {
    Box(modifier = modifier) {
        Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally) {
            VirtualButton("U", Modifier.weight(1f).fillMaxWidth(0.4f)) { onPress(PhobosCore.Input.UP, it) }
            Row(Modifier.weight(1f).fillMaxWidth()) {
                VirtualButton("L", Modifier.weight(1f).fillMaxHeight()) { onPress(PhobosCore.Input.LEFT, it) }
                Box(Modifier.weight(1f).fillMaxHeight())
                VirtualButton("R", Modifier.weight(1f).fillMaxHeight()) { onPress(PhobosCore.Input.RIGHT, it) }
            }
            VirtualButton("D", Modifier.weight(1f).fillMaxWidth(0.4f)) { onPress(PhobosCore.Input.DOWN, it) }
        }
    }
}

@Composable
fun ActionButtons(modifier: Modifier = Modifier, onPress: (Int, Boolean) -> Unit) {
    Box(modifier = modifier) {
        VirtualButton("X", Modifier.align(Alignment.TopCenter).size(55.dp)) { onPress(PhobosCore.Input.X, it) }
        VirtualButton("B", Modifier.align(Alignment.BottomCenter).size(55.dp)) { onPress(PhobosCore.Input.B, it) }
        VirtualButton("Y", Modifier.align(Alignment.CenterStart).size(55.dp)) { onPress(PhobosCore.Input.Y, it) }
        VirtualButton("A", Modifier.align(Alignment.CenterEnd).size(55.dp)) { onPress(PhobosCore.Input.A, it) }
    }
}

@Composable
fun VirtualButton(text: String, modifier: Modifier = Modifier, onPress: (Boolean) -> Unit) {
    var isPressed by remember { mutableStateOf(false) }
    val activePointers = remember { mutableStateListOf<Long>() }
    Surface(
        color = if (isPressed) Color.White.copy(alpha = 0.5f) else Color.White.copy(alpha = 0.2f),
        shape = CircleShape,
        modifier = modifier.clip(CircleShape).pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    val event = awaitPointerEvent(); event.changes.forEach { it.consume() }
                    event.changes.forEach { change ->
                        val pointerId = change.id.value
                        if (change.changedToDown()) { if (!activePointers.contains(pointerId)) activePointers.add(pointerId) }
                        else if (change.changedToUp()) { activePointers.remove(pointerId) }
                    }
                    val pressed = activePointers.isNotEmpty()
                    if (pressed != isPressed) { isPressed = pressed; onPress(pressed) }
                }
            }
        }
    ) { Box(contentAlignment = Alignment.Center) { Text(text = text, color = Color.White, style = MaterialTheme.typography.labelLarge) } }
}
