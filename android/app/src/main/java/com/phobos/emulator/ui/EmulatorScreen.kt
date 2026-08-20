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
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
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

// Interpolate along a 4-stop rainbow (red->yellow->green->blue) by t in [0,1].
private fun lerpRainbow(stops: List<Color>, t: Float): Color {
    val clamped = t.coerceIn(0f, 1f)
    if (stops.size < 2) return stops.first()
    val seg = clamped * (stops.size - 1)
    val idx = seg.toInt().coerceIn(0, stops.size - 2)
    val frac = seg - idx
    return Color(
        red = stops[idx].red + (stops[idx + 1].red - stops[idx].red) * frac,
        green = stops[idx].green + (stops[idx + 1].green - stops[idx].green) * frac,
        blue = stops[idx].blue + (stops[idx + 1].blue - stops[idx].blue) * frac,
        alpha = 1f
    )
}

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
    // On-screen keyboard for keyboard-based systems (ZX Spectrum / 128).
    // Toggled real-time via hotkey or the pause menu. The keyboard's internal
    // state (scheme, turbo, shift latches) is hoisted HERE so it persists
    // when the keyboard overlay is hidden/shown.
    var showKeyboard by remember { mutableStateOf(false) }
    var zxSymLatched by remember { mutableStateOf(false) }
    var zxCapsLatched by remember { mutableStateOf(false) }
    var zxTurboTape by remember { mutableStateOf(false) }
    var zxControlScheme by remember { mutableStateOf(0) }
    // ZX CUSTOM rebind target: the keyboard key label currently being bound to
    // a gamepad control (null = not rebinding). Only active in CUSTOM mode.
    var zxRebindTarget by remember { mutableStateOf<String?>(null) }

    val showDriverSuggestion by viewModel.showDriverSuggestion.collectAsState()
    val unsupportedSystem by viewModel.unsupportedSystem.collectAsState()
    val biosRequired by viewModel.biosRequired.collectAsState()

    // Push the persisted N64 debug-logging setting to native whenever it
    // changes (including the initial DataStore load). The init block pushes
    // the DEFAULT (false) before DataStore emits the persisted value, so
    // without this, native stays off until a manual re-toggle.
    LaunchedEffect(settingsState.n64DebugLogging) {
        PhobosCore.setN64DebugLogging(settingsState.n64DebugLogging)
    }

    val focusRequester = remember { FocusRequester() }
    val context = LocalContext.current
    val view = LocalView.current
    val window = (view.context as? Activity)?.window

    var pressedKeys by remember { mutableStateOf(setOf<Int>()) }
    var ffToggled by remember { mutableStateOf(false) }

    // Re-run the hotkey combo check when the D-pad hat changes (motion events
    // don't otherwise trigger onPreviewKeyEvent, so Z + D-pad hotkeys would
    // never fire even though the hat is tracked).
    val triggerHotkeyCheck = rememberUpdatedState<() -> Unit> {
        val allPressed = pressedKeys + GameInputState.hotkeyKeys
        settingsState.hotkeys.forEach { (action, combo) ->
            if (combo.isNotEmpty() && combo.size == allPressed.size && combo.all { allPressed.contains(it) }) {
                when (action) {
                    "inc_slot"       -> viewModel.incrementSlot()
                    "dec_slot"       -> viewModel.decrementSlot()
                    "save"           -> viewModel.saveState(systemName, romName, currentSlot)
                    "load"           -> viewModel.loadState(systemName, romName, currentSlot)
                    "library"        -> viewModel.swapToLibrary()
                    else -> {}
                }
            }
        }
    }
    DisposableEffect(Unit) {
        GameInputState.onHotkeyKeysChanged = { triggerHotkeyCheck.value() }
        onDispose { GameInputState.onHotkeyKeysChanged = null }
    }

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
            // After loading, request focus so hardware key events
            // (hotkeys, gamepad) reach onPreviewKeyEvent immediately
            // rather than waiting for a tap.
            focusRequester.requestFocus()
        }
    }

    LaunchedEffect(isPaused) {
        if (isPaused) {
            GameInputState.releaseAllButtons()
            pressedKeys = emptySet()  // clear stale keys so hotkey combos re-arm
        }
    }

    LaunchedEffect(showControls, isPaused) {
        if (showControls && !isPaused) { delay(3000); showControls = false }
    }

    BackHandler { if (!isLoaded || isPaused) { viewModel.setPause(true); showQuitDialog = true } }

    DisposableEffect(Unit) {
        viewModel.setEmulatorScreenVisible(true)
        onDispose {
            viewModel.setEmulatorScreenVisible(false)
            GameInputState.reset()
            // The game stays loaded (paused) when leaving the screen — the
            // swap-screen hotkey relies on it. Unload only happens via the
            // explicit Quit flow (quit dialog), which calls unloadSystem()
            // before navigating back.
        }
    }

    // ── Quit dialog ──────────────────────────────────────────────────────────
    if (showQuitDialog) {
        AlertDialog(
            onDismissRequest = { showQuitDialog = false; viewModel.setPause(false) },
            title = { Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating $romName?") },
            confirmButton = {
                TextButton(onClick = {
                    showQuitDialog = false
                    viewModel.unloadSystem()
                    onBack()
                }) { Text("Quit") }
            },
            dismissButton = {
                TextButton(onClick = { showQuitDialog = false; viewModel.setPause(false) }) { Text("Cancel") }
            }
        )
    }

    // ── Driver suggestion dialog ─────────────────────────────────────────────
    if (showDriverSuggestion) {
        AlertDialog(
            onDismissRequest = { viewModel.dismissDriverSuggestion() },
            title = { Text("GPU Driver Issue Detected") },
            text = {
                Text("The built-in GPU driver is unable to compile shaders needed by this game. " +
                     "You may see visual glitches, missing graphics, or reduced performance.\n\n" +
                     "For best results, install a Turnip Mesa driver via:\n" +
                     "Settings → GPU Driver Manager")
            },
            confirmButton = {
                TextButton(onClick = { viewModel.dismissDriverSuggestion() }) { Text("OK") }
            }
        )
    }

    // ── Unsupported system dialog (broken cores) ──────────────────────────
    unsupportedSystem?.let { sys ->
        AlertDialog(
            // On OK (or dismiss), navigate back to the library — the load was
            // refused, so EmulatorScreen has nothing to show (it would sit on
            // the "Initializing" spinner forever).
            onDismissRequest = {
                viewModel.dismissUnsupportedSystem()
                onBack()
            },
            title = { Text("$sys Unsupported") },
            text = {
                Text("$sys games are currently unsupported in this build. " +
                     "This is a known issue being worked on.\n\n" +
                     "Please try a different system or game.")
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.dismissUnsupportedSystem()
                    onBack()
                }) { Text("OK") }
            }
        )
    }

    // ── Neo Geo BIOS required dialog ─────────────────────────────────────
    biosRequired?.let { sys ->
        AlertDialog(
            onDismissRequest = {
                viewModel.dismissBiosRequired()
                onBack()
            },
            title = { Text("Neo Geo BIOS Required") },
            text = {
                Text("The Neo Geo core cannot boot without a BIOS.\n\n" +
                     "Add neogeo.zip (containing sp-e.sp1 and 000-lo.lo) by " +
                     "setting the Neo Geo BIOS in Settings, or place neogeo.zip " +
                     "next to your ROMs, then try loading the game again.")
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.dismissBiosRequired()
                    onBack()
                }) { Text("OK") }
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

                // Hotkey match: the pressed keys (key events) PLUS the virtual
                // D-pad keys from hat axes (GameInputState.hotkeyKeys). This
                // lets combos like Z + D-pad-Right work even though the D-pad
                // is reported as a hat (motion event), not a keycode.
                val allPressed = pressedKeys + GameInputState.hotkeyKeys
                var hotkeyTriggered = false
                settingsState.hotkeys.forEach { (action, combo) ->
                    if (combo.isNotEmpty() && combo.size == allPressed.size && combo.all { allPressed.contains(it) }) {
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
                                "keyboard"       -> showKeyboard = !showKeyboard
                                "library"        -> viewModel.swapToLibrary()
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
                            GameInputState.handleMotionEvent(event, mappings, systemName)
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

        // ── On-screen keyboard (ZX Spectrum / 128) — real-time toggle ──────
        if (isLoaded && showKeyboard && systemName.contains("ZX Spectrum", ignoreCase = true)) {
            ZXKeyboardOverlay(
                modifier = Modifier.align(Alignment.BottomCenter),
                symLatched = zxSymLatched, onSymLatched = { zxSymLatched = it },
                capsLatched = zxCapsLatched, onCapsLatched = { zxCapsLatched = it },
                turboTape = zxTurboTape, onTurboTape = { zxTurboTape = it },
                controlScheme = zxControlScheme, onControlScheme = { zxControlScheme = it },
                systemName = systemName,
                rebindTarget = zxRebindTarget, onRebindTarget = { zxRebindTarget = it },
                onBindKey = { label, bit -> viewModel.setZxKeyBinding(systemName, label, bit) },
                boundKeys = (settingsState.zxKeyBindings[systemName] ?: emptyMap()).keys,
                keyboardOpacity = settingsState.zxKeyboardOpacity
            )
        }

        // ── ZX tape-loading progress bar ────────────────────────────────────
        // Shows while the ZX tape is playing (LOAD ""), disappears on completion.
        // Progress = 0..10000 (percent*100); -1 = not playing → hide.
        if (isLoaded && systemName.contains("ZX Spectrum", ignoreCase = true)) {
            val tapeProgress by viewModel.zxTapeProgress.collectAsState()
            if (tapeProgress >= 0) {  // playing
                val pct = tapeProgress / 100f
                // ZX Spectrum rainbow stops (red -> yellow -> green -> blue)
                val rainbowStops = listOf(
                    Color(0xFFC3463A), // red
                    Color(0xFFE2C332), // yellow
                    Color(0xFF64A34A), // green
                    Color(0xFF64ADD0), // blue
                )
                // Animated shimmer sweep across the filled segments.
                val shimmer by rememberInfiniteTransition().animateFloat(
                    initialValue = -1f, targetValue = 2f,
                    animationSpec = infiniteRepeatable(tween(1600, easing = LinearEasing), RepeatMode.Restart)
                )

                Column(
                    modifier = Modifier
                        .align(Alignment.TopCenter)
                        .padding(top = 10.dp)
                        .fillMaxWidth(0.75f),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    // % label pill
                    Text(
                        "Loading tape… ${pct.toInt()}%",
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = Color.White,
                        modifier = Modifier
                            .shadow(6.dp, RoundedCornerShape(8.dp))
                            .background(Color(0xCC101010), RoundedCornerShape(8.dp))
                            .border(1.dp, Color.White.copy(alpha = 0.25f), RoundedCornerShape(8.dp))
                            .padding(horizontal = 14.dp, vertical = 6.dp)
                    )
                    Spacer(Modifier.height(8.dp))
                    // Segmented rainbow bar wrapped in a dark rounded outline frame
                    // so it's clearly visible against any background.
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(22.dp)
                            .shadow(10.dp, RoundedCornerShape(11.dp))          // outer glow
                            .background(Color(0xCC101010), RoundedCornerShape(11.dp))  // dark track bg
                            .border(2.dp, Color(0xE6151515), RoundedCornerShape(11.dp)) // dark outline
                            .padding(2.dp)  // inset so segments sit inside the frame
                    ) {
                        Canvas(
                            modifier = Modifier.fillMaxSize()
                        ) {
                            val segGap = 1.5.dp.toPx()
                            val segW = (size.width - segGap * 9) / 10f
                            val h = size.height
                            val filledSegs = (pct / 10f).toInt().coerceIn(0, 10)
                            // Track (dark, slightly rounded per segment)
                            for (i in 0 until 10) {
                                val x = i * (segW + segGap)
                                drawRoundRect(
                                    color = Color(0x66101010),
                                    topLeft = Offset(x, 0f),
                                    size = Size(segW, h),
                                    cornerRadius = CornerRadius(5.dp.toPx(), 5.dp.toPx())
                                )
                            }
                            // Filled segments: rainbow gradient across filled width
                            for (i in 0 until filledSegs) {
                                val x = i * (segW + segGap)
                                // Color per segment along the 4-stop rainbow
                                val t = i.toFloat() / filledSegs
                                val col = lerpRainbow(rainbowStops, t)
                                drawRoundRect(
                                    color = col,
                                    topLeft = Offset(x, 0f),
                                    size = Size(segW, h),
                                    cornerRadius = CornerRadius(5.dp.toPx(), 5.dp.toPx())
                                )
                                // Top shine: a lighter band across the upper half
                                drawRoundRect(
                                    color = Color.White.copy(alpha = 0.28f),
                                    topLeft = Offset(x, 1.dp.toPx()),
                                    size = Size(segW, h * 0.35f),
                                    cornerRadius = CornerRadius(4.dp.toPx(), 4.dp.toPx())
                                )
                            }
                            // Shimmer sweep: a moving white streak over the fill
                            if (filledSegs > 0) {
                                val sweepX = (shimmer + 0.5f) * (segW + segGap) * filledSegs
                                val streakW = (segW + segGap) * 2f
                                drawRoundRect(
                                    color = Color.White.copy(alpha = 0.25f),
                                    topLeft = Offset(sweepX - streakW / 2, 0f),
                                    size = Size(streakW, h),
                                    cornerRadius = CornerRadius(5.dp.toPx(), 5.dp.toPx())
                                )
                            }
                        }
                    }
                }
            }
        }

        // ── Performance monitor (draggable/resizable) ───────────────────────
        if (isLoaded && !isPaused && settingsState.showPerformanceMonitor) {
            key(showControls) {
                val screenWidth = context.resources.displayMetrics.widthPixels
                val screenHeight = context.resources.displayMetrics.heightPixels
                PerformanceOverlay(
                    perfStats = perfStats,
                    savedScale = settingsState.perfOverlayScale,
                    savedPosX = settingsState.perfOverlayPosX,
                    savedPosY = settingsState.perfOverlayPosY,
                    screenWidth = screenWidth,
                    screenHeight = screenHeight,
                    onScaleChanged = { viewModel.setPerfOverlayScale(it) },
                    onPositionChanged = { x, y ->
                        viewModel.setPerfOverlayPosX(x)
                        viewModel.setPerfOverlayPosY(y)
                    },
                    modifier = Modifier.fillMaxSize(),
                    showFps = settingsState.perfShowFps,
                    showFrameTime = settingsState.perfShowFrameTime,
                    showRam = settingsState.perfShowRam,
                    showCore = settingsState.perfShowCore,
                    showShaderFails = settingsState.perfShowShaderFails
                )
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
                    showKeyboard = showKeyboard,
                    onKeyboardToggle = { showKeyboard = it },
                    onResume = { viewModel.togglePause() },
                    onQuit = { showQuitDialog = true },
                    onLibrary = { viewModel.swapToLibrary() },
                    zxControlScheme = zxControlScheme,
                    onZxControlScheme = { s ->
                        zxControlScheme = s
                        viewModel.setZxControlScheme(systemName, s)
                    }
                )
            }
        }
    }
}

