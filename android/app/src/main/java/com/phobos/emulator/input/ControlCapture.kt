package com.phobos.emulator.input

import android.view.KeyEvent
import android.view.MotionEvent
import com.phobos.emulator.PhobosCore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.math.abs

/**
 * A controller press being captured for a binding. While one runs, MainActivity hands it every key
 * and joystick event before anything else sees them, so no hotkey, Library combo, tab switch or
 * menu navigation acts on the press, and B binds instead of going back. Used from the main thread.
 */
class ControlCapture(private val scope: CoroutineScope) {
    sealed interface Target {
        val label: String
        data class Button(val bit: Int, override val label: String) : Target
        data class Hotkey(val action: String, override val label: String) : Target
    }

    private val _target = MutableStateFlow<Target?>(null)
    val target: StateFlow<Target?> = _target.asStateFlow()

    private val _combo = MutableStateFlow<List<Int>>(emptyList())
    /** The keys of the hotkey combo held so far. */
    val combo: StateFlow<List<Int>> = _combo.asStateFlow()

    private var onButton: (String) -> Unit = {}
    private var onCombo: (List<Int>) -> Unit = {}
    private var timer: Job? = null
    // Keys pressed for a capture: their releases are swallowed as well, even after it ends.
    private val heldKeys = mutableSetOf<Int>()
    // A trigger or hat D-pad can send its axis and its key together: once the axis has bound, that
    // key is swallowed too, for a moment.
    private var echoKeys: Set<Int> = emptySet()
    private var echoUntil = 0L
    // Axes still held from a capture: their moves are taken until they're let go, or Android would
    // turn them into D-pad keys that move the menu or set off the hotkey just bound.
    private var heldAxes: Set<Int> = emptySet()
    // The axes past the threshold in the capture's latest move.
    private var deflectedAxes: Set<Int> = emptySet()

    fun captureButton(bit: Int, label: String, onBound: (String) -> Unit) {
        onButton = onBound
        start(Target.Button(bit, label))
    }

    fun captureHotkey(action: String, label: String, onBound: (List<Int>) -> Unit) {
        onCombo = onBound
        start(Target.Hotkey(action, label))
    }

    /**
     * Ends the capture and keeps the old binding. What an earlier capture left held is let through
     * again, so closing the page hands a still-held stick or trigger straight back to the game.
     */
    fun cancel() {
        timer?.cancel()
        _target.value = null
        _combo.value = emptyList()
        echoKeys = emptySet()
        heldAxes = emptySet()
    }

    private fun start(target: Target) {
        _combo.value = emptyList()
        deflectedAxes = emptySet()
        _target.value = target
        keepWaiting()
    }

    // The wait for a press; one the target ignores still shows someone there, and starts it over.
    private fun keepWaiting() = restartTimer(IDLE_TIMEOUT_MS) { end() }

    private fun restartTimer(ms: Long, action: () -> Unit) {
        timer?.cancel()
        timer = scope.launch { delay(ms); action() }
    }

    // Ends the capture with its page still open: what it last saw held stays held back until let go.
    private fun end() {
        val held = deflectedAxes
        cancel()
        heldAxes = held
    }

    private fun finish(now: Long, echo: Set<Int> = emptySet(), bind: () -> Unit) {
        end()
        echoKeys = echo
        echoUntil = now + ECHO_MS
        bind()
    }

    /**
     * A key event; true when the capture takes it. [systemBack] is a Back from the navigation bar or
     * gesture rather than a controller, and cancels instead of binding.
     */
    fun onKey(keyCode: Int, down: Boolean, repeat: Int, systemBack: Boolean, now: Long = System.currentTimeMillis()): Boolean {
        if (keyCode in PASS_THROUGH_KEYS) return false
        val target = _target.value
        // Only the capture's own keys: a key held from before it is released as usual.
        if (!down) return heldKeys.remove(keyCode)
        if (target == null) {
            // A key still held from a capture keeps repeating, and a trigger's or hat's key can follow
            // the axis that bound; any other press, a new one whose release was lost included, is
            // the game's or the menu's.
            if ((repeat > 0 && keyCode in heldKeys) || (keyCode in echoKeys && now < echoUntil)) {
                heldKeys += keyCode
                return true
            }
            heldKeys.remove(keyCode)
            return false
        }
        heldKeys += keyCode
        if (repeat > 0) return true
        if (systemBack) {
            end()
            return true
        }
        when (target) {
            is Target.Button -> {
                val binding = CapturePolicy.keyBinding(target.bit, keyCode)
                if (binding != null) finish(now) { onButton(binding) } else keepWaiting()
            }
            is Target.Hotkey -> addToCombo(listOf(keyCode))
        }
        return true
    }

