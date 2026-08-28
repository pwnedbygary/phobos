package com.phobos.emulator

import android.os.Bundle
import android.net.Uri
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.collectAsState
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import coil.Coil
import coil.ImageLoader
import coil.decode.SvgDecoder
import com.phobos.emulator.data.SettingsStore
import com.phobos.emulator.input.GameInputState
import com.phobos.emulator.ui.MainScaffold
import com.phobos.emulator.ui.MainViewModel
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.ui.theme.PhobosTheme
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

class MainActivity : ComponentActivity() {

    private lateinit var settingsStore: SettingsStore
    private lateinit var viewModel: MainViewModel

    // Debug/test harness scope for the adb-driven ROM loader (intent extras).
    private val debugScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    // True while the emulator screen's Compose Box holds focus. When set, key
    // events are handled by Compose's onKeyEvent (which pushes to GameInputState);
    // the Activity-level fallback must then NOT re-process the same key, or every
    // press would be pushed twice — the root cause of the "rapid-fire" D-pad and
    // face buttons across all cores.
    @Volatile
    private var emulatorKeyHandled = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val imageLoader = ImageLoader.Builder(this)
            .components {
                add(SvgDecoder.Factory())
            }
            .build()
        Coil.setImageLoader(imageLoader)
        
        settingsStore = SettingsStore(this)
        viewModel = ViewModelProvider(this, object : ViewModelProvider.Factory {
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return MainViewModel(applicationContext, settingsStore) as T
            }
        })[MainViewModel::class.java]

        lifecycle.addObserver(viewModel)

        setContent {
            val settingsState = viewModel.settings.collectAsState()
            val settings = settingsState.value
            
            PhobosTheme(themeMode = settings.themeMode) {
                MainScaffold(viewModel = viewModel)
            }
        }

        // Key events (D-Pad, face buttons) are handled by Compose's onKeyEvent while
        // the emulator screen holds focus; this fallback catches them if focus is lost.
        installKeyEventFallback()

        // Window-level interception for the swap-screen RETURN hotkey. When the
        // emulator screen is NOT visible and a game is loaded+paused (we swapped
        // away), a focused Compose node on the library/settings screens absorbs
        // key events, so the content-view listener never sees them. ViewRootImpl
        // consults Window.Callback BEFORE any view dispatch, so the wrapper below
        // sees every key event regardless of focus. (ComponentActivity restricts
        // overriding dispatchKeyEvent itself — hence the callback wrapper.)
        val originalCallback = window.callback
        window.callback = object : android.view.Window.Callback by originalCallback {
            private val pressedKeys = mutableSetOf<Int>()
            override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                val visible = viewModel.emulatorScreenVisible.value
                val loaded = viewModel.isLoaded.value
                if (visible || (loaded && viewModel.isPaused.value)) {
                    if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                        pressedKeys += event.keyCode
                        val libraryCombo = viewModel.settings.value.hotkeys["library"] ?: emptyList()
                        if (libraryCombo.isNotEmpty() &&
                            libraryCombo.size == pressedKeys.size &&
                            libraryCombo.all { pressedKeys.contains(it) }
                        ) {
                            if (visible) viewModel.swapToLibrary()
                            else if (loaded) viewModel.swapBackToGame()
                            return true
                        }
                    } else if (event.action == KeyEvent.ACTION_UP) {
                        pressedKeys -= event.keyCode
                    }
                } else {
                    pressedKeys.clear()
                }
                return originalCallback.dispatchKeyEvent(event)
            }
        }

        handleDebugLoadIntent(intent)
    }

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        handleDebugLoadIntent(intent)
    }

    override fun onDestroy() {
        debugScope.cancel()
        super.onDestroy()
    }

    /**
     * Debug/test harness: load a ROM directly from adb without UI interaction.
     *
     * Usage:
     *   adb shell am start -n com.phobos.emulator/.MainActivity \
     *     --es load_uri "file:///storage/EBFF-F6C0/ROMs/tg16/Final Lap Twin (USA).zip" \
     *     --es load_name "Final Lap Twin (USA).zip" \
     *     --es load_system "PC Engine"
     *
     * Waits (bounded) for native asset extraction + settings/firmware paths to be
     * ready before loading, so cold-start intent loads behave like UI loads.
     */
    private fun handleDebugLoadIntent(intent: android.content.Intent?) {
        // Debug: dump NGCD graphics memories (sprite/fix/vram/pram) to filesDir.
        // Usage: adb shell am start -n com.phobos.emulator/.MainActivity --ez dump_ng_gfx true
        if (intent?.getBooleanExtra("dump_ng_gfx", false) == true) {
            PhobosCore.dumpNgGfx(filesDir.absolutePath)
            Log.i("Phobos", "dumpNgGfx: wrote to ${filesDir.absolutePath}")
            return
        }
        val uri = intent?.getStringExtra("load_uri") ?: return
        val name = intent.getStringExtra("load_name") ?: return
        val system = intent.getStringExtra("load_system") ?: return
        debugScope.launch {
            withTimeoutOrNull(8000) {
                viewModel.systems.first { it.isNotEmpty() }
                viewModel.settings.first { it.systemFirmwarePaths.isNotEmpty() }
            }
            Log.d("Phobos", "debugLoad: loading system='$system', rom='$name'")
            viewModel.loadRom(applicationContext, system, RomFile(name, Uri.parse(uri)))
            // Mirror the UI tap flow (SystemDetailScreen loads THEN navigates)
            // so the game is visible on the emulator screen instead of running
            // headless behind the library.
            viewModel.navigateTo("emulator/${Uri.encode(system)}/${Uri.encode(name)}")
        }
    }

    /**
     * Joystick motion is dispatched here before it reaches the view hierarchy.
     *
     * The view tree gets the event FIRST (via super) so the Controller Mapping
     * screen's OnGenericMotionListener can capture sticks for binding, and the
     * emulator's SurfaceView listener can process its own events. Previously this
     * method consumed every joystick move and returned true, which silently
     * swallowed those events — that is why analog sticks could never be bound in
     * the mapping menu, and why in-game stick handling bypassed the user's axis
     * mappings (raw AXIS_X/Y/Z/RZ was hardcoded).
     *
     * Only when the emulator core is loaded AND no view handled the event do we
     * translate the motion event ourselves. Everything is routed through
     * [GameInputState] so the button mask is never zeroed out by stick motion
     * (the previous cause of dropped / "rapid fire" inputs).
     */
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        // 1) Let the view hierarchy (mapping screen listener, emulator SurfaceView) try first.
        if (super.dispatchGenericMotionEvent(event)) return true

        // 2) Emulator-only fallback: full mapping pipeline through shared state.
        if (viewModel.isLoaded.value &&
            (event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
            event.action == MotionEvent.ACTION_MOVE) {
            return GameInputState.handleMotionEvent(event, viewModel.settings.value.inputMappings, viewModel.loadedSystemName)
        }
        return false
    }

    /**
     * D-Pad and game buttons arrive as key events (e.g. KEYCODE_DPAD_UP = 19).
     * The emulator screen handles them through Compose's onKeyEvent, which only
     * fires while its Box holds focus. If focus is lost (e.g. after tapping the
     * screen to toggle the control overlay, or after a dialog), D-Pad stops
     * working on every system even though the analog sticks — which travel
     * through [dispatchGenericMotionEvent] — keep working.
     *
     * ComponentActivity marks dispatchKeyEvent as @RestrictTo(LIBRARY_GROUP_PREFIX),
     * so we can't override it from the app. Instead we attach an OnKeyListener to
     * the root content view: Android dispatches key events to the focused view
     * first (Compose's onKeyEvent), and only if nothing consumed the event does
     * it bubble up to the root listener, which then translates mapped `k:*`
     * bindings through [GameInputState].
     */
    private fun installKeyEventFallback() {
        // Key tracking for the swap-screen hotkey while the emulator screen
        // is NOT focused (library/settings visible). Mirrors the emulator's
        // pressedKeys set; cleared on focus changes so stale combos don't fire.
        val fallbackPressedKeys = mutableSetOf<Int>()
        val onWindowFocusChanged = object : android.view.View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(v: android.view.View) {}
            override fun onViewDetachedFromWindow(v: android.view.View) { fallbackPressedKeys.clear() }
        }
        findViewById<android.view.View>(android.R.id.content).addOnAttachStateChangeListener(onWindowFocusChanged)

        findViewById<android.view.View>(android.R.id.content).setOnKeyListener { _, keyCode, event ->
            // When the emulator screen's Compose Box has focus, its onKeyEvent
            // already translated and pushed this key. Skip to avoid double-push.
            if (emulatorKeyHandled) return@setOnKeyListener false

            // Swap-screen hotkey: works from ANY screen. With the emulator
            // screen visible → pause + leave to library; from library/settings
            // with a game loaded (paused) → navigate back and resume.
            if (event.action == android.view.KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                fallbackPressedKeys += keyCode
                val libraryCombo = viewModel.settings.value.hotkeys["library"] ?: emptyList()
                if (libraryCombo.isNotEmpty()) {
                    val allPressed = fallbackPressedKeys + GameInputState.hotkeyKeys
                    if (libraryCombo.size == allPressed.size && libraryCombo.all { allPressed.contains(it) }) {
                        if (viewModel.emulatorScreenVisible.value) viewModel.swapToLibrary()
                        else if (viewModel.isLoaded.value) viewModel.swapBackToGame()
                        return@setOnKeyListener true
                    }
                }
            } else if (event.action == android.view.KeyEvent.ACTION_UP) {
                fallbackPressedKeys -= keyCode
            }

            if (viewModel.isLoaded.value && !viewModel.isPaused.value) {
                // Swallow auto-repeat so held buttons don't rapidly toggle.
                if (event.repeatCount > 0) return@setOnKeyListener true

                var bitmask = 0
                viewModel.settings.value.inputMappings.forEach { (bit, binding) ->
                    if (binding == "k:$keyCode") bitmask = bitmask or bit
                }
                if (bitmask != 0) {
                    GameInputState.setButton(bitmask, event.action != android.view.KeyEvent.ACTION_UP)
                    return@setOnKeyListener true
                }
            }
            false
        }
    }

    /**
     * Lets the emulator screen toggle which path owns key handling: when the
     * Compose Box holds focus, its onKeyEvent is authoritative; the Activity
     * fallback only kicks in when focus is lost.
     */
    fun setEmulatorKeyHandled(handled: Boolean) {
        emulatorKeyHandled = handled
    }
}
