package com.phobos.emulator.input

import android.view.InputDevice
import android.view.MotionEvent
import com.phobos.emulator.PhobosCore
import kotlin.math.abs

/**
 * Single shared source of truth for the current virtual controller state.
 *
 * Every input path writes into this object — the Activity-level joystick fallback
 * (MainActivity), the emulator screen's key handler, its SurfaceView motion listener and the
 * on-screen touch controls — and the combined state is pushed to the native core in one JNI
 * call. This prevents one path (e.g. stick motion) from pushing a button mask of zero and
 * clearing buttons held through another path (e.g. hardware keys), which used to show up as
 * dropped or "rapid fire" inputs whenever a stick moved while a button was held.
 *
 * All members are used from the main thread.
 */
object GameInputState {
    var lx = 0f
    var ly = 0f
    var rx = 0f
    var ry = 0f
    var hwButtons = 0
    var virtualButtons = 0

    // Hotkey support: many controllers report the D-pad as HAT axes (motion events), not
    // KEYCODE_DPAD_* keys, so the emulator screen's key-only pressedKeys tracker never sees the
    // D-pad. This set holds the D-pad keycodes currently pressed via the hat, so hotkey combos
    // like Z + D-pad Right can match.
    val hotkeyKeys = mutableSetOf<Int>()
    var hotkeyKeysVersion = 0
        private set

    /** Re-evaluates hotkey combos when the hat changes (a motion event doesn't re-run the check). */
    var onHotkeyKeysChanged: (() -> Unit)? = null

    /** Called when a physical controller produces input; the touch overlay hides itself. */
    var onPhysicalInput: (() -> Unit)? = null

    private const val HAT_PRESS_THRESHOLD = 0.5f

    // Hysteresis for axis-mapped digital buttons (triggers, hats, stick-as-buttons). The press
    // threshold matches ares' digital qualifier (±16384 / 32767 ≈ 50% deflection); a button only
    // releases below RELEASE_THRESHOLD so a noisy axis near the boundary can't chatter (the PS1
    // "rapid fire" symptom). ares has no release hysteresis — a deliberate deviation for Android's
    // noisier controller axes.
    private const val PRESS_THRESHOLD = 0.5f
    private const val RELEASE_THRESHOLD = 0.4f

    /** Stick deflection that counts as "the player is using a physical controller". */
    private const val PHYSICAL_ACTIVITY_THRESHOLD = 0.35f

    private const val FIRST_STICK_BIT = PhobosCore.Input.LS_UP
    private const val LAST_STICK_BIT = PhobosCore.Input.RS_RIGHT

    /** Last latched press state per digital bit driven by an axis binding. */
    private val axisDigitalState = HashMap<Int, Boolean>()

    val buttons: Int get() = hwButtons or virtualButtons

    /** Reset all state (used when leaving the emulator / starting a new session). */
    fun reset() {
        lx = 0f; ly = 0f; rx = 0f; ry = 0f
        hwButtons = 0
        virtualButtons = 0
        axisDigitalState.clear()
        hotkeyKeys.clear()
        lastHatBits = 0
    }

    /**
     * Release every held button without touching stick state. Called when the emulator loses
     * focus (dialog, pause menu): Android can drop the matching KeyUp for a held button, which
     * would otherwise leave the bit latched forever.
     */
    fun releaseAllButtons() {
        lastHatBits = 0
        if (hwButtons != 0 || virtualButtons != 0) {
            hwButtons = 0
            virtualButtons = 0
            axisDigitalState.clear()
            push()
        }
    }

    /** Set or clear hardware-mapped digital buttons and push the combined state. */
    fun setButton(bits: Int, down: Boolean) {
        hwButtons = if (down) hwButtons or bits else hwButtons and bits.inv()
        if (down) onPhysicalInput?.invoke()
        push()
    }

    /** Replace the on-screen (touch) button mask and push. */
    fun updateVirtualButtons(mask: Int) {
        if (mask == virtualButtons) return
        virtualButtons = mask
        push()
    }

    /** Set a stick from the touch overlay (screen-down positive, like the physical axes). */
    fun setTouchStick(left: Boolean, x: Float, y: Float) {
        if (left) { lx = x; ly = y } else { rx = x; ry = y }
        push()
    }

