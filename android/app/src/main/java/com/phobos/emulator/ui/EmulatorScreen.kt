package com.phobos.emulator.ui

import android.app.Activity
import android.content.Context
import android.net.Uri
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
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.*
import androidx.compose.ui.input.pointer.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.text.font.FontWeight
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
import com.phobos.emulator.input.GameInputState
import kotlinx.coroutines.delay

// ─── EmulatorScreen ──────────────────────────────────────────────────────────

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

    var pressedKeys by remember { mutableStateOf(setOf<Int>()) }
    var ffToggled by remember { mutableStateOf(false) }

    // ── Fullscreen ───────────────────────────────────────────────────────────
    DisposableEffect(settingsState.fullScreenMode) {
        if (settingsState.fullScreenMode && window != null) {
            val controller = WindowCompat.getInsetsController(window, view)
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        onDispose {
            if (window != null) {
                WindowCompat.getInsetsController(window, view)
                    .show(WindowInsetsCompat.Type.systemBars())
            }
        }
    }

    // ── Lifecycle effects ────────────────────────────────────────────────────
    LaunchedEffect(isLoaded) {
        if (isLoaded) {
            viewModel.setPause(false)
            PhobosCore.setEmulationRunning(true)
        }
    }

    LaunchedEffect(isPaused) {
        if (isPaused) GameInputState.releaseAllButtons()
    }

    LaunchedEffect(showControls, isPaused) {
        if (showControls && !isPaused) { delay(3000); showControls = false }
    }

    BackHandler { if (!isLoaded || isPaused) { viewModel.setPause(true); showQuitDialog = true } }

    DisposableEffect(Unit) {
        onDispose {
            GameInputState.reset()
            viewModel.unloadSystem()
        }
    }

    // ── Quit dialog ──────────────────────────────────────────────────────────
    if (showQuitDialog) {
        AlertDialog(
            onDismissRequest = { showQuitDialog = false; viewModel.setPause(false) },
            title = { Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating $romName?") },
            confirmButton = {
                TextButton(onClick = { showQuitDialog = false; onBack() }) { Text("Quit") }
            },
            dismissButton = {
                TextButton(onClick = { showQuitDialog = false; viewModel.setPause(false) }) { Text("Cancel") }
            }
        )
    }

    // ── Main container ───────────────────────────────────────────────────────
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .focusRequester(focusRequester)
            .focusable()
            .onFocusChanged { fs -> if (!fs.isFocused) GameInputState.releaseAllButtons() }
            .onPreviewKeyEvent { keyEvent ->
                val native = keyEvent.nativeKeyEvent
                val keyCode = native.keyCode
                val isDown = keyEvent.type == KeyEventType.KeyDown

                // Do NOT consume volume keys — let the Android system handle them.
                if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || 
                    keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || 
                    keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
                    return@onPreviewKeyEvent false
                }

                if (native.repeatCount > 0) return@onPreviewKeyEvent true

                pressedKeys = if (isDown) pressedKeys + keyCode else pressedKeys - keyCode

                var hotkeyTriggered = false
                settingsState.hotkeys.forEach { (action, combo) ->
                    if (combo.isNotEmpty() && combo.size == pressedKeys.size && combo.all { pressedKeys.contains(it) }) {
                        hotkeyTriggered = true
                        if (isDown) {
                            when (action) {
                                "pause"          -> viewModel.togglePause()
                                "ff_hold"        -> PhobosCore.setFastForward(true)
                                "ff_toggle"      -> { ffToggled = !ffToggled; PhobosCore.setFastForward(ffToggled) }
                                "save"           -> viewModel.saveState(systemName, romName, currentSlot)
                                "load"           -> viewModel.loadState(systemName, romName, currentSlot)
                                "inc_slot"       -> viewModel.incrementSlot()
                                "dec_slot"       -> viewModel.decrementSlot()
                                "reset"          -> viewModel.resetSystem()
                                "frame_advance"  -> if (isPaused) PhobosCore.frameAdvance()
                                "mute"           -> viewModel.setMuteAudio(!settingsState.muteAudio)
                                "screenshot"     -> viewModel.takeScreenshot(systemName, romName)
                                "reload"         -> viewModel.roms.value.find { it.name == romName }
                                    ?.let { viewModel.loadRom(view.context, systemName, it) }
                                "quit"           -> { showQuitDialog = true; viewModel.setPause(true) }
                                "analog_toggle"  -> viewModel.togglePs1AnalogMode()
                            }
                        }
                    } else if (action == "ff_hold" && !isDown && combo.contains(keyCode)) {
                        if (!ffToggled) PhobosCore.setFastForward(false)
                    }
                }
                if (hotkeyTriggered) return@onPreviewKeyEvent true

                // Game input via key mappings
                var bitmask = 0
                settingsState.inputMappings.forEach { (bit, binding) ->
                    if (binding == "k:$keyCode") bitmask = bitmask or bit
                }
                if (bitmask != 0) {
                    GameInputState.setButton(bitmask, isDown)
                    return@onPreviewKeyEvent true
                }
                false
            }
            .onKeyEvent { keyEvent ->
                val keyCode = keyEvent.nativeKeyEvent.keyCode
                if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || 
                    keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || 
                    keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
                    return@onKeyEvent false
                }

                // When game is loaded and running, consume BACK key events
                // so the BackHandler doesn't fire when B is pressed on a gamepad.
                // If paused, let BACK through to show the quit dialog.
                if (!isLoaded || isPaused) return@onKeyEvent false
                keyEvent.nativeKeyEvent.let {
                    if (it.keyCode == KeyEvent.KEYCODE_BACK) return@onKeyEvent true
                }
                true
            }
    ) {
        // ── SurfaceView (game picture) ───────────────────────────────────────
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) { detectTapGestures(onTap = { showControls = !showControls }) },
            contentAlignment = Alignment.Center
        ) {
            val ratio = 4f / 3f
            val maxWidthDp = this.maxWidth; val maxHeightDp = this.maxHeight
            var vW: Dp; var vH: Dp
            when (settingsState.aspectRatioMode) {
                AspectRatioMode.STRETCHED -> { vW = maxWidthDp; vH = maxHeightDp }
                AspectRatioMode.CORE_PROVIDED -> {
                    if (maxWidthDp / maxHeightDp > ratio) { vH = maxHeightDp; vW = vH * ratio }
                    else { vW = maxWidthDp; vH = vW / ratio }
                }
                AspectRatioMode.INTEGER_SCALED -> {
                    val s = maxOf(1, minOf(maxWidthDp.value / 320f, maxHeightDp.value / 240f).toInt())
                    vW = (320f * s).dp; vH = (240f * s).dp
                }
            }

            AndroidView(
                factory = { ctx ->
                    SurfaceView(ctx).apply {
                        setZOrderMediaOverlay(true)
                        isFocusable = false
                        setOnGenericMotionListener { _, event ->
                            val mappings = viewModel.settings.value.inputMappings
                            GameInputState.handleMotionEvent(event, mappings)
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

        // ── Loading overlay ──────────────────────────────────────────────────
        if (!isLoaded && !isPaused) {
            Column(
                modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.8f)),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
                Spacer(modifier = Modifier.height(16.dp))
                Text("Initializing $systemName...", style = MaterialTheme.typography.bodyLarge, color = Color.White)
            }
        }

        // ── Touch controls ───────────────────────────────────────────────────
        if (isLoaded && !isPaused && settingsState.showTouchControls) {
            TouchControls(
                systemName = systemName,
                onInputChanged = { mask -> GameInputState.updateVirtualButtons(mask) },
                modifier = Modifier.fillMaxSize()
            )
        }

        // ── Performance monitor ──────────────────────────────────────────────
        if (isLoaded && !isPaused && settingsState.showPerformanceMonitor) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 100.dp, end = 16.dp)
                    .background(Color.Black.copy(alpha = 0.4f), RoundedCornerShape(4.dp))
                    .padding(4.dp)
            ) {
                Column {
                    Text("FPS: %.1f".format(perfStats.fps),
                        color = if (perfStats.fps > 55) Color.Green else Color.Yellow,
                        style = MaterialTheme.typography.labelSmall)
                    Text("Frame: %.2fms".format(perfStats.frameTime),
                        color = Color.White, style = MaterialTheme.typography.labelSmall)
                    Text("Core: ${perfStats.activeCore}",
                        color = if (perfStats.activeCore >= 4) Color.Cyan else Color.LightGray,
                        style = MaterialTheme.typography.labelSmall)
                }
            }
        }

        // ── Top bar (auto-hide) ──────────────────────────────────────────
        if (isLoaded && (showControls || isPaused || showQuitDialog)) {
            Surface(
                color = Color.Black.copy(alpha = 0.6f),
                modifier = Modifier.fillMaxWidth().align(Alignment.TopCenter)
            ) {
                Row(
                    modifier = Modifier.statusBarsPadding().padding(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = { viewModel.setPause(true); showQuitDialog = true }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back", tint = Color.White)
                    }
                    Text(
                        romName, style = MaterialTheme.typography.titleMedium, color = Color.White,
                        modifier = Modifier.weight(1f).padding(horizontal = 8.dp), maxLines = 1
                    )
                    IconButton(onClick = { viewModel.togglePause() }) {
                        if (isPaused) Icon(Icons.Default.PlayArrow, contentDescription = "Resume", tint = Color.White)
                        else Icon(painterResource(R.drawable.ic_pause), contentDescription = "Pause", tint = Color.White)
                    }
                }
            }
        }

        // ── Pause menu ───────────────────────────────────────────────────────
        if (isPaused) {
            Box(modifier = Modifier.fillMaxSize()) {
                EmulationMenu(
                    viewModel = viewModel, systemName = systemName, romName = romName,
                    onResume = { viewModel.togglePause() },
                    onQuit = { showQuitDialog = true }
                )
            }
        }
    }
}

