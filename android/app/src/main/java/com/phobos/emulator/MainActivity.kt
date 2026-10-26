package com.phobos.emulator

import android.animation.ValueAnimator
import android.hardware.input.InputManager
import android.os.Bundle
import android.os.SystemClock
import android.net.Uri
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.view.accessibility.AccessibilityManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import coil.Coil
import coil.ImageLoader
import coil.decode.SvgDecoder
import com.phobos.emulator.data.CrtBackdropScene
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.GlassBackdropScene
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.MangaBackdropScene
import com.phobos.emulator.data.PixelBackdropScene
import com.phobos.emulator.data.RetrowaveBackdropScene
import com.phobos.emulator.data.RpgBackdropScene
import com.phobos.emulator.data.SettingsStore
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.data.XmbBackdropScene
import com.phobos.emulator.input.FallbackKeys
import com.phobos.emulator.input.GameInputState
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.input.InputLog
import com.phobos.emulator.input.KeyHandler
import com.phobos.emulator.input.ScreensaverKeys
import com.phobos.emulator.input.WindowKeys
import com.phobos.emulator.input.isSystemBack
import com.phobos.emulator.launch.launchRequestOf
import com.phobos.emulator.ui.ConsoleArtFetcher
import com.phobos.emulator.ui.ConsoleArtKeyer
import com.phobos.emulator.ui.IntroKeys
import com.phobos.emulator.ui.IntroSplash
import com.phobos.emulator.ui.introPlays
import com.phobos.emulator.ui.MainScaffold
import com.phobos.emulator.ui.MainViewModel
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.ui.theme.PhobosTheme
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull

private data class ThemeSelection(
    val id: String,
    val effects: UiEffects,
    val glass: GlassEffects,
    val pixelBackdrop: PixelBackdropScene,
    val mangaBackdrop: MangaBackdropScene,
    val rpgBackdrop: RpgBackdropScene,
    val retrowaveBackdrop: RetrowaveBackdropScene,
    val crtBackdrop: CrtBackdropScene,
    val glassBackdrop: GlassBackdropScene,
    val xmbBackdrop: XmbBackdropScene,
)

private fun EmulatorSettings.themeSelection() = ThemeSelection(
    themeId, uiEffects, glassEffects,
    pixelBackdrop, mangaBackdrop, rpgBackdrop, retrowaveBackdrop, crtBackdrop, glassBackdrop, xmbBackdrop,
)

private const val STARTUP_SETTINGS_TIMEOUT_MS = 500L

class MainActivity : ComponentActivity() {

    private lateinit var settingsStore: SettingsStore
    private lateinit var viewModel: MainViewModel

    // Debug/test harness scope for the adb-driven ROM loader (intent extras).
    private val debugScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val windowKeys = WindowKeys(object : WindowKeys.App {
        override fun gameKeys(): KeyHandler? =
            viewModel.gameKeys?.takeIf { viewModel.emulatorScreenVisible.value && viewModel.isLoaded.value }
        override fun gameKeysHolding(keyCode: Int): KeyHandler? = viewModel.gameKeys?.takeIf { it.holdsKey(keyCode) }
        override fun swappedAway() =
            viewModel.isLoaded.value && viewModel.isPaused.value && !viewModel.emulatorScreenVisible.value
        override fun gameScreenShown() = viewModel.emulatorScreenVisible.value
        override fun hotkeys() = viewModel.activeControls.value.hotkeys
        override fun heldAxes(): Set<Int> = GameInputState.hotkeyKeys
        override fun swapBackToGame() = viewModel.swapBackToGame()
        override fun showScreensaver() = viewModel.showScreensaver()
        override fun pagesTakeBack() = onBackPressedDispatcher.hasEnabledCallbacks()
    })
    private val fallbackKeys = FallbackKeys()
    private val screensaverKeys = ScreensaverKeys()

