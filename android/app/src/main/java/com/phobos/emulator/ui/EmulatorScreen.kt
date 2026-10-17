package com.phobos.emulator.ui

import android.app.Activity
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.boundsInParent
import androidx.compose.ui.layout.onPlaced
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
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
import com.phobos.emulator.input.GameKeys
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.input.InputLog
import com.phobos.emulator.input.VOLUME_KEYS
import com.phobos.emulator.launch.LaunchSystems
import com.phobos.emulator.ui.hud.PerformanceHudOverlay
import com.phobos.emulator.ui.hud.hudConfig
import com.phobos.emulator.ui.hud.hudPlacement
import com.phobos.emulator.ui.touch.ButtonCluster
import com.phobos.emulator.ui.touch.TouchAction
import com.phobos.emulator.ui.touch.TouchControlsOverlay
import com.phobos.emulator.ui.touch.TouchControlsProbe
import com.phobos.emulator.ui.touch.TouchFamily
import com.phobos.emulator.ui.touch.TouchLayoutCodec
import com.phobos.emulator.ui.touch.TouchLayoutEditor
import com.phobos.emulator.ui.touch.TouchLayouts
import com.phobos.emulator.ui.touch.isHidden
import com.phobos.emulator.ui.touch.touchLayoutKey
import com.phobos.emulator.util.DisplayRefresh
import com.phobos.emulator.util.pictureMultiple
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

private const val MENU_REVEAL_MS = 3000L

