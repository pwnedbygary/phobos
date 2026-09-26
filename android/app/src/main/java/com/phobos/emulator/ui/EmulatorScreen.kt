package com.phobos.emulator.ui

import android.app.Activity
import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.input.GameInputState
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.input.InputBindings
import com.phobos.emulator.input.comboUsesDpad
import com.phobos.emulator.input.mapKeyCodeToBit
import com.phobos.emulator.input.matchingHotkeys
import com.phobos.emulator.ui.hud.PerformanceHudOverlay
import com.phobos.emulator.ui.hud.hudConfig
import com.phobos.emulator.ui.touch.ButtonCluster
import com.phobos.emulator.ui.touch.TouchAction
import com.phobos.emulator.ui.touch.TouchControlsOverlay
import com.phobos.emulator.ui.touch.TouchFamily
import com.phobos.emulator.ui.touch.TouchLayoutCodec
import com.phobos.emulator.ui.touch.TouchLayoutEditor
import com.phobos.emulator.ui.touch.TouchLayouts
import com.phobos.emulator.ui.touch.isHidden
import com.phobos.emulator.ui.touch.touchLayoutKey
import kotlin.math.roundToInt

private val VOLUME_KEYS = setOf(KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE)

@Composable
fun EmulatorScreen(viewModel: MainViewModel, systemName: String, romName: String, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val isPaused by viewModel.isPaused.collectAsState()
    val isLoaded by viewModel.isLoaded.collectAsState()
    val perfStats by viewModel.perfStats.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val videoGeometry by viewModel.videoGeometry.collectAsState()

    var showQuitDialog by remember { mutableStateOf(false) }
    var editingTouchLayout by remember { mutableStateOf(false) }
    // A physical controller is in use: touch controls hide until the screen is touched again.
    var controllerActive by remember { mutableStateOf(false) }

    // ZX Spectrum on-screen keyboard state, hoisted here so it survives hiding the keyboard.
    var showKeyboard by remember { mutableStateOf(false) }
    var zxSymLatched by remember { mutableStateOf(false) }
    var zxCapsLatched by remember { mutableStateOf(false) }
    var zxTurboTape by remember { mutableStateOf(false) }
    // The saved scheme is what loadRom applied natively.
    val zxControlScheme = settings.zxControlScheme[systemName] ?: 0
    // ZX CUSTOM rebind target: keyboard key currently being bound (null = not rebinding).
    var zxRebindTarget by remember { mutableStateOf<String?>(null) }
    // Cancelling the quit dialog returns to where it was opened: the running game or the pause menu.
    var resumeOnQuitCancel by remember { mutableStateOf(true) }

    // The ViewModel init pushes the default before DataStore emits the persisted value.
    LaunchedEffect(settings.n64DebugLogging) { PhobosCore.setN64DebugLogging(settings.n64DebugLogging) }

    val focusRequester = remember { FocusRequester() }
    val view = LocalView.current
    val window = (view.context as? Activity)?.window

    var pressedKeys by remember { mutableStateOf(setOf<Int>()) }
    var fastForwardToggled by remember { mutableStateOf(false) }

    fun toggleFastForward() {
        fastForwardToggled = !fastForwardToggled
        PhobosCore.setFastForward(fastForwardToggled)
    }

    fun askToQuit() {
        resumeOnQuitCancel = !isPaused
        viewModel.setPause(true)
        showQuitDialog = true
    }

    /** Runs a hotkey on key down. Returns false when the action doesn't apply to this system. */
    fun runHotkey(action: String): Boolean {
        when (action) {
            HotkeyAction.PAUSE -> viewModel.togglePause()
            HotkeyAction.FAST_FORWARD_HOLD -> PhobosCore.setFastForward(true)
            HotkeyAction.FAST_FORWARD_TOGGLE -> toggleFastForward()
            HotkeyAction.SAVE_STATE -> viewModel.saveState(systemName, romName, currentSlot)
            HotkeyAction.LOAD_STATE -> viewModel.loadState(systemName, romName, currentSlot)
            HotkeyAction.NEXT_SLOT -> viewModel.incrementSlot()
            HotkeyAction.PREVIOUS_SLOT -> viewModel.decrementSlot()
            HotkeyAction.RESET -> viewModel.resetSystem()
            HotkeyAction.FRAME_ADVANCE -> if (isPaused) PhobosCore.frameAdvance()
            HotkeyAction.MUTE -> viewModel.setMuteAudio(!settings.muteAudio)
            HotkeyAction.SCREENSHOT -> viewModel.takeScreenshot(systemName, romName)
            HotkeyAction.RELOAD -> viewModel.roms.value.find { it.name == romName }?.let { viewModel.loadRom(view.context, systemName, it) }
            HotkeyAction.QUIT -> askToQuit()
            HotkeyAction.KEYBOARD -> if (systemName.contains("ZX Spectrum", ignoreCase = true)) showKeyboard = !showKeyboard else return false
            HotkeyAction.LIBRARY -> viewModel.swapToLibrary()
            HotkeyAction.PS1_ANALOG_TOGGLE -> if (systemName == "PlayStation") viewModel.togglePs1AnalogMode() else return false
            else -> return false
        }
        return true
    }

    // Controllers that report the D-pad as a hat send motion events, not keys, so combos that
    // include a D-pad direction are re-checked when the hat changes. Hold-to-fast-forward needs
    // a key release and is not supported from the hat.
    val onHatChanged = rememberUpdatedState<() -> Unit> {
        val pressed = pressedKeys + GameInputState.hotkeyKeys
        for (action in matchingHotkeys(settings.hotkeys, pressed)) {
            val combo = settings.hotkeys[action].orEmpty()
            if (action != HotkeyAction.FAST_FORWARD_HOLD && comboUsesDpad(combo)) runHotkey(action)
        }
    }
    DisposableEffect(Unit) {
        val hotkeyListener: () -> Unit = { onHatChanged.value() }
        val physicalListener: () -> Unit = { if (!controllerActive) controllerActive = true }
        GameInputState.onHotkeyKeysChanged = hotkeyListener
        GameInputState.onPhysicalInput = physicalListener
        onDispose {
            // An incoming screen may already have installed its own listeners.
            if (GameInputState.onHotkeyKeysChanged === hotkeyListener) GameInputState.onHotkeyKeysChanged = null
            if (GameInputState.onPhysicalInput === physicalListener) GameInputState.onPhysicalInput = null
        }
    }

    // ── Fullscreen ───────────────────────────────────────────────────────────
    DisposableEffect(settings.fullScreenMode) {
        if (settings.fullScreenMode && window != null) {
            val controller = WindowCompat.getInsetsController(window, view)
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        onDispose {
            if (window != null) WindowCompat.getInsetsController(window, view).show(WindowInsetsCompat.Type.systemBars())
        }
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────
    LaunchedEffect(isLoaded) {
        if (isLoaded) {
            viewModel.setPause(false)
            PhobosCore.setEmulationRunning(true)
            // Take focus right away so hardware keys (hotkeys, gamepad) reach onPreviewKeyEvent.
            focusRequester.requestFocus()
        }
    }
    LaunchedEffect(isPaused, editingTouchLayout) {
        if (isPaused) {
            GameInputState.releaseAllButtons()
            pressedKeys = emptySet() // Re-arm hotkey combos.
        } else if (isLoaded && !editingTouchLayout) {
            // Controller navigation of the pause menu moves focus into it; game input needs it back.
            focusRequester.requestFocus()
        }
    }
    BackHandler { if (!isLoaded || isPaused) askToQuit() }
    BackHandler(enabled = editingTouchLayout) { editingTouchLayout = false }

    DisposableEffect(Unit) {
        viewModel.setEmulatorScreenVisible(true)
        onDispose {
            viewModel.setEmulatorScreenVisible(false)
            GameInputState.reset()
            // fastForwardToggled starts false when the screen returns, so native must match.
            PhobosCore.setFastForward(false)
            // The game stays loaded (paused) when leaving the screen — the swap-screen hotkey
            // relies on it. Unloading only happens through the quit dialog.
        }
    }

    EmulatorDialogs(
        viewModel = viewModel,
        romName = romName,
        showQuitDialog = showQuitDialog,
        onQuitDismissed = { showQuitDialog = false; if (resumeOnQuitCancel) viewModel.setPause(false) },
        onQuitConfirmed = { showQuitDialog = false; viewModel.unloadSystem(); onBack() },
        onLeave = onBack,
    )

    // ── Touch layout for this system ─────────────────────────────────────────
    val touchPrefs = settings.touch
    val touchFamily = remember(systemName) { TouchFamily.of(systemName) }
    val touchLayout = remember(touchFamily, touchPrefs.showMenuButton, touchPrefs.showFastForwardButton, settings.ps1AnalogMode, settings.orientationVertical) {
        TouchLayouts.forFamily(
            touchFamily,
            TouchLayouts.Options(
                showMenu = touchPrefs.showMenuButton,
                showFastForward = touchPrefs.showFastForwardButton,
                ps1Analog = settings.ps1AnalogMode,
                wonderSwanVertical = settings.orientationVertical,
            ),
        )
    }
    val landscapeLayout = settings.touchLayouts[touchLayoutKey(touchFamily, landscape = true)]
    val portraitLayout = settings.touchLayouts[touchLayoutKey(touchFamily, landscape = false)]
    val landscapeOverrides = remember(landscapeLayout) { TouchLayoutCodec.decode(landscapeLayout) }
    val portraitOverrides = remember(portraitLayout) { TouchLayoutCodec.decode(portraitLayout) }
    val touchVisible = isLoaded && !isPaused && !editingTouchLayout && settings.showTouchControls &&
        !(touchPrefs.hideOnController && controllerActive)
    // Match TouchControlsOverlay: landscape vs portrait from this screen's own size,
    // not Configuration (which can disagree in split-screen / inset layouts).
    var landscapeScreen by remember { mutableStateOf(true) }
    // Without an on-screen menu button (touch controls off, or the button turned off or
    // hidden in the layout editor) a tap on the game opens the pause menu instead.
    val menuButtonShown = touchVisible && touchLayout.elements.any { element ->
        element is ButtonCluster && element.buttons.any { it.action == TouchAction.MENU } &&
            !isHidden(element, (if (landscapeScreen) landscapeOverrides else portraitOverrides)[element.id], landscapeScreen)
    }

    // ── Main container ───────────────────────────────────────────────────────
    Box(
        modifier = Modifier
            .fillMaxSize()
            .onSizeChanged { landscapeScreen = it.width >= it.height }
            .background(Color.Black)
            // Before focusable() so it observes this node's focus (hasFocus includes children).
            .onFocusChanged { if (!it.hasFocus) GameInputState.releaseAllButtons() }
            .focusRequester(focusRequester)
            .focusable()
            .onPreviewKeyEvent { keyEvent ->
                val native = keyEvent.nativeKeyEvent
                val keyCode = native.keyCode
                val isDown = keyEvent.type == KeyEventType.KeyDown
                // Leave volume keys to the system, and every key to the layout editor.
                if (keyCode in VOLUME_KEYS || editingTouchLayout) return@onPreviewKeyEvent false
                // Paused: let key repeats drive focus navigation in the pause menu.
                if (native.repeatCount > 0) return@onPreviewKeyEvent !isPaused

                // ZX CUSTOM rebinding captures the next controller button.
                val rebind = zxRebindTarget
                if (rebind != null && isDown) {
                    val bit = mapKeyCodeToBit(keyCode)
                    if (bit != 0) {
                        viewModel.setZxKeyBinding(systemName, rebind, bit)
                        zxRebindTarget = null
                        return@onPreviewKeyEvent true
                    }
                }

                pressedKeys = if (isDown) pressedKeys + keyCode else pressedKeys - keyCode

                // Hotkeys match on keys plus hat D-pad directions (GameInputState.hotkeyKeys),
                // so combos like Z + D-pad Right work on controllers with a hat D-pad.
                var consumed = false
                if (isDown) {
                    for (action in matchingHotkeys(settings.hotkeys, pressedKeys + GameInputState.hotkeyKeys)) {
                        if (runHotkey(action)) consumed = true
                    }
                } else if (!fastForwardToggled && settings.hotkeys[HotkeyAction.FAST_FORWARD_HOLD]?.contains(keyCode) == true) {
                    PhobosCore.setFastForward(false)
                }
                if (consumed) return@onPreviewKeyEvent true
                // Paused: mapped buttons navigate the menu (BUTTON_A falls back to DPAD_CENTER).
                if (isPaused) return@onPreviewKeyEvent false

                val bits = InputBindings.of(settings.inputMappings).bitsForKey(keyCode)
                if (bits != 0) {
                    GameInputState.setButton(bits, isDown)
                    return@onPreviewKeyEvent true
                }
                false
            }
            .onKeyEvent { keyEvent ->
                val keyCode = keyEvent.nativeKeyEvent.keyCode
                if (keyCode in VOLUME_KEYS) return@onKeyEvent false
                // While a game runs, swallow BACK so a gamepad B mapped to BACK doesn't open the
                // quit flow; when paused, let it through to the BackHandler.
                if (!isLoaded || isPaused) return@onKeyEvent false
                true
            }
    ) {
        GamePicture(
            viewModel = viewModel,
            settings = settings,
            geometry = videoGeometry,
            systemName = systemName,
            alignTop = settings.showTouchControls,
            onTap = {
                // With touch controls hidden for a controller, the first tap brings them back.
                if (controllerActive && settings.showTouchControls && touchPrefs.hideOnController) controllerActive = false
                else if (isLoaded && !menuButtonShown) viewModel.setPause(true)
            },
        )

        if (!isLoaded && !isPaused) LoadingOverlay(systemName)

        if (touchVisible) {
            TouchControlsOverlay(
                layout = touchLayout,
                landscapeOverrides = landscapeOverrides,
                portraitOverrides = portraitOverrides,
                prefs = touchPrefs,
                onAction = { action ->
                    when (action) {
                        TouchAction.MENU -> viewModel.setPause(true)
                        TouchAction.FAST_FORWARD -> toggleFastForward()
                        TouchAction.KEYBOARD -> showKeyboard = !showKeyboard
                        TouchAction.NONE -> {}
                    }
                },
                onBackgroundTap = { if (isLoaded && !menuButtonShown) viewModel.setPause(true) },
            )
        }

        // ── ZX Spectrum keyboard and tape progress ──────────────────────────
        val isZx = systemName.contains("ZX Spectrum", ignoreCase = true)
        if (isLoaded && showKeyboard && isZx) {
            ZXKeyboardOverlay(
                modifier = Modifier.align(Alignment.BottomCenter),
                symLatched = zxSymLatched, onSymLatched = { zxSymLatched = it },
                capsLatched = zxCapsLatched, onCapsLatched = { zxCapsLatched = it },
                turboTape = zxTurboTape, onTurboTape = { zxTurboTape = it },
                controlScheme = zxControlScheme, onControlScheme = { viewModel.setZxControlScheme(systemName, it) },
                rebindTarget = zxRebindTarget, onRebindTarget = { zxRebindTarget = it },
                boundKeys = (settings.zxKeyBindings[systemName] ?: emptyMap()).keys,
                keyboardOpacity = settings.zxKeyboardOpacity,
            )
        }
        if (isLoaded && isZx) {
            // Progress = 0..10000 (percent * 100); -1 = no tape playing.
            val tapeProgress by viewModel.zxTapeProgress.collectAsState()
            if (tapeProgress >= 0) {
                ZxTapeProgressBar(
                    percent = tapeProgress / 100f,
                    modifier = Modifier.align(Alignment.TopCenter).padding(top = 10.dp).fillMaxWidth(0.75f),
                )
            }
        }

        // ── Performance HUD (MangoHud-style, draggable) ─────────────────────
        if (isLoaded && !isPaused && settings.showPerformanceMonitor) {
            val metrics = LocalContext.current.resources.displayMetrics
            key(metrics.widthPixels, metrics.heightPixels) {
                PerformanceHudOverlay(
                    stats = perfStats,
                    config = settings.hudConfig(),
                    systemName = viewModel.loadedSystemName,
                    resolution = videoGeometry?.let { "${it.width.roundToInt()}\u00D7${it.height.roundToInt()}" },
                    savedPosX = settings.perfOverlayPosX,
                    savedPosY = settings.perfOverlayPosY,
                    screenWidth = metrics.widthPixels,
                    screenHeight = metrics.heightPixels,
                    onPositionChanged = { x, y ->
                        viewModel.setPerfOverlayPosX(x)
                        viewModel.setPerfOverlayPosY(y)
                    },
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }

        if (isPaused && !editingTouchLayout) {
            EmulationMenu(
                viewModel = viewModel, systemName = systemName, romName = romName,
                showKeyboard = showKeyboard,
                onKeyboardToggle = { showKeyboard = it },
                onResume = { viewModel.togglePause() },
                onQuit = { resumeOnQuitCancel = false; showQuitDialog = true },
                onLibrary = { viewModel.swapToLibrary() },
                onEditTouchLayout = { editingTouchLayout = true },
                zxControlScheme = zxControlScheme,
                onZxControlScheme = { scheme -> viewModel.setZxControlScheme(systemName, scheme) },
            )
        }

        if (editingTouchLayout) {
            TouchLayoutEditor(
                layout = touchLayout,
                prefs = touchPrefs,
                overridesFor = { landscape -> if (landscape) landscapeOverrides else portraitOverrides },
                onSave = { landscape, overrides ->
                    viewModel.saveTouchLayout(touchFamily, landscape, overrides)
                    editingTouchLayout = false
                },
                onCancel = { editingTouchLayout = false },
            )
        }
    }
}

/**
 * The emulator's SurfaceView, sized by the aspect-ratio setting from the core's reported
 * [geometry] (4:3 / 320x240 until the first frame). With touch controls enabled the picture sits
 * at the top in portrait so the controls get the lower part of the screen.
 */
@Composable
private fun GamePicture(
    viewModel: MainViewModel,
    settings: EmulatorSettings,
    geometry: VideoGeometry?,
    systemName: String,
    alignTop: Boolean,
    onTap: () -> Unit,
) {
    val currentOnTap by rememberUpdatedState(onTap)
    val density = LocalDensity.current
    // In full screen the status bar is hidden, so keep the picture clear of the camera cutout.
    val cutoutTop: Dp = if (settings.fullScreenMode) with(density) { WindowInsets.displayCutout.getTop(density).toDp() } else 0.dp

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) { detectTapGestures(onTap = { currentOnTap() }) },
    ) {
        val portrait = maxHeight > maxWidth
        val topInset = if (portrait && alignTop) cutoutTop else 0.dp
        val availableHeight = maxHeight - topInset
        val baseWidth = geometry?.width ?: 320f
        val baseHeight = geometry?.height ?: 240f
        val ratio = baseWidth / baseHeight
        val (width, height) = when (settings.aspectRatioMode) {
            AspectRatioMode.STRETCHED -> maxWidth to availableHeight
            AspectRatioMode.CORE_PROVIDED ->
                if (maxWidth / availableHeight > ratio) (availableHeight * ratio) to availableHeight
                else maxWidth to (maxWidth / ratio)
            AspectRatioMode.INTEGER_SCALED -> with(density) {
                // Whole multiples of the core's picture in physical pixels.
                val scale = maxOf(1, minOf(maxWidth.toPx() / baseWidth, availableHeight.toPx() / baseHeight).toInt())
                (baseWidth * scale).toDp() to (baseHeight * scale).toDp()
            }
        }

        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    setZOrderMediaOverlay(true)
                    isFocusable = false
                    setOnGenericMotionListener { _, event ->
                        GameInputState.handleMotionEvent(event, viewModel.settings.value.inputMappings, systemName)
                    }
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(h: SurfaceHolder) { PhobosCore.setSurface(h.surface) }
                        override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, h2: Int) { PhobosCore.setSurface(h.surface) }
                        override fun surfaceDestroyed(h: SurfaceHolder) { viewModel.setPause(true); PhobosCore.setSurface(null) }
                    })
                }
            },
            modifier = Modifier
                .align(if (portrait && alignTop) Alignment.TopCenter else Alignment.Center)
                .padding(top = topInset)
                .size(width, height),
        )
    }
}

@Composable
private fun LoadingOverlay(systemName: String) {
    Column(
        modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.8f)),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
        Spacer(modifier = Modifier.height(16.dp))
        Text("Initializing $systemName...", style = MaterialTheme.typography.bodyLarge, color = Color.White)
    }
}