// ─── EmulationMenu ───────────────────────────────────────────────────────────

@Composable
fun EmulationMenu(
    viewModel: MainViewModel, systemName: String, romName: String,
    onResume: () -> Unit, onQuit: () -> Unit
) {
    val settings by viewModel.settings.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val context = LocalContext.current
    val diskLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.loadSecondaryRom(context, systemName, RomFile(uri.lastPathSegment ?: "Disk", uri))
    }

    Box(
        modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.7f)).clickable(enabled = false) {},
        contentAlignment = Alignment.Center
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(0.85f).fillMaxHeight(0.8f),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Emulation Paused", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 16.dp))
                LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    // ── Save / Load ──────────────────────────────────────────
                    item {
                        MenuSection("Save / Load States") {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    IconButton(onClick = { viewModel.decrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Prev") }
                                    Text("Slot $currentSlot", style = MaterialTheme.typography.bodyLarge)
                                    IconButton(onClick = { viewModel.incrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Next") }
                                }
                                Row {
                                    Button(onClick = { viewModel.saveState(systemName, romName, currentSlot); onResume() }) { Text("Save") }
                                    Spacer(Modifier.width(8.dp))
                                    Button(onClick = { viewModel.loadState(systemName, romName, currentSlot); onResume() }) { Text("Load") }
                                }
                            }
                        }
                    }

                    // ── Disc Management (N64 / PS1 / Saturn) ─────────────────
                    if (systemName.contains("Nintendo 64") || systemName.contains("PlayStation") || systemName.contains("Sega Saturn")) {
                        item {
                            MenuSection("Disc Management") {
                                Button(onClick = { diskLauncher.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) {
                                    Icon(Icons.Default.Add, null); Spacer(Modifier.width(8.dp))
                                    Text(if (systemName.contains("Nintendo 64")) "Insert 64DD Disk" else "Change Disc")
                                }
                            }
                        }
                    }

                    // ── N64 Settings ─────────────────────────────────────────
                    if (systemName.contains("Nintendo 64", ignoreCase = true)) {
                        item {
                            MenuSection("N64 Settings") {
                                var upscaleExpanded by remember { mutableStateOf(false) }
                                ListItem(
                                    headlineContent = { Text("Internal Upscale") },
                                    trailingContent = {
                                        Box {
                                            TextButton(onClick = { upscaleExpanded = true }) { Text("${settings.n64Upscale}x") }
                                            DropdownMenu(expanded = upscaleExpanded, onDismissRequest = { upscaleExpanded = false }) {
                                                listOf(1, 2, 4, 8).forEach { factor ->
                                                    DropdownMenuItem(
                                                        text = { Text("${factor}x") },
                                                        onClick = { viewModel.setN64Upscale(factor); upscaleExpanded = false }
                                                    )
                                                }
                                            }
                                        }
                                    }
                                )
                                SettingsSwitchItem("Expansion Pak", "Increase RDRAM to 8MB.", settings.n64ExpansionPak) { viewModel.setN64ExpansionPak(it) }
                                SettingsSwitchItem("CPU Recompiler", "Use JIT recompiler.", settings.n64Recompiler) { viewModel.setN64Recompiler(it) }
                            }
                        }
                    }

                    // ── DualShock (PS1) ──────────────────────────────────────
                    if (systemName.contains("PlayStation", ignoreCase = true)) {
                        item {
                            MenuSection("DualShock") {
                                var ps1Analog by remember { mutableStateOf(settings.ps1AnalogMode) }
                                LaunchedEffect(settings.ps1AnalogMode) { ps1Analog = settings.ps1AnalogMode }
                                SettingsSwitchItem("Analog Mode", "Toggle DualShock analog mode.", ps1Analog) {
                                    viewModel.togglePs1AnalogMode()
                                }
                            }
                        }
                    }

                    // ── Settings Toggles ─────────────────────────────────────
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

                    // ── System ───────────────────────────────────────────────
                    item {
                        MenuSection("System") {
                            Button(
                                onClick = { viewModel.resetSystem(); onResume() },
                                modifier = Modifier.fillMaxWidth(),
                                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
                            ) {
                                Icon(Icons.Default.Refresh, null); Spacer(Modifier.width(8.dp)); Text("Reset System")
                            }
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    OutlinedButton(onClick = onQuit, modifier = Modifier.weight(1f)) { Text("Quit Game") }
                    Button(onClick = onResume, modifier = Modifier.weight(1f)) { Text("Resume") }
                }
            }
        }
    }
}

// ─── MenuSection ─────────────────────────────────────────────────────────────

@Composable
fun MenuSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier = Modifier.padding(vertical = 8.dp)) {
        Text(title, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(4.dp)); content()
        HorizontalDivider(modifier = Modifier.padding(top = 8.dp), thickness = 0.5.dp)
    }
}