    /**
     * Processes a joystick MotionEvent using the user's input mappings: resolves each stick axis
     * through its binding (falling back to the platform default axis), latches axis-mapped
     * digital buttons with hysteresis and pushes the combined state.
     *
     * @return true if the event was a joystick move and was consumed.
     */
    fun handleMotionEvent(event: MotionEvent, mappings: Map<Int, String>, systemName: String): Boolean {
        if ((event.source and InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        val bindings = InputBindings.of(mappings)
        val hatActive = updateHatDpad(event)

        val newLx = stickAxis(event, bindings, PhobosCore.Input.LS_LEFT, PhobosCore.Input.LS_RIGHT, MotionEvent.AXIS_X)
        val newLy = stickAxis(event, bindings, PhobosCore.Input.LS_UP, PhobosCore.Input.LS_DOWN, MotionEvent.AXIS_Y)
        val newRx = stickAxis(event, bindings, PhobosCore.Input.RS_LEFT, PhobosCore.Input.RS_RIGHT, MotionEvent.AXIS_Z)
        val newRy = stickAxis(event, bindings, PhobosCore.Input.RS_UP, PhobosCore.Input.RS_DOWN, MotionEvent.AXIS_RZ)

        // Only the N64 latches stick-as-button bits (17-24): its right stick drives the
        // C-buttons. Every other system treats the right stick as purely analog (e.g. the PS1
        // DualShock camera), so stick bits are left alone there.
        val latchStickBits = systemName.contains("Nintendo 64", ignoreCase = true)

        var axisButtonPressed = false
        for (binding in bindings.axisBindings) {
            val isStickBit = binding.bit in FIRST_STICK_BIT..LAST_STICK_BIT
            if (isStickBit && !latchStickBits) continue
            val value = event.getAxisValue(binding.axis)
            val magnitude = if (binding.positive) value else -value
            val wasPressed = axisDigitalState[binding.bit] ?: false
            val pressed = magnitude > if (wasPressed) RELEASE_THRESHOLD else PRESS_THRESHOLD
            axisDigitalState[binding.bit] = pressed
            hwButtons = if (pressed) hwButtons or binding.bit else hwButtons and binding.bit.inv()
            if (pressed && !wasPressed) axisButtonPressed = true
        }

        lx = newLx
        ly = newLy
        rx = newRx
        ry = newRy
        push()

        val stickActive = maxOf(abs(newLx), abs(newLy), abs(newRx), abs(newRy)) > PHYSICAL_ACTIVITY_THRESHOLD
        if (hatActive || axisButtonPressed || stickActive) onPhysicalInput?.invoke()
        return true
    }

    /** Push the combined sticks + buttons to the native core. */
    fun push() {
        PhobosCore.setInput(lx, ly, rx, ry, buttons)
    }

    /**
     * Latches the D-pad hat (AXIS_HAT_X/Y) into the gameplay D-pad bits and the hotkey key set.
     * Many controllers report the D-pad ONLY as a hat, so it must drive the core's D-pad bits
     * (0..3) for every system, not just hotkey combos.
     *
     * @return true when any hat direction is pressed.
     */
    private fun updateHatDpad(event: MotionEvent): Boolean {
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val left = hatX < -HAT_PRESS_THRESHOLD
        val right = hatX > HAT_PRESS_THRESHOLD
        val up = hatY < -HAT_PRESS_THRESHOLD
        val down = hatY > HAT_PRESS_THRESHOLD

        fun setKey(keyCode: Int, pressed: Boolean) {
            if (pressed) hotkeyKeys.add(keyCode) else hotkeyKeys.remove(keyCode)
        }
        setKey(android.view.KeyEvent.KEYCODE_DPAD_LEFT, left)
        setKey(android.view.KeyEvent.KEYCODE_DPAD_RIGHT, right)
        setKey(android.view.KeyEvent.KEYCODE_DPAD_UP, up)
        setKey(android.view.KeyEvent.KEYCODE_DPAD_DOWN, down)

        val dpadBits = (if (up) PhobosCore.Input.UP else 0) or
            (if (down) PhobosCore.Input.DOWN else 0) or
            (if (left) PhobosCore.Input.LEFT else 0) or
            (if (right) PhobosCore.Input.RIGHT else 0)

        // Only act when the hat changed: re-running hotkey matching on every stick motion event
        // would fire a held combo again, and rewriting all four D-pad bits would release a D-pad
        // that the controller reports as keys. Only the bits the hat itself set are cleared.
        if (dpadBits != lastHatBits) {
            hwButtons = (hwButtons and lastHatBits.inv()) or dpadBits
            lastHatBits = dpadBits
            hotkeyKeysVersion++
            onHotkeyKeysChanged?.invoke()
        }
        return dpadBits != 0
    }

    private var lastHatBits = 0

    /**
     * Value of one stick axis from its half-axis bindings ([negBit] and [posBit]), falling back
     * to the platform default axis when neither half is bound to an axis.
     *
     * Raw pass-through, ares-style: no frontend dead zone. ares feeds raw ±32767 values to the
     * core, which applies its own authoritative dead zone and response curve; pre-dead-zoning
     * here would stack with the core's and make sticks feel unresponsive.
     */
    private fun stickAxis(event: MotionEvent, bindings: InputBindings, negBit: Int, posBit: Int, defaultAxis: Int): Float {
        var negValue = 0f
        var posValue = 0f
        bindings.axisFor(negBit)?.let { b ->
            val v = event.getAxisValue(b.axis)
            if (!b.positive && v < 0f) negValue = v
            else if (b.positive && v > 0f) negValue = -v
        }
        bindings.axisFor(posBit)?.let { b ->
            val v = event.getAxisValue(b.axis)
            if (b.positive && v > 0f) posValue = v
            else if (!b.positive && v < 0f) posValue = -v
        }
        if (negValue != 0f || posValue != 0f) return (negValue + posValue).coerceIn(-1f, 1f)
        return event.getAxisValue(defaultAxis).coerceIn(-1f, 1f)
    }
}