@Composable
fun EmulatorScreen(viewModel: MainViewModel, systemName: String, romName: String, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val controls by viewModel.activeControls.collectAsState()
    val isPaused by viewModel.isPaused.collectAsState()
    val isLoaded by viewModel.isLoaded.collectAsState()
    val perfStats by viewModel.perfStats.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val videoGeometry by viewModel.videoGeometry.collectAsState()
    val pspDialog by viewModel.pspDialog.collectAsState()

    var showQuitDialog by remember { mutableStateOf(false) }
    var editingTouchLayout by remember { mutableStateOf(false) }
    // Moving or resizing the performance monitor on screen, with the game paused.
    var editingHud by remember { mutableStateOf(false) }
    // A physical controller is in use: touch controls hide until the screen is touched again.
    var controllerActive by remember { mutableStateOf(false) }

    // On-screen keyboard state (ZX Spectrum and MSX), hoisted here so it survives hiding the keyboard.
    val isZx = systemName.contains("ZX Spectrum", ignoreCase = true)
    val isMsx = systemName == "MSX" || systemName == "MSX2"
    val hasKeyboard = isZx || isMsx
    var showKeyboard by remember { mutableStateOf(false) }
    // The keyboard's height, kept clear of the game picture so the game stays in view above it.
    var keyboardHeight by remember { mutableStateOf(0.dp) }
    // Where the keyboard sits; the performance HUD stays above it.
    var keyboardBounds by remember { mutableStateOf(Rect.Zero) }
    val touchProbe = remember { TouchControlsProbe() }
    val density = LocalDensity.current
    var zxSymLatched by remember { mutableStateOf(false) }
    var zxCapsLatched by remember { mutableStateOf(false) }
    // The saved scheme is what loadRom applied natively.
    val zxControlScheme = viewModel.zxControlScheme(settings, systemName, romName)
    // ZX CUSTOM rebind target: keyboard key currently being bound (null = not rebinding).
    var zxRebindTarget by remember { mutableStateOf<String?>(null) }
    // Cancelling the quit dialog returns to where it was opened: the running game or the pause menu.
    var resumeOnQuitCancel by remember { mutableStateOf(true) }

    // The ViewModel init pushes the default before DataStore emits the persisted value.
    LaunchedEffect(settings.n64DebugLogging) { PhobosCore.setN64DebugLogging(settings.n64DebugLogging) }

    val focusRequester = remember { FocusRequester() }
    val view = LocalView.current
    val window = (view.context as? Activity)?.window
    // This screen, to the ViewModel: when a frontend starts a game over the running one, the new game's
    // screen arrives while this one is still leaving, and from then on this one leaves things to it.
    val screen = remember { Any() }
    fun takeFocus() { if (!viewModel.emulatorScreenReplaced(screen)) focusRequester.requestFocus() }

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
            HotkeyAction.RELOAD -> viewModel.reloadGame(view.context)
            HotkeyAction.QUIT -> askToQuit()
            HotkeyAction.KEYBOARD -> if (hasKeyboard) showKeyboard = !showKeyboard else return false
            HotkeyAction.LIBRARY -> viewModel.swapToLibrary()
            HotkeyAction.PS1_ANALOG_TOGGLE -> if (systemName == "PlayStation") viewModel.togglePs1AnalogMode() else return false
            else -> return false
        }
        return true
    }

    // Keys, the hat's hotkeys and focus go through GameKeys, remembered once: what it reads must be state, read at each
    // call, or fixed for the screen, as systemName and romName are (each game gets its own screen: MainScaffold pops
    // the old game's entry, with no launchSingleTop).
    val gameKeys = remember {
        GameKeys(object : GameKeys.Screen {
            override fun controls() = controls
            override fun paused() = isPaused
            override fun editing() = editingTouchLayout || editingHud
            override fun fastForwardToggled() = fastForwardToggled
            override fun zxKeyToBind() = zxRebindTarget
            override fun bindZxKey(key: String, bit: Int) {
                viewModel.setZxKeyBinding(systemName, key, bit)
                zxRebindTarget = null
            }
            override fun hotkey(action: String) = runHotkey(action)
            override fun stopFastForward() {
                PhobosCore.setFastForward(false)
            }
        })
    }
    DisposableEffect(Unit) {
        val hotkeyListener: () -> Unit = { gameKeys.onHatChanged() }
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
            // Back to the menus' state: the navigation bar, and the status bar unless Full Screen Mode hides it there too.
            if (window != null && !viewModel.emulatorScreenReplaced(screen)) {
                val controller = WindowCompat.getInsetsController(window, view)
                controller.show(WindowInsetsCompat.Type.navigationBars())
                if (!settings.fullScreenMode) controller.show(WindowInsetsCompat.Type.statusBars())
            }
        }
    }

    // Match the panel to the game's rate (e.g. 60 Hz on a 120 Hz display). Native
    // ANativeWindow_setFrameRate alone can leave the panel at 120 on some devices;
    // preferredDisplayModeId is what switches the mode there. Read the live native
    // hint (not polled perfStats), which unload resets and the core updates ASAP.
    val activity = view.context as? Activity
    var contentHz by remember { mutableStateOf(60.0) }
    LaunchedEffect(isLoaded) {
        if (!isLoaded || activity == null || viewModel.emulatorScreenReplaced(screen)) return@LaunchedEffect
        while (true) {
            val hz = PhobosCore.getRefreshRateHint()
            if (hz > 1.0) {
                contentHz = hz
                DisplayRefresh.preferContentRate(activity, hz)
            }
            delay(200)
        }
    }
    DisposableEffect(Unit) {
        onDispose {
            if (activity != null && !viewModel.emulatorScreenReplaced(screen)) {
                DisplayRefresh.clearPreferredDisplayMode(activity)
            }
        }
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────
    LaunchedEffect(isLoaded) {
        if (isLoaded && !viewModel.emulatorScreenReplaced(screen)) {
            InputLog.refresh()
            InputLog.note { "$systemName game, bindings ${controls.mappings}, hotkeys ${controls.hotkeys}" }
            viewModel.setPause(false)
            PhobosCore.setEmulationRunning(true)
            // Take focus right away so hardware keys (hotkeys, gamepad) reach onPreviewKeyEvent.
            takeFocus()
        }
    }
    LaunchedEffect(isPaused, editingTouchLayout) {
        if (isPaused) {
            GameInputState.releaseAllButtons()
            gameKeys.forgetPressedKeys() // Re-arm hotkey combos.
        } else if (isLoaded && !editingTouchLayout) {
            // Controller navigation of the pause menu moves focus into it; game input needs it back.
            takeFocus()
        }
    }
    BackHandler { if (!isLoaded || isPaused) askToQuit() }
    BackHandler(enabled = editingTouchLayout) { editingTouchLayout = false }
    BackHandler(enabled = editingHud) {
        editingHud = false
        viewModel.setPause(false)
    }

    DisposableEffect(Unit) {
        // Replacing another game's screen, which leaves input to this one once replaced: start from
        // released input, as that screen's exit would have left it.
        if (viewModel.emulatorScreenVisible.value) {
            GameInputState.reset()
            PhobosCore.setFastForward(false)
        }
        viewModel.emulatorScreenShown(screen)
        onDispose {
            if (!viewModel.emulatorScreenReplaced(screen)) {
                GameInputState.reset()
                // fastForwardToggled starts false when the screen returns, so native must match.
                PhobosCore.setFastForward(false)
            }
            viewModel.emulatorScreenGone(screen)
            // The game stays loaded (paused) when leaving the screen — the swap-screen hotkey
            // relies on it. Unloading only happens through the quit dialog.
        }
    }

    // The PSP's system dialog the game waits on (a message, the keyboard, saving or loading), over the running game;
    // hidden while the pause menu (which its pause hotkey opens) or a layout editor is up. The game's buttons are let
    // go of as it opens, and the game takes the controller back once it's answered.
    val shownPspDialog = pspDialog?.takeIf { isLoaded && !isPaused && !editingTouchLayout && !editingHud }
    LaunchedEffect(shownPspDialog?.id) {
        if (shownPspDialog != null) {
            GameInputState.releaseAllButtons()
            gameKeys.forgetPressedKeys()
        } else if (isLoaded && !isPaused) {
            takeFocus()
        }
    }
    shownPspDialog?.let { request ->
        PspSystemDialog(request, controls, settings.fullScreenMode, onPause = { viewModel.setPause(true) }) {
            viewModel.answerPspDialog(it)
        }
    }

    EmulatorDialogs(
        viewModel = viewModel,
        romName = romName,
        showQuitDialog = showQuitDialog,
        onQuitDismissed = { showQuitDialog = false; if (resumeOnQuitCancel) viewModel.setPause(false) },
        // A game a frontend started returns to it; otherwise back to the Library.
        onQuitConfirmed = { showQuitDialog = false; viewModel.unloadSystem(); if (!viewModel.leaveGame()) onBack() },
        onLeave = { if (!viewModel.leaveGame()) onBack() },
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
    // hidden in the layout editor) a tap on the game reveals one for a few seconds instead.
    // Only that button opens the pause menu, so a thumb brushing the screen can't.
    val menuButtonShown = touchVisible && touchLayout.elements.any { element ->
        element is ButtonCluster && element.buttons.any { it.action == TouchAction.MENU } &&
            !isHidden(element, (if (landscapeScreen) landscapeOverrides else portraitOverrides)[element.id], landscapeScreen)
    }
    var menuRevealed by remember { mutableStateOf(false) }
    var menuRevealCount by remember { mutableIntStateOf(0) }
    fun revealMenuButton() { menuRevealed = true; menuRevealCount++ }
    LaunchedEffect(menuRevealCount) {
        if (menuRevealed) { delay(MENU_REVEAL_MS); menuRevealed = false }
    }
    LaunchedEffect(isPaused) { if (isPaused) menuRevealed = false }

    val pictureBounds = remember { PictureBounds() }

    // ── Main container ───────────────────────────────────────────────────────
    Box(
        modifier = Modifier
            .fillMaxSize()
            .onPlaced { pictureBounds.screen = it }
            .onSizeChanged { landscapeScreen = it.width >= it.height }
            .background(Color.Black)
            // Before focusable() so it observes this node's focus (hasFocus includes children).
            .onFocusChanged { gameKeys.onFocusChanged(it) }
            .focusRequester(focusRequester)
            .focusable()
            .onPreviewKeyEvent { keyEvent ->
                val native = keyEvent.nativeKeyEvent
                InputLog.key("game", native)
                gameKeys.onKey(native.keyCode, keyEvent.type == KeyEventType.KeyDown, native.repeatCount)
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
            contentHz = contentHz,
            alignTop = settings.showTouchControls,
            bottomReserve = if (isLoaded && showKeyboard && hasKeyboard) keyboardHeight else 0.dp,
            bounds = pictureBounds,
            onTap = {
                // With touch controls hidden for a controller, the first tap brings them back.
                if (controllerActive && settings.showTouchControls && touchPrefs.hideOnController) controllerActive = false
                else if (isLoaded && !menuButtonShown) revealMenuButton()
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
                onBackgroundTap = { if (isLoaded && !menuButtonShown) revealMenuButton() },
                ownsInput = { !viewModel.emulatorScreenReplaced(screen) },
                probe = touchProbe,
            )
        }

        // ── ZX Spectrum keyboard, whose rainbow stripe shows the tape's progress and controls ──
        val zxTape by viewModel.zxTape.collectAsState()
        if (isLoaded && showKeyboard && isZx) {
            ZXKeyboardOverlay(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .onSizeChanged { keyboardHeight = with(density) { it.height.toDp() } }
                    .onPlaced { keyboardBounds = it.boundsInParent() }
                    .then(if (isPaused) Modifier else Modifier.systemGestureExclusion()),
                symLatched = zxSymLatched, onSymLatched = { zxSymLatched = it },
                capsLatched = zxCapsLatched, onCapsLatched = { zxCapsLatched = it },
                loadSpeed = settings.zxLoadSpeed, onLoadSpeed = { viewModel.setZxLoadSpeed(it) },
                controlScheme = zxControlScheme, onControlScheme = { viewModel.setZxControlScheme(systemName, romName, it) },
                rebindTarget = zxRebindTarget, onRebindTarget = { zxRebindTarget = it },
                onClose = { showKeyboard = false },
                boundKeys = (settings.zxKeyBindings[systemName] ?: emptyMap()).keys,
                keyboardOpacity = settings.zxKeyboardOpacity,
                tape = zxTape,
                onTapePlaying = { viewModel.setZxTapePlaying(it) },
                onTapeRewind = { viewModel.rewindZxTape() },
                onTapeLoad = { viewModel.playZxTapeFromStart() },
            )
        }
        val msxTape by viewModel.msxTape.collectAsState()
        if (isLoaded && showKeyboard && isMsx) {
            MSXKeyboardOverlay(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .onSizeChanged { keyboardHeight = with(density) { it.height.toDp() } }
                    .onPlaced { keyboardBounds = it.boundsInParent() }
                    .then(if (isPaused) Modifier else Modifier.systemGestureExclusion()),
                msx2 = systemName == "MSX2",
                onClose = { showKeyboard = false },
                keyboardOpacity = settings.zxKeyboardOpacity,
                tape = msxTape.tape,
                recording = msxTape.recording,
                onTapeRewind = { viewModel.rewindMsxTape() },
            )
        }

        // ── Performance HUD (MangoHud-style); press and hold it to move or resize it ──
        if (isLoaded && (!isPaused || editingHud) && settings.showPerformanceMonitor) {
            PerformanceHudOverlay(
                stats = perfStats,
                config = settings.hudConfig(),
                placement = settings.hudPlacement(),
                systemName = viewModel.loadedSystemName,
                resolution = videoGeometry?.let { "${it.width.roundToInt()}\u00D7${it.height.roundToInt()}" },
                editing = editingHud,
                onEditRequest = {
                    if (!isPaused) {
                        editingHud = true
                        viewModel.setPause(true)
                    }
                },
                onEditDone = { edit ->
                    edit?.let { viewModel.saveHudEdit(it) }
                    editingHud = false
                    viewModel.setPause(false)
                },
                modifier = Modifier.fillMaxSize(),
                isControlAt = { p -> touchProbe.isControlAt(p.x, p.y) },
                bottomLimit = if (showKeyboard && hasKeyboard && !keyboardBounds.isEmpty) keyboardBounds.top else Float.POSITIVE_INFINITY,
            )
        }

        AnimatedVisibility(
            visible = menuRevealed && isLoaded && !isPaused && !editingTouchLayout && !menuButtonShown,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top))
                .padding(top = 16.dp),
        ) {
            MenuRevealButton(onOpenMenu = { menuRevealed = false; viewModel.setPause(true) })
        }

        if (isPaused && !editingTouchLayout && !editingHud) {
            EmulationMenu(
                viewModel = viewModel, systemName = systemName, romName = romName,
                showKeyboard = showKeyboard,
                onKeyboardToggle = { showKeyboard = it },
                onResume = { viewModel.togglePause() },
                onQuit = { resumeOnQuitCancel = false; showQuitDialog = true },
                onLibrary = { viewModel.swapToLibrary() },
                onEditTouchLayout = { editingTouchLayout = true },
                zxControlScheme = zxControlScheme,
                onZxControlScheme = { scheme -> viewModel.setZxControlScheme(systemName, romName, scheme) },
            )
        }

        ScreenshotEffect(viewModel.screenshotEvents, pictureBounds, touchControlsShown = touchVisible)

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
 * at the top in portrait so the controls get the lower part of the screen. [bottomReserve] is
 * kept free below the picture, which then sits at the top (the ZX Spectrum or MSX keyboard).
 * Where it's placed goes in [bounds], for the screenshot effect.
 */
@Composable
private fun GamePicture(
    viewModel: MainViewModel,
    settings: EmulatorSettings,
    geometry: VideoGeometry?,
    systemName: String,
    contentHz: Double,
    alignTop: Boolean,
    bottomReserve: Dp,
    bounds: PictureBounds,
    onTap: () -> Unit,
) {
    val currentOnTap by rememberUpdatedState(onTap)
    val currentContentHz by rememberUpdatedState(contentHz)
    val density = LocalDensity.current
    // In full screen the status bar is hidden, so keep the picture clear of the camera cutout.
    val cutoutTop: Dp = if (settings.fullScreenMode) with(density) { WindowInsets.displayCutout.getTop(density).toDp() } else 0.dp

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) { detectTapGestures(onTap = { currentOnTap() }) },
    ) {
        val portrait = maxHeight > maxWidth
        val atTop = (portrait && alignTop) || bottomReserve > 0.dp
        val topInset = if (atTop) cutoutTop else 0.dp
        val availableHeight = maxOf(maxHeight - topInset - bottomReserve, 1.dp)
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
        // The PSP's picture is drawn at the whole multiple of its size nearest the view's, so the compositor's own
        // (bilinear) scaling of it is slight and its one-pixel lines stay sharp and even (util/PspVideo.kt).
        val multiple = if (systemName == LaunchSystems.PSP) {
            with(density) { pictureMultiple(baseWidth, baseHeight, width.toPx(), height.toPx()) }
        } else 1
        LaunchedEffect(multiple) { PhobosCore.setPictureMultiple(multiple) }

        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    setZOrderMediaOverlay(true)
                    isFocusable = false
                    setOnGenericMotionListener { _, event ->
                        GameInputState.handleMotionEvent(
                            event, viewModel.activeControls.value.mappings, systemName, viewModel.settings.value.pspRightStick,
                        )
                    }
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(h: SurfaceHolder) {
                            DisplayRefresh.setSurfaceFrameRate(h.surface, currentContentHz)
                            PhobosCore.attachSurface(h.surface)
                        }
                        override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, h2: Int) {
                            DisplayRefresh.setSurfaceFrameRate(h.surface, currentContentHz)
                            PhobosCore.refreshSurface(h.surface)
                        }
                        override fun surfaceDestroyed(h: SurfaceHolder) {
                            if (PhobosCore.drawsTo(h.surface)) {
                                DisplayRefresh.clearSurfaceFrameRate(h.surface)
                                viewModel.setPause(true)
                                PhobosCore.detachSurface(h.surface)
                            }
                        }
                    })
                }
            },
            update = { view ->
                view.holder.surface?.takeIf { it.isValid }?.let { DisplayRefresh.setSurfaceFrameRate(it, contentHz) }
            },
            modifier = Modifier
                .align(if (atTop) Alignment.TopCenter else Alignment.Center)
                .padding(top = topInset)
                .size(width, height)
                .onPlaced { bounds.picture = it },
        )
    }
}

/** Opens the pause menu for a game without an on-screen menu button; shown briefly after a tap on the game. */
@Composable
private fun MenuRevealButton(onOpenMenu: () -> Unit) {
    val currentOnOpenMenu by rememberUpdatedState(onOpenMenu)
    Row(
        modifier = Modifier
            .clip(CircleShape)
            .background(Color.Black.copy(alpha = 0.6f))
            .border(1.dp, Color.White.copy(alpha = 0.4f), CircleShape)
            .semantics(mergeDescendants = true) {
                role = Role.Button
                onClick { currentOnOpenMenu(); true }
            }
            .pointerInput(Unit) { detectTapGestures(onTap = { currentOnOpenMenu() }) }
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Rounded.Menu, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
        Spacer(modifier = Modifier.width(8.dp))
        Text("Menu", color = Color.White, style = MaterialTheme.typography.labelLarge)
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

