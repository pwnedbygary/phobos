package com.phobos.emulator.input

import android.view.KeyEvent
import androidx.compose.ui.focus.FocusState

/** Keys left to the system over a game: the volume. */
val VOLUME_KEYS = setOf(KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE)

/**
 * How long a tap waits after its release: on the RP6 the activity hears of a controller gone within 3 ms of the
 * release Android sends as it goes (InputLog).
 */
private const val TAP_WAIT_MS = 100L

/**
 * The game screen's controller keys: its key handler, its hat listener and its focus, as EmulatorScreen and the
 * activity's window run them (and the JVM tests, which replay the RP6's events through this). The window hands it
 * every key while the game is on screen, whichever view has focus. Hotkeys come first, then the buttons a key is bound
 * to. A PSP system dialog over the game hands it its keys too, for the hotkeys alone ([onDialogKey]).
 */
class GameKeys(private val screen: Screen) : KeyHandler {
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
        /**
         * Runs [action]; false when it doesn't apply to this system. Fast-forward hold starts this way, and each
         * press of Frame Advance runs its frame.
         */
        fun hotkey(action: String): Boolean
        fun stopFastForward()
        /** The Frame Advance combo went down (true) or was let go (false): what repeats frames after its press's. */
        fun frameAdvanceHeld(held: Boolean)
        /** The Slow Motion combo went down (true) or was let go (false). */
        fun slowMotionHeld(held: Boolean)
        /** Runs [run] [delayMs] from now on this thread, unless the screen has gone by then; at once by default. */
        fun later(delayMs: Long, run: () -> Unit) = run()
    }

    private val tracker = HotkeyTracker()
    private var fastForwardHeld = false
    private var slowMotionHeld = false
    private var frameAdvanceHeld = false
    /** How many times the keys have been forgotten ([forgetPressedKeys]): a tap waiting from before is dropped. */
    private var forgets = 0

    /**
     * Forgets the keys held, re-arming hotkey combos and letting go of the hold actions: as the game pauses, a PSP
     * dialog opens or a controller goes away.
     */
    fun forgetPressedKeys() {
        forgets++
        tracker.reset()
        GameInputState.consumedHotkeyKeys = emptySet()
        endHolds()
    }

    /** A key event ([down]: a key press, repeats included); true when it's taken. */
    override fun onKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean {
        // Leave volume keys to the system, and every key to the layout editors.
        if (keyCode in VOLUME_KEYS || screen.editing()) return false
        // A held key's repeats: taken while the game runs, focus navigation in the pause menu.
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

        if (runHotkeys(keyCode, down)) return true
        // Paused: mapped buttons navigate the menu (BUTTON_A falls back to DPAD_CENTER).
        if (screen.paused()) return false

        val bits = InputBindings.of(screen.controls().mappings).bitsForKey(keyCode)
        if (bits != 0) {
            GameInputState.setButton(bits, down)
            return true
        }
        return false
    }

    /**
     * A key in a dialog over the game, a PSP system dialog (a window of its own, whose keys don't pass the activity's
     * window): the hotkeys run as they do over the game, and true when they take the key; the rest are the dialog's,
     * none the game's. A held key's repeats are taken where its press was.
     */
    fun onDialogKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean {
        if (keyCode in VOLUME_KEYS) return false
        if (repeatCount > 0) return keyCode in tracker.consumedTokens
        return runHotkeys(keyCode, down)
    }

    /** The hotkeys count [keyCode]'s key as held: its press came here, or in a dialog ([onDialogKey]), and no release. */
    fun holdsKey(keyCode: Int): Boolean = tracker.holdsKey(keyCode)

    /** The hotkeys' part of a key: it pressed or let go of, what that completes run, the holds kept; true if taken. */
    private fun runHotkeys(keyCode: Int, down: Boolean): Boolean {
        val hotkeys = screen.controls().hotkeys
        // The hat and the axes may have changed since the last key: reconcile them first, so a
        // combo never matches a direction that is no longer held.
        tracker.syncExternal(hotkeys, GameInputState.hotkeyKeys)
        var consumed = if (down) tracker.press(hotkeys, keyCode) else tracker.release(keyCode)
        GameInputState.consumedHotkeyKeys = tracker.consumedTokens
        GameInputState.applyHotkeyConsumption()

        // A press completes combos, each once however its keys arrive; a release completes only a tap.
        val completed = tracker.completions(hotkeys)
        for (action in completed) {
            if (action !in HotkeyAction.HOLDS && screen.hotkey(action)) consumed = true
        }
        runTaps(tracker.taps(hotkeys))
        updateHolds(hotkeys, completed)
        return consumed
    }

    /**
     * A tap's actions run a moment after its key is let go ([TAP_WAIT_MS]), unless the keys are forgotten first: a
     * controller going away lets go of its keys, Android sometimes before it reports the controller gone (the
     * activity's onInputDeviceRemoved then forgets them), and that release is no tap.
     */
    private fun runTaps(actions: List<String>) {
        val taps = actions.filter { it !in HotkeyAction.HOLDS }
        if (taps.isEmpty()) return
        val at = forgets
        screen.later(TAP_WAIT_MS) {
            if (forgets == at) taps.forEach { screen.hotkey(it) }
        }
    }

    /**
     * The hat or an axis changed (GameInputState.onHotkeyKeysChanged). Controllers that report the
     * D-pad as a hat and sticks/triggers as axes send motion events, not keys, so combos that use
     * them are re-checked when the held set changes: a press can complete one, a release a tap.
     */
    fun onHatChanged() {
        val hotkeys = screen.controls().hotkeys
        tracker.syncExternal(hotkeys, GameInputState.hotkeyKeys)
        GameInputState.consumedHotkeyKeys = tracker.consumedTokens
        GameInputState.applyHotkeyConsumption()
        val completed = tracker.completions(hotkeys)
        for (action in completed) {
            if (action !in HotkeyAction.HOLDS) screen.hotkey(action)
        }
        runTaps(tracker.taps(hotkeys))
        updateHolds(hotkeys, completed)
    }

    /**
     * A hold action starts when a press completes one of its combos ([completed]) and lasts while every key of one of
     * them stays held, other keys or not. A tap can't start one: it comes as its key is let go.
     */
    private fun updateHolds(hotkeys: Map<String, List<List<Int>>>, completed: List<String>) {
        val fastForward = holds(HotkeyAction.FAST_FORWARD_HOLD, fastForwardHeld, hotkeys, completed)
        if (fastForward != fastForwardHeld) {
            fastForwardHeld = fastForward
            if (fastForward) screen.hotkey(HotkeyAction.FAST_FORWARD_HOLD) else if (!screen.fastForwardToggled()) screen.stopFastForward()
        }
        val slowMotion = holds(HotkeyAction.SLOW_MOTION, slowMotionHeld, hotkeys, completed)
        if (slowMotion != slowMotionHeld) {
            slowMotionHeld = slowMotion
            screen.slowMotionHeld(slowMotion)
        }
        val frameAdvance = holds(HotkeyAction.FRAME_ADVANCE, frameAdvanceHeld, hotkeys, completed)
        if (frameAdvance != frameAdvanceHeld) {
            frameAdvanceHeld = frameAdvance
            screen.frameAdvanceHeld(frameAdvance)
            // The press runs its frame as an action, not as the hold, so a press let go of at once still runs one.
            if (frameAdvance) screen.hotkey(HotkeyAction.FRAME_ADVANCE)
        }
    }

    private fun holds(action: String, held: Boolean, hotkeys: Map<String, List<List<Int>>>, completed: List<String>): Boolean =
        if (held) hotkeys[action].orEmpty().any(tracker::holding) else action in completed

    private fun endHolds() {
        if (fastForwardHeld) {
            fastForwardHeld = false
            if (!screen.fastForwardToggled()) screen.stopFastForward()
        }
        if (slowMotionHeld) {
            slowMotionHeld = false
            screen.slowMotionHeld(false)
        }
        if (frameAdvanceHeld) {
            frameAdvanceHeld = false
            screen.frameAdvanceHeld(false)
        }
    }

    /** The screen's focus changed: every button is let go of once neither it nor anything in it has focus. */
    fun onFocusChanged(focus: FocusState) {
        InputLog.note { "game focus $focus" }
        if (!focus.hasFocus) {
            forgetPressedKeys()
            GameInputState.releaseAllButtons()
        }
    }
}
