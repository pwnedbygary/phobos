package com.phobos.emulator.input

import android.view.KeyEvent
import androidx.compose.ui.focus.FocusState

/** Keys left to the system over a game: the volume. */
val VOLUME_KEYS = setOf(KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE)

/**
 * The game screen's controller keys: its key handler, its hat listener and its focus, as EmulatorScreen runs them (and
 * the JVM tests, which replay the RP6's events through this). Hotkeys come first, then the buttons a key is bound to.
 */
class GameKeys(private val screen: Screen) {
    /** What the game screen tells its keys, asked at each key. */
    interface Screen {
        fun controls(): ActiveControls
        fun paused(): Boolean
        /** A layout editor is open (the touch controls' or the performance monitor's): every key is its own. */
        fun editing(): Boolean
        fun fastForwardToggled(): Boolean
        /** The ZX keyboard key a controller button is being bound to, or null. */
        fun zxKeyToBind(): String?
        fun bindZxKey(key: String, bit: Int)
        /** Runs [action]; false when it doesn't apply to this system. */
        fun hotkey(action: String): Boolean
        fun stopFastForward()
    }

    /** The keys held, for hotkey combos; a hat D-pad's directions are GameInputState.hotkeyKeys. */
    private var pressedKeys = setOf<Int>()

    /** Forgets the keys held, re-arming hotkey combos: as the game pauses or a PSP dialog opens. */
    fun forgetPressedKeys() {
        pressedKeys = emptySet()
    }

    /** A key event from the screen's onPreviewKeyEvent ([down]: KeyEventType.KeyDown); true when it's taken. */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean {
        // Leave volume keys to the system, and every key to the layout editors.
        if (keyCode in VOLUME_KEYS || screen.editing()) return false
        // Paused: let key repeats drive focus navigation in the pause menu.
        if (repeatCount > 0) return !screen.paused()

        // ZX CUSTOM rebinding captures the next controller button.
        val rebind = screen.zxKeyToBind()
        if (rebind != null && down) {
            val bit = mapKeyCodeToBit(keyCode)
            if (bit != 0) {
                screen.bindZxKey(rebind, bit)
                return true
            }
        }

        pressedKeys = if (down) pressedKeys + keyCode else pressedKeys - keyCode

        // Hotkeys match on keys plus hat D-pad directions (GameInputState.hotkeyKeys),
        // so combos like Z + D-pad Right work on controllers with a hat D-pad.
        val controls = screen.controls()
        var consumed = false
        if (down) {
            for (action in matchingHotkeys(controls.hotkeys, pressedKeys + GameInputState.hotkeyKeys)) {
                if (screen.hotkey(action)) consumed = true
            }
        } else if (!screen.fastForwardToggled() && controls.hotkeys[HotkeyAction.FAST_FORWARD_HOLD]?.contains(keyCode) == true) {
            screen.stopFastForward()
        }
        if (consumed) return true
        // Paused: mapped buttons navigate the menu (BUTTON_A falls back to DPAD_CENTER).
        if (screen.paused()) return false

        val bits = InputBindings.of(controls.mappings).bitsForKey(keyCode)
        if (bits != 0) {
            GameInputState.setButton(bits, down)
            return true
        }
        return false
    }

    /**
     * The hat moved (GameInputState.onHotkeyKeysChanged). Controllers that report the D-pad as a hat send motion
     * events, not keys, so combos that include a D-pad direction are re-checked then. Hold-to-fast-forward needs a
     * key release and is not supported from the hat.
     */
    fun onHatChanged() {
        val hotkeys = screen.controls().hotkeys
        for (action in matchingHotkeys(hotkeys, pressedKeys + GameInputState.hotkeyKeys)) {
            if (action != HotkeyAction.FAST_FORWARD_HOLD && comboUsesDpad(hotkeys[action].orEmpty())) screen.hotkey(action)
        }
    }

    /** The screen's focus changed: every button is let go of once neither it nor anything in it has focus. */
    fun onFocusChanged(focus: FocusState) {
        InputLog.note { "game focus $focus" }
        if (!focus.hasFocus) GameInputState.releaseAllButtons()
    }
}