    private val inputDeviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) {}
        override fun onInputDeviceChanged(deviceId: Int) {}
        override fun onInputDeviceRemoved(deviceId: Int) {
            InputLog.note { "device $deviceId removed" }
            windowKeys.reset()
            screensaverKeys.reset()
            viewModel.gameKeys?.forgetPressedKeys()
            GameInputState.releaseAllButtons()
        }
    }

    // The intro over the Library as the app opens (IntroSplash): whether it's playing, whether a key or a touch has
    // skipped it, and the keys it takes meanwhile.
    private var introPlaying by mutableStateOf(false)
    private var introSkipped by mutableStateOf(false)
    private val introKeys = IntroKeys()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Edge to edge on every version, as Android 15 enforces: pages scroll under the floating dock
        // and the navigation bar, and the Scaffolds pad for the system bars themselves.
        WindowCompat.setDecorFitsSystemWindows(window, false)

        val imageLoader = ImageLoader.Builder(this)
            .components {
                add(SvgDecoder.Factory())
                add(ConsoleArtKeyer())
                add(ConsoleArtFetcher.Factory())
            }
            .build()
        Coil.setImageLoader(imageLoader)
        
        settingsStore = SettingsStore(this)
        // A game another app (a frontend) asked for. Not when the activity is recreated after the process
        // died: the old request's file grant is gone, and the Library is shown instead.
        val launch = if (savedInstanceState == null) intent?.let(::launchRequestOf) else null
        val debugLoad = savedInstanceState == null && isDebugLoad(intent)
        // The stored settings, read once before the first frame (bounded), so the window starts in the
        // saved full-screen state and the first frame already has the saved theme. A launch straight
        // into a game starts with both bars hidden, as the game screen will.
        val startupSettings = runBlocking { withTimeoutOrNull(STARTUP_SETTINGS_TIMEOUT_MS) { settingsStore.settings.first() } }
        if (startupSettings?.fullScreenMode == true) {
            WindowCompat.getInsetsController(window, window.decorView).apply {
                systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                hide(if (launch != null || debugLoad) WindowInsetsCompat.Type.systemBars() else WindowInsetsCompat.Type.statusBars())
            }
        }
        introPlaying = introPlays(
            freshStart = savedInstanceState == null,
            gameRequested = launch != null || debugLoad,
            // (settings not read in time may have it off)
            settingOn = startupSettings?.introAnimation == true,
            animationsOn = ValueAnimator.areAnimatorsEnabled(),
            touchExploration = getSystemService(AccessibilityManager::class.java)?.isTouchExplorationEnabled == true,
        )
        viewModel = ViewModelProvider(this, object : ViewModelProvider.Factory {
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return MainViewModel(applicationContext, settingsStore, startupSettings ?: EmulatorSettings()) as T
            }
        })[MainViewModel::class.java]

        lifecycle.addObserver(viewModel)

        setContent {
            // viewModel.settings starts from defaults if the startup read timed out; waiting for the
            // stored theme then keeps launches from flashing, and animating away from, the default theme.
            val theme by remember {
                settingsStore.settings
                    .map { it.themeSelection() }
                    .distinctUntilChanged()
            }.collectAsState(initial = startupSettings?.themeSelection())

            Box(Modifier.fillMaxSize()) {
                theme?.let {
                    PhobosTheme(
                        themeId = it.id,
                        effects = it.effects,
                        glassEffects = it.glass,
                        pixelBackdrop = it.pixelBackdrop,
                        mangaBackdrop = it.mangaBackdrop,
                        rpgBackdrop = it.rpgBackdrop,
                        retrowaveBackdrop = it.retrowaveBackdrop,
                        crtBackdrop = it.crtBackdrop,
                        glassBackdrop = it.glassBackdrop,
                        xmbBackdrop = it.xmbBackdrop,
                    ) {
                        MainScaffold(viewModel = viewModel, introPlaying = { introPlaying })
                    }
                }
                if (introPlaying) {
                    IntroSplash(
                        skipped = introSkipped,
                        onSkip = { introSkipped = true },
                        onDone = { introPlaying = false },
                    )
                }
            }
        }

        // A controller gone with keys down never sends their releases: forget them, or a held modifier would keep
        // holding the game's buttons back and a held button would stay down for the game.
        getSystemService(InputManager::class.java).registerInputDeviceListener(inputDeviceListener, null)

        // Every key event of the window, before any view sees it: ViewRootImpl consults Window.Callback before
        // dispatching to the focused view, so the wrapper below gets each one whichever view has focus. While a game
        // is on screen, WindowKeys hands them all to its GameKeys; away from it, it watches for the hotkeys that run
        // there: Show screensaver's, and the Library hotkey that brings a game back from behind the Library.
        // (ComponentActivity restricts overriding dispatchKeyEvent itself, hence the callback wrapper.)
        val originalCallback = window.callback
        window.callback = object : android.view.Window.Callback by originalCallback {
            override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                InputLog.key("window", event)
                // Two keys Android makes up aren't a controller's. A fallback (the Back an unhandled B gives, say,
                // marked or not: FallbackKeys) follows a key the capture and the hotkeys have had already, and goes to
                // neither; the navigation bar's or gesture's Back cancels a capture and goes to no hotkey. With no
                // predictive back (the manifest), both come here as keys.
                val fallback = fallbackKeys.isFallback(event.keyCode, event.deviceId, event.scanCode, event.downTime, event.flags)
                val systemBack = isSystemBack(event.keyCode, event.deviceId)
                // A controller that sends Back, which starts the default hotkeys, is remembered (the Hotkeys page warns
                // while no controller connected ever has); neither of those Backs is a controller's.
                if (event.keyCode == KeyEvent.KEYCODE_BACK && event.action == KeyEvent.ACTION_DOWN && !fallback && !systemBack) {
                    event.device?.descriptor?.let(viewModel::noteBackPress)
                }
                // The repeats and release of a key the window's hotkeys took are theirs (WindowKeys), the screensaver
                // up or not: the keys that brought it up don't put it down, and WindowKeys sees them let go of. One it
                // gives back, a binding's first key pressed alone, goes to the pages now, its press and release
                // together.
                val down = event.action == KeyEvent.ACTION_DOWN
                val repeatOrRelease = event.action == KeyEvent.ACTION_UP || (down && event.repeatCount > 0)
                if (repeatOrRelease && !fallback && !systemBack && windowKeys.holds(event.keyCode)) {
                    windowKeys.onKey(event.keyCode, down, event.repeatCount)
                    val replay = windowKeys.takeReplay() == event.keyCode && !event.isCanceled
                    if (replay && !viewModel.screensaverActive.value) {
                        InputLog.note { "window gives back ${event.keyCode}" }
                        toPages(KeyEvent.changeAction(event, KeyEvent.ACTION_DOWN))
                        toPages(event)
                    }
                    return true
                }
                viewModel.noteUserActivity()
                // While the intro plays every key is its own and skips it (IntroKeys), the volume keys aside.
                if (event.action != KeyEvent.ACTION_MULTIPLE &&
                    introKeys.onKey(event.keyCode, event.action == KeyEvent.ACTION_DOWN, event.repeatCount,
                        event.eventTime, introPlaying) { introSkipped = true }
                ) return true
                // A press being captured for a binding goes nowhere else — including the screensaver.
                if (event.action != KeyEvent.ACTION_MULTIPLE && !fallback &&
                    viewModel.controlCapture.onKey(event.keyCode, event.action == KeyEvent.ACTION_DOWN, event.repeatCount, systemBack)
                ) return true
                // While the screensaver shows, every key puts it away and goes nowhere else, and so does every key
                // pressed while one of those is still held, each until it's let go of (ScreensaverKeys): the rest of a
                // combo pressed to put it away reaches neither the pages nor, as a fallback, the focused item.
                val showing = viewModel.screensaverActive.value
                if (event.action == KeyEvent.ACTION_MULTIPLE) {
                    if (showing) return true
                } else if (screensaverKeys.onKey(event.keyCode, down, event.repeatCount, showing)) {
                    // A key the window's hotkeys hold isn't pressed alone any more either.
                    if (down && event.repeatCount == 0) windowKeys.pressedBeside()
                    return true
                }
                // A key the hotkeys here don't see (a D-pad direction Android makes from a hat, a fallback, the
                // navigation bar's Back): a key they took and hold isn't pressed alone.
                if ((fallback || systemBack) && down && event.repeatCount == 0) windowKeys.pressedBeside()
                if (event.action != KeyEvent.ACTION_MULTIPLE && !fallback && !systemBack &&
                    windowKeys.onKey(event.keyCode, event.action == KeyEvent.ACTION_DOWN, event.repeatCount)
                ) return true
                return toPages(event)
            }

            // A key for the pages: L1/R1 outside the game step through the Library, Console and Settings tabs
            // (MainScaffold), unless the Library hotkey uses them; every other key goes to the views.
            private fun toPages(event: KeyEvent): Boolean {
                val libraryKeys = viewModel.activeControls.value.hotkeys[HotkeyAction.LIBRARY].orEmpty().flatten()
                val shoulder = event.keyCode == KeyEvent.KEYCODE_BUTTON_L1 || event.keyCode == KeyEvent.KEYCODE_BUTTON_R1
                if (!viewModel.emulatorScreenVisible.value && shoulder && event.keyCode !in libraryKeys) {
                    if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                        viewModel.stepTab(if (event.keyCode == KeyEvent.KEYCODE_BUTTON_L1) -1 else 1)
                    }
                    return true
                }
                return originalCallback.dispatchKeyEvent(event)
            }
        }

        // In touch mode (after a touch, say), Android keeps the first D-pad press, a hat's included, to leave it,
        // so the window never has it: touch mode ending is that press. The intro's keys count it (IntroKeys), and a
        // key the window's hotkeys hold isn't pressed alone any more (WindowKeys). Not as the window gains focus,
        // when Android brings it into line with the rest of the screen (hasWindowFocus is true only after that).
        window.decorView.viewTreeObserver.addOnTouchModeChangeListener { inTouchMode ->
            if (!inTouchMode && window.decorView.hasWindowFocus()) {
                introKeys.onUnseenPress(introPlaying, SystemClock.uptimeMillis(),
                    ViewConfiguration.getKeyRepeatTimeout().toLong()) { introSkipped = true }
                windowKeys.pressedBeside()
            }
        }

        // Quitting a game a frontend started (or a launch that can't go ahead) returns to the frontend.
        lifecycleScope.launch { viewModel.leaveToFrontend.collect { finishAndRemoveTask() } }

        if (savedInstanceState == null) handleDebugLoadIntent(intent)
        launch?.let(viewModel::launchExternal)
    }

    // MainActivity is singleTask, so a frontend's launch while Phobos runs arrives here.
    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        // A game asked for while the intro plays goes ahead without it.
        if (launchRequestOf(intent) != null || isDebugLoad(intent)) introPlaying = false
        setIntent(intent)
        handleDebugLoadIntent(intent)
        launchRequestOf(intent)?.let(viewModel::launchExternal)
    }

    override fun onDestroy() {
        getSystemService(InputManager::class.java).unregisterInputDeviceListener(inputDeviceListener)
        debugScope.cancel()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        InputLog.note { "window focus $hasFocus" }
    }

    private fun isDebugLoad(intent: android.content.Intent?): Boolean = intent?.getStringExtra("load_uri") != null

    /**
     * Debug/test harness: load a ROM directly from adb without UI interaction.
     *
     * Usage:
     *   adb shell am start -n com.phobos.emulator/.MainActivity \
     *     --es load_uri "file:///storage/EBFF-F6C0/ROMs/tg16/Final Lap Twin (USA).zip" \
     *     --es load_name "Final Lap Twin (USA).zip" \
     *     --es load_system "PC Engine"
     *
     * Waits (bounded) for native asset extraction and the stored settings before
     * loading, so cold-start intent loads behave like UI loads.
     */
    private fun handleDebugLoadIntent(intent: android.content.Intent?) {
        if (intent == null || intent.flags and android.content.Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY != 0) return
        // Debug: dump NGCD graphics memories (sprite/fix/vram/pram) to filesDir.
        // Usage: adb shell am start -n com.phobos.emulator/.MainActivity --ez dump_ng_gfx true
        if (intent.getBooleanExtra("dump_ng_gfx", false)) {
            PhobosCore.dumpNgGfx(filesDir.absolutePath)
            Log.i("Phobos", "dumpNgGfx: wrote to ${filesDir.absolutePath}")
            return
        }
        val uri = intent.getStringExtra("load_uri") ?: return
        val name = intent.getStringExtra("load_name") ?: return
        val system = intent.getStringExtra("load_system") ?: return
        debugScope.launch {
            viewModel.awaitLaunchReady()
            Log.d("Phobos", "debugLoad: loading system='$system', rom='$name'")
            val rom = viewModel.namedFor(system, RomFile(name, Uri.parse(uri)))
            viewModel.loadRom(applicationContext, system, rom)
            // Mirror the UI tap flow (SystemDetailScreen loads THEN navigates)
            // so the game is visible on the emulator screen instead of running
            // headless behind the library.
            viewModel.navigateTo("emulator/${Uri.encode(system)}/${Uri.encode(rom.name)}")
        }
    }

    /**
     * Joystick motion is dispatched here before it reaches the view hierarchy.
     *
     * A capture for a binding ([MainViewModel.controlCapture]) takes every joystick move while it
     * runs. Otherwise the view tree gets the event first (via super) so the emulator's SurfaceView
     * listener can process its own events. Previously this method consumed every joystick move
     * and returned true, which silently swallowed those events, and in-game stick handling
     * bypassed the user's axis mappings (raw AXIS_X/Y/Z/RZ was hardcoded).
     *
     * Only when the emulator core is loaded AND no view handled the event do we
     * translate the motion event ourselves. Everything is routed through
     * [GameInputState] so the button mask is never zeroed out by stick motion
     * (the previous cause of dropped / "rapid fire" inputs).
     */
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        // 0) A stick or hat move being captured for a binding goes nowhere else.
        if ((event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && event.action == MotionEvent.ACTION_MOVE &&
            viewModel.controlCapture.onJoystickMove(event::getAxisValue)
        ) return true

        // Do not wake the screensaver or bump idle from stick ACTION_MOVE: analog drift would
        // either keep the timer from arming or dismiss the overlay instantly. Hats that become
        // D-pad keys still wake via dispatchKeyEvent; touch dismisses the overlay directly.

        // 1) Let the view hierarchy (emulator SurfaceView) try first.
        if (super.dispatchGenericMotionEvent(event)) return true

        // 2) Emulator-only fallback: full mapping pipeline through shared state, while the game runs on
        //    screen. Paused or behind the Library the motion is left unhandled, so Android turns a hat D-pad
        //    (the RP6's) and the left stick into D-pad keys that move focus through the menus.
        if (viewModel.isLoaded.value && viewModel.emulatorScreenVisible.value && !viewModel.isPaused.value &&
            (event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
            event.action == MotionEvent.ACTION_MOVE) {
            return GameInputState.handleMotionEvent(
                event, viewModel.activeControls.value.mappings, viewModel.loadedSystemName, viewModel.settings.value.pspRightStick,
            )
        }
        return false
    }
}