    /**
     * A joystick move, read through [axisValue]; true when the capture takes it. All of them are
     * taken while capturing, so Android doesn't turn the stick or hat into D-pad keys.
     */
    fun onJoystickMove(axisValue: (Int) -> Float, now: Long = System.currentTimeMillis()): Boolean {
        val target = _target.value
        if (target == null) {
            if (heldAxes.isEmpty()) return false
            heldAxes = CapturePolicy.stillHeld(heldAxes, axisValue)
            return true
        }
        deflectedAxes = CapturePolicy.deflectedAxes(axisValue)
        when (target) {
            is Target.Button -> {
                val binding = CapturePolicy.axisBinding(target.bit, axisValue)
                if (binding != null) finish(now, CapturePolicy.pairedKeys(binding)) { onButton(binding) }
                else if (deflectedAxes.isNotEmpty()) keepWaiting()
            }
            // Hotkeys match a hat D-pad as the D-pad keys (GameInputState.hotkeyKeys), held like any
            // key the capture took. Once a combo has keys, only a new one restarts its timer.
            is Target.Hotkey -> {
                val hat = CapturePolicy.hatKeys(axisValue)
                if (hat.isNotEmpty()) {
                    heldKeys += hat
                    addToCombo(hat)
                } else if (_combo.value.isEmpty() && deflectedAxes.isNotEmpty()) keepWaiting()
            }
        }
        return true
    }

    private fun addToCombo(keys: List<Int>) {
        val added = keys.filter { it !in _combo.value }
        if (added.isEmpty()) return
        _combo.value = _combo.value + added
        restartTimer(COMBO_SETTLE_MS) {
            val combo = _combo.value
            finish(System.currentTimeMillis()) { onCombo(combo) }
        }
    }

    companion object {
        /** With no input for this long the capture ends and the old binding stays. */
        const val IDLE_TIMEOUT_MS = 5_000L
        /** A combo is done once no key has joined it for this long. */
        const val COMBO_SETTLE_MS = 1_000L
        private const val ECHO_MS = 250L
        private val PASS_THROUGH_KEYS = setOf(KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE)
    }
}

/** What a captured press binds to, as the Controller Mapping screen has always decided it. */
object CapturePolicy {
    private const val AXIS_THRESHOLD = 0.4f
    private const val HAT_THRESHOLD = 0.5f
    private val STICK_AXES = setOf(MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ)
    private val DPAD_KEYS = KeyEvent.KEYCODE_DPAD_UP..KeyEvent.KEYCODE_DPAD_RIGHT
    private val ALL_AXES = (0 until 64).toSet()

    private fun isDpad(bit: Int) = bit in PhobosCore.Input.UP..PhobosCore.Input.RIGHT
    private fun isStick(bit: Int) = bit >= PhobosCore.Input.LS_UP

    /**
     * The binding an axis past the threshold makes for [bit], or null. A D-pad target ignores the
     * sticks, so a stick can't be taken for the D-pad; a hat D-pad binds as its axis.
     */
    fun axisBinding(bit: Int, axisValue: (Int) -> Float): String? {
        for (axis in 0 until 64) {
            val value = axisValue(axis)
            if (abs(value) <= AXIS_THRESHOLD || (isDpad(bit) && axis in STICK_AXES)) continue
            return "a:$axis:${if (value > 0) 1 else 0}"
        }
        return null
    }

    /** The axes past the threshold; a resting stick's jitter isn't. */
    fun deflectedAxes(axisValue: (Int) -> Float): Set<Int> = stillHeld(ALL_AXES, axisValue)

    /** Which of [axes] are still past the threshold. */
    fun stillHeld(axes: Set<Int>, axisValue: (Int) -> Float): Set<Int> = axes.filterTo(mutableSetOf()) { abs(axisValue(it)) > AXIS_THRESHOLD }

    /** The keys a controller may send along with the axis of [binding]: a trigger's button, a hat's D-pad keys. */
    fun pairedKeys(binding: String): Set<Int> {
        if (!binding.startsWith("a:")) return emptySet()
        return when (binding.split(":").getOrNull(1)?.toIntOrNull()) {
            MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE -> setOf(KeyEvent.KEYCODE_BUTTON_L2)
            MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS -> setOf(KeyEvent.KEYCODE_BUTTON_R2)
            MotionEvent.AXIS_HAT_X -> setOf(KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT)
            MotionEvent.AXIS_HAT_Y -> setOf(KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN)
            else -> emptySet()
        }
    }

    /** The binding [keyCode] makes for [bit], or null. A stick target ignores D-pad keys, which controllers send alongside. */
    fun keyBinding(bit: Int, keyCode: Int): String? =
        if (isStick(bit) && keyCode in DPAD_KEYS) null else "k:$keyCode"

    /** The D-pad keys a hat D-pad is pressing. */
    fun hatKeys(axisValue: (Int) -> Float): List<Int> {
        val x = axisValue(MotionEvent.AXIS_HAT_X)
        val y = axisValue(MotionEvent.AXIS_HAT_Y)
        return listOfNotNull(
            KeyEvent.KEYCODE_DPAD_UP.takeIf { y < -HAT_THRESHOLD },
            KeyEvent.KEYCODE_DPAD_DOWN.takeIf { y > HAT_THRESHOLD },
            KeyEvent.KEYCODE_DPAD_LEFT.takeIf { x < -HAT_THRESHOLD },
            KeyEvent.KEYCODE_DPAD_RIGHT.takeIf { x > HAT_THRESHOLD },
        )
    }
}
