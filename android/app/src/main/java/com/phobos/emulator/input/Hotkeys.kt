package com.phobos.emulator.input

import android.view.KeyEvent

/** Hotkey action identifiers, as persisted in settings (`hotkey_combo_<action>`). */
object HotkeyAction {
    const val PAUSE = "pause"
    const val FAST_FORWARD_HOLD = "ff_hold"
    const val FAST_FORWARD_TOGGLE = "ff_toggle"
    const val SAVE_STATE = "save"
    const val LOAD_STATE = "load"
    const val NEXT_SLOT = "inc_slot"
    const val PREVIOUS_SLOT = "dec_slot"
    const val RESET = "reset"
    const val RELOAD = "reload"
    const val QUIT = "quit"
    const val SCREENSHOT = "screenshot"
    const val MUTE = "mute"
    const val FRAME_ADVANCE = "frame_advance"
    const val PS1_ANALOG_TOGGLE = "analog_toggle"
    const val KEYBOARD = "keyboard"
    const val LIBRARY = "library"
}

private val DPAD_KEYCODES = setOf(
    KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
    KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
)

/** Actions whose key combo is exactly the set of currently pressed keys. */
fun matchingHotkeys(hotkeys: Map<String, List<Int>>, pressed: Set<Int>): List<String> =
    hotkeys.mapNotNull { (action, combo) ->
        action.takeIf { combo.isNotEmpty() && combo.size == pressed.size && combo.all(pressed::contains) }
    }

/**
 * True when a combo includes a D-pad key. Only such combos can be completed by a D-pad *hat*
 * change (motion event), so only they are re-evaluated when the hat moves.
 */
fun comboUsesDpad(combo: List<Int>): Boolean = combo.any { it in DPAD_KEYCODES }