// ─── EmulationMenu ───────────────────────────────────────────────────────────

@Composable
fun EmulationMenu(
    viewModel: MainViewModel, systemName: String, romName: String,
    showKeyboard: Boolean, onKeyboardToggle: (Boolean) -> Unit,
    onResume: () -> Unit, onQuit: () -> Unit, onLibrary: () -> Unit,
    zxControlScheme: Int = 0, onZxControlScheme: (Int) -> Unit = {}
) {
    val settings by viewModel.settings.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val context = LocalContext.current
    val diskLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.loadSecondaryRom(context, systemName, RomFile(uri.lastPathSegment ?: "Disk", uri))
    }
    // In-pause sub-menu state: the N64 Experimental screen replaces the main
    // pause list (with a back arrow) to keep the pause menu clean.
    var experimentalOpen by remember { mutableStateOf(false) }
    BackHandler(enabled = experimentalOpen) { experimentalOpen = false }

    Box(
        modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.7f)).clickable(enabled = false) {},
        contentAlignment = Alignment.Center
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(0.85f).fillMaxHeight(0.8f),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                if (experimentalOpen) {
                    N64ExperimentalSection(viewModel, settings, onBack = { experimentalOpen = false })
                } else {
                Text("Emulation Paused", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 16.dp))
                LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    // ── Save / Load ──────────────────────────────────────────
                    item {
                        MenuSection("Save / Load States") {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    IconButton(onClick = { viewModel.decrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Prev") }
                                    Text(if (currentSlot < 0) "Slot Auto" else "Slot $currentSlot", style = MaterialTheme.typography.bodyLarge)
                                    IconButton(onClick = { viewModel.incrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Next") }
                                }
                                Row {
                                    Button(onClick = { viewModel.saveState(systemName, romName, currentSlot); onResume() }) { Text("Save") }
                                    Spacer(Modifier.width(8.dp))
                                    Button(onClick = { viewModel.loadState(systemName, romName, currentSlot); onResume() }) { Text("Load") }
                                    Spacer(Modifier.width(8.dp))
                                    OutlinedButton(
                                        onClick = { viewModel.deleteState(systemName, romName, currentSlot) },
                                        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)
                                    ) { Text("Delete") }
                                }
                            }
                            // Auto-Save / Auto-Load STATE toggles (all cores) — distinct
                            // from the always-on cartridge/flash save flushing.
                            SettingsSwitchItem(
                                "Auto-Save State",
                                "Save a state snapshot automatically when you quit a game",
                                settings.autoSaveState
                            ) { viewModel.setAutoSaveState(it) }
                            SettingsSwitchItem(
                                "Auto-Load State",
                                "Restore the auto-saved state when a game is loaded",
                                settings.autoLoadState
                            ) { viewModel.setAutoLoadState(it) }
                        }
                    }

                    // ── Boot Options (per-core — ares supports Fast Boot only on
                    //    GB/GBC, NGP/NGPC, PS1) ────────────────────────────────
                    val supportsFastBoot = systemName == "Game Boy" ||
                        systemName == "Game Boy Color" ||
                        systemName.contains("Neo Geo Pocket") ||
                        systemName == "PlayStation"
                    if (supportsFastBoot) {
                        item {
                            MenuSection("Boot Options") {
                                SettingsSwitchItem(
                                    "Fast Boot",
                                    "Skip BIOS and intro animations",
                                    settings.fastBoot
                                ) { viewModel.setFastBoot(it) }
                            }
                        }
                    }

                    // ── Disc Management (N64 / PS1) ─────────────────────────
                    if (systemName.contains("Nintendo 64") || systemName.contains("PlayStation")) {
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
                                SettingsSwitchItem("Expansion Pak", "Increase RDRAM to 8MB.", settings.n64ExpansionPak) { viewModel.setN64ExpansionPak(it) }
                                SettingsSwitchItem("CPU Recompiler", "Use JIT recompiler.", settings.n64Recompiler) { viewModel.setN64Recompiler(it) }
                                var pakExpanded by remember { mutableStateOf(false) }
                                ListItem(
                                    headlineContent = { Text("Controller Pak") },
                                    supportingContent = { Text("Peripheral for Player 1 (hot-swappable).") },
                                    trailingContent = {
                                        Box {
                                            TextButton(onClick = { pakExpanded = true }) { Text(settings.n64Pak) }
                                            DropdownMenu(expanded = pakExpanded, onDismissRequest = { pakExpanded = false }) {
                                                listOf("None", "Rumble Pak", "Controller Pak").forEach { pak ->
                                                    DropdownMenuItem(
                                                        text = { Text(pak) },
                                                        onClick = { viewModel.setN64Pak(pak); pakExpanded = false }
                                                    )
                                                }
                                            }
                                        }
                                    }
                                )
                                ListItem(
                                    headlineContent = { Text("N64 Experimental") },
                                    supportingContent = { Text("Overclocking, VI rendering, debug logging") },
                                    trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
                                    modifier = Modifier.clickable { experimentalOpen = true }
                                )
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

                    // ── On-Screen Keyboard (ZX Spectrum / 128) ─────────────
                    if (systemName.contains("ZX Spectrum", ignoreCase = true)) {
                        item {
                            MenuSection("Keyboard") {
                                SettingsSwitchItem(
                                    "On-Screen Keyboard",
                                    "Show the compact keyboard to type LOAD etc.",
                                    showKeyboard
                                ) { onKeyboardToggle(it) }
                                // ZX control scheme selector (0=Kempston,1=QAOP,2=ZXZX,3=ELITE,4=CUSTOM).
                                // CUSTOM enables long-press-to-rebind on the keyboard.
                                var schemeExpanded by remember { mutableStateOf(false) }
                                ListItem(
                                    headlineContent = { Text("Control Scheme") },
                                    supportingContent = { Text("Kempston / QAOP / ZXZX / ELITE / CUSTOM") },
                                    trailingContent = {
                                        Box {
                                            TextButton(onClick = { schemeExpanded = true }) {
                                                Text(when (zxControlScheme) {
                                                    1 -> "QAOP"; 2 -> "ZXZX"; 3 -> "ELITE"; 4 -> "CUSTOM"; else -> "KEMP"
                                                })
                                            }
                                            DropdownMenu(expanded = schemeExpanded, onDismissRequest = { schemeExpanded = false }) {
                                                listOf("KEMP" to 0, "QAOP" to 1, "ZXZX" to 2, "ELITE" to 3, "CUSTOM" to 4).forEach { (label, s) ->
                                                    DropdownMenuItem(
                                                        text = { Text(label) },
                                                        onClick = {
                                                            schemeExpanded = false
                                                            onZxControlScheme(s)
                                                        }
                                                    )
                                                }
                                            }
                                        }
                                    }
                                )
                                if (zxControlScheme == 4) {
                                    Text(
                                        "CUSTOM: long-press a key on the keyboard to bind it to a gamepad control.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
                                    )
                                }
                                // Keyboard opacity slider — makes the large on-screen
                                // keyboard less intrusive on small screens.
                                var kbdOpacity by remember { mutableStateOf(settings.zxKeyboardOpacity) }
                                LaunchedEffect(settings.zxKeyboardOpacity) { kbdOpacity = settings.zxKeyboardOpacity }
                                Text(
                                    "Keyboard Opacity: ${(kbdOpacity * 100).toInt()}%",
                                    style = MaterialTheme.typography.labelMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
                                )
                                Slider(
                                    value = kbdOpacity,
                                    onValueChange = {
                                        kbdOpacity = it
                                        viewModel.setZxKeyboardOpacity(it)
                                    },
                                    valueRange = 0.2f..1.0f,
                                    modifier = Modifier.padding(horizontal = 16.dp)
                                )
                                // Mute the loud tape-loading screech (game still
                                // receives the EAR bit — only the audio is silenced).
                                SettingsSwitchItem(
                                    "Mute Tape Audio",
                                    "Silence the loud tape-loading screech.",
                                    settings.zxTapeMuted
                                ) { viewModel.setZxTapeMuted(it) }
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
                }

                Spacer(Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    OutlinedButton(onClick = onQuit, modifier = Modifier.weight(1f)) { Text("Quit Game") }
                    OutlinedButton(onClick = onLibrary, modifier = Modifier.weight(1f)) { Text("Library") }
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

// ─── N64 Experimental sub-menu (pause menu) ──────────────────────────────────
// Shown in-place (replaces the pause list) with a back arrow, mirroring the
// "Experimental (N64 Vulkan)" section in Settings. Keeps the main pause menu
// clean while keeping every N64 tuning knob one tap away.

@Composable
fun ColumnScope.N64ExperimentalSection(
    viewModel: MainViewModel,
    settings: EmulatorSettings,
    onBack: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
        Text("N64 Experimental", style = MaterialTheme.typography.headlineSmall)
    }
    LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item {
            MenuSection("Rendering") {
                var upscaleExpanded by remember { mutableStateOf(false) }
                ListItem(
                    headlineContent = { Text("Internal Upscale") },
                    trailingContent = {
                        Box {
                            TextButton(onClick = { upscaleExpanded = true }) { Text("${settings.n64Upscale}x") }
                            DropdownMenu(expanded = upscaleExpanded, onDismissRequest = { upscaleExpanded = false }) {
                                listOf(1, 2, 4).forEach { factor ->
                                    DropdownMenuItem(
                                        text = { Text("${factor}x") },
                                        onClick = { viewModel.setN64Upscale(factor); upscaleExpanded = false }
                                    )
                                }
                            }
                        }
                    }
                )
                SettingsSwitchItem("Disable VI Process", "Bypass VI post-processing. May fix GPU shader errors.", settings.n64DisableVIProcessing) { viewModel.setN64DisableVIProcessing(it) }
                SettingsSwitchItem("Supersample Scanout", "Downscale internal upscale for native output.", settings.n64SupersampleScanout) { viewModel.setN64SupersampleScanout(it) }
                SettingsSwitchItem("Weave Deinterlace", "Blend deinterlace (needs Supersample OFF).", settings.n64WeaveDeinterlacing) { viewModel.setN64WeaveDeinterlacing(it) }
            }
        }
        item {
            MenuSection("Overclocking") {
                var overclockExpanded by remember { mutableStateOf(false) }
                ListItem(
                    headlineContent = { Text("VI Overclock") },
                    supportingContent = { Text("Run VI faster so games render above 50/60Hz (game logic speeds up too).") },
                    trailingContent = {
                        Box {
                            TextButton(onClick = { overclockExpanded = true }) { Text("${settings.n64ViOverclock / 100.0f}x") }
                            DropdownMenu(expanded = overclockExpanded, onDismissRequest = { overclockExpanded = false }) {
                                listOf(100, 125, 150, 175, 200).forEach { pct ->
                                    DropdownMenuItem(
                                        text = { Text("${pct / 100.0f}x") },
                                        onClick = { viewModel.setN64ViOverclock(pct); overclockExpanded = false }
                                    )
                                }
                            }
                        }
                    }
                )
                SettingsSwitchItem("Use default count per op", "Modifying this can change game timing. Lower values can overclock the game but cause instability.", settings.n64UseDefaultCountPerOp) { viewModel.setN64UseDefaultCountPerOp(it) }
                var countPerOpExpanded by remember { mutableStateOf(false) }
                val countPerOpEnabled = !settings.n64UseDefaultCountPerOp
                ListItem(
                    modifier = Modifier.alpha(if (countPerOpEnabled) 1f else 0.4f),
                    headlineContent = { Text("Count Per Operation") },
                    supportingContent = { Text("Default 2. 1 = overclock (may be unstable), 3 = underclock.") },
                    trailingContent = {
                        Box {
                            TextButton(enabled = countPerOpEnabled, onClick = { countPerOpExpanded = true }) { Text("${settings.n64CountPerOp}") }
                            DropdownMenu(expanded = countPerOpExpanded, onDismissRequest = { countPerOpExpanded = false }) {
                                listOf(1, 2, 3).forEach { v ->
                                    DropdownMenuItem(
                                        text = { Text("$v") },
                                        onClick = { viewModel.setN64CountPerOp(v); countPerOpExpanded = false }
                                    )
                                }
                            }
                        }
                    }
                )
                SettingsSwitchItem("Use default overclocking factor", "Modifying this overclocks the R4300 by a factor of 2 each increment. 0 is no overclock.", settings.n64UseDefaultCpuOverclock) { viewModel.setN64UseDefaultCpuOverclock(it) }
                var cpuOverclockExpanded by remember { mutableStateOf(false) }
                val cpuOverclockEnabled = !settings.n64UseDefaultCpuOverclock
                ListItem(
                    modifier = Modifier.alpha(if (cpuOverclockEnabled) 1f else 0.4f),
                    headlineContent = { Text("Overclocking Factor") },
                    supportingContent = { Text("Overclocks the R4300 by 2^factor (0 = none). Game logic faster at same frame rate.") },
                    trailingContent = {
                        Box {
                            TextButton(enabled = cpuOverclockEnabled, onClick = { cpuOverclockExpanded = true }) { Text("${settings.n64CpuOverclock}") }
                            DropdownMenu(expanded = cpuOverclockExpanded, onDismissRequest = { cpuOverclockExpanded = false }) {
                                listOf(0, 1, 2, 3, 4, 5).forEach { v ->
                                    DropdownMenuItem(
                                        text = { Text("$v") },
                                        onClick = { viewModel.setN64CpuOverclock(v); cpuOverclockExpanded = false }
                                    )
                                }
                            }
                        }
                    }
                )
            }
        }
        item {
            MenuSection("Diagnostics") {
                SettingsSwitchItem("N64 Debug Logging", "Per-second N64 PC + stall dumps to logcat.", settings.n64DebugLogging) { viewModel.setN64DebugLogging(it) }
            }
        }
    }
}
