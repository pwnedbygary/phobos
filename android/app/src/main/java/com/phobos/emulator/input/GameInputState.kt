package com.phobos.emulator.input

import android.view.InputDevice
import android.view.MotionEvent
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.launch.LaunchSystems
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
    /** What the right stick presses in a PSP game ([PspRightStick]): apart from [hwButtons], so each lets go alone. */
    var rightStickButtons = 0
        private set
    /** The right stick's latched directions ([stickDirections]). */
    private var rightStickDirections = 0

    // Hotkey support: many controllers report the D-pad as HAT axes and the triggers and sticks as
    // axes (motion events), not keys, so key events alone never show them to the hotkeys. This set
    // holds the hotkey tokens held that way: the hat's D-pad keycodes, a trigger's L2/R2 keycode and
    // each stick direction's axis token (HotkeyTokens), so combos like Mode + D-pad Right can match.
    val hotkeyKeys = mutableSetOf<Int>()
    var hotkeyKeysVersion = 0
        private set

    /** Hotkey tokens whose game input is held back: their key is a combo modifier or action. */
    var consumedHotkeyKeys = emptySet<Int>()

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

    val buttons: Int get() = hwButtons or virtualButtons or rightStickButtons

    /** Reset all state (used when leaving the emulator / starting a new session). */
    fun reset() {
        InputLog.note { "reset, buttons 0x${Integer.toHexString(buttons)}" }
        lx = 0f; ly = 0f; rx = 0f; ry = 0f
        hwButtons = 0
        virtualButtons = 0
        rightStickButtons = 0
        rightStickDirections = 0
        axisDigitalState.clear()
        hotkeyKeys.clear()
        hotkeyAxisKeys.clear()
        consumedHotkeyKeys = emptySet()
        lastHatBits = 0
        lastHatKeyBits = 0
    }

    /**
     * Release every held button without touching stick state. Called when the emulator loses
     * focus (dialog, pause menu): Android can drop the matching KeyUp for a held button, which
     * would otherwise leave the bit latched forever.
     */
    fun releaseAllButtons() {
        lastHatBits = 0
        lastHatKeyBits = 0
        hotkeyKeys.clear()
        hotkeyAxisKeys.clear()
        consumedHotkeyKeys = emptySet()
        rightStickDirections = 0
        if (hwButtons != 0 || virtualButtons != 0 || rightStickButtons != 0) {
            InputLog.letGoOfAll(buttons)
            hwButtons = 0
            virtualButtons = 0
            rightStickButtons = 0
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
     * digital buttons with hysteresis and pushes the combined state. [pspRightStick] is Settings'
     * PSP Right Stick, for a PSP game.
     *
     * @return true if the event was a joystick move and was consumed.
     */
    fun handleMotionEvent(event: MotionEvent, mappings: Map<Int, String>, systemName: String, pspRightStick: PspRightStick): Boolean {
        if ((event.source and InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        val outer = EventAxes.event
        EventAxes.event = event
        try {
            onJoystickMove(EventAxes, mappings, systemName, pspRightStick)
        } finally {
            EventAxes.event = outer
        }
        return true
    }

    /** A joystick's axes by number (MotionEvent.AXIS_*). */
    internal fun interface AxisValues {
        fun value(axis: Int): Float
    }

    /** The motion event being handled, as [AxisValues]: one reader for every move. */
    private object EventAxes : AxisValues {
        var event: MotionEvent? = null
        override fun value(axis: Int) = event!!.getAxisValue(axis)
    }

    /** A joystick move, its axes read through [axisValue]: what [handleMotionEvent] does with one. */
    internal fun onJoystickMove(axisValue: AxisValues, mappings: Map<Int, String>, systemName: String, pspRightStick: PspRightStick) {
        val bindings = InputBindings.of(mappings)
        val hatActive = updateHatDpad(axisValue)
        updateHotkeyAxes(axisValue)

        val newLx = stickAxis(axisValue, bindings, PhobosCore.Input.LS_LEFT, PhobosCore.Input.LS_RIGHT, MotionEvent.AXIS_X)
        val newLy = stickAxis(axisValue, bindings, PhobosCore.Input.LS_UP, PhobosCore.Input.LS_DOWN, MotionEvent.AXIS_Y)
        val newRx = stickAxis(axisValue, bindings, PhobosCore.Input.RS_LEFT, PhobosCore.Input.RS_RIGHT, MotionEvent.AXIS_Z)
        val newRy = stickAxis(axisValue, bindings, PhobosCore.Input.RS_UP, PhobosCore.Input.RS_DOWN, MotionEvent.AXIS_RZ)

        // Only the N64 latches stick-as-button bits (17-24): its right stick drives the
        // C-buttons. Every other system leaves stick bits alone: the right stick is analog there
        // (e.g. the PS1 DualShock camera), or what the PSP's Right Stick makes it (updateSticks).
        val latchStickBits = systemName.contains("Nintendo 64", ignoreCase = true)

        var axisButtonPressed = false
        for (binding in bindings.axisBindings) {
            val hotkeyToken = axisHotkeyToken(binding)
            if (hotkeyToken != null && hotkeyToken in consumedHotkeyKeys) continue
            val isStickBit = binding.bit in FIRST_STICK_BIT..LAST_STICK_BIT
            if (isStickBit && !latchStickBits) continue
            val value = axisValue.value(binding.axis)
            val magnitude = if (binding.positive) value else -value
            val wasPressed = axisDigitalState[binding.bit] ?: false
            val pressed = axisPressed(magnitude, wasPressed)
            axisDigitalState[binding.bit] = pressed
            hwButtons = if (pressed) hwButtons or binding.bit else hwButtons and binding.bit.inv()
            if (pressed && !wasPressed) axisButtonPressed = true
        }

        updateSticks(newLx, newLy, newRx, newRy, systemName, pspRightStick)
        push()

        val stickActive = maxOf(abs(newLx), abs(newLy), abs(newRx), abs(newRy)) > PHYSICAL_ACTIVITY_THRESHOLD
        if (hatActive || axisButtonPressed || stickActive) onPhysicalInput?.invoke()
    }

    /**
     * Takes the sticks' positions, as the user's bindings give them, for a game of [systemName]; the
     * caller pushes. A PSP game has one analog stick, so [pspRightStick] can make the right stick
     * press buttons, or be that stick. The right stick still goes to the core as itself in every
     * choice: the PSP core hands it to games patched to read a second stick (ares/psp/kernel/ctrl.cpp).
     */
    internal fun updateSticks(newLx: Float, newLy: Float, newRx: Float, newRy: Float, systemName: String, pspRightStick: PspRightStick) {
        val rightStick = if (systemName == LaunchSystems.PSP) pspRightStick else PspRightStick.OFF
        rightStickDirections = stickDirections(newRx, newRy, rightStickDirections)
        rightStickButtons = rightStick.buttons(rightStickDirections)
        if (rightStick == PspRightStick.ANALOG) {
            lx = newRx; ly = newRy
        } else {
            lx = newLx; ly = newLy
        }
        rx = newRx; ry = newRy
    }

    /** Whether a button an axis drives is down at [magnitude], its direction's deflection, with the hysteresis above. */
    internal fun axisPressed(magnitude: Float, wasPressed: Boolean): Boolean =
        magnitude > if (wasPressed) RELEASE_THRESHOLD else PRESS_THRESHOLD

    /**
     * The directions (D-pad bits) a stick at [x], [y] holds, each latched from those it [held] as
     * [axisPressed] latches a button: a diagonal holds two, and a stick resting near the edge doesn't chatter.
     */
    internal fun stickDirections(x: Float, y: Float, held: Int): Int {
        fun latch(direction: Int, magnitude: Float) = if (axisPressed(magnitude, (held and direction) != 0)) direction else 0
        return latch(PhobosCore.Input.UP, -y) or latch(PhobosCore.Input.DOWN, y) or
            latch(PhobosCore.Input.LEFT, -x) or latch(PhobosCore.Input.RIGHT, x)
    }

    /** Push the combined sticks + buttons to the native core. */
    fun push() {
        val now = buttons
        if (now != pushedButtons) {
            InputLog.buttons(pushedButtons, now)
            pushedButtons = now
        }
        coreInput.send(lx, ly, rx, ry, now)
    }

    /** The buttons [push] last sent. */
    private var pushedButtons = 0

    /** What [push] sends the sticks and buttons to. */
    internal fun interface CoreInput {
        fun send(lx: Float, ly: Float, rx: Float, ry: Float, buttons: Int)
    }

    /** Where [push] sends the state: the native core (JVM tests, without the native library, replace it). */
    internal var coreInput = CoreInput { lx, ly, rx, ry, buttons -> PhobosCore.setInput(lx, ly, rx, ry, buttons) }

    /**
     * Latches the D-pad hat (AXIS_HAT_X/Y) into the gameplay D-pad bits and the hotkey key set.
     * Many controllers report the D-pad ONLY as a hat, so it must drive the core's D-pad bits
     * (0..3) for every system, not just hotkey combos.
     *
     * @return true when any hat direction is pressed.
     */
    private fun updateHatDpad(axisValue: AxisValues): Boolean {
        val hatX = axisValue.value(MotionEvent.AXIS_HAT_X)
        val hatY = axisValue.value(MotionEvent.AXIS_HAT_Y)
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
        if (dpadBits != lastHatKeyBits) {
            lastHatKeyBits = dpadBits
            hotkeyKeysVersion++
            onHotkeyKeysChanged?.invoke()
        }
        // After the callback: a hotkey may have taken the D-pad as a combo key.
        val effectiveDpadBits = dpadBits and consumedDpadMask()
        if (effectiveDpadBits != lastHatBits) {
            hwButtons = (hwButtons and lastHatBits.inv()) or effectiveDpadBits
            lastHatBits = effectiveDpadBits
            push()
        }
        return dpadBits != 0
    }

    private var lastHatBits = 0
    private var lastHatKeyBits = 0
    private val hotkeyAxisKeys = mutableSetOf<Int>()

    /** Re-applies hotkey consumption to the D-pad the hat itself is holding. */
    fun applyHotkeyConsumption() {
        val effective = lastHatKeyBits and consumedDpadMask()
        if (effective != lastHatBits) {
            hwButtons = (hwButtons and lastHatBits.inv()) or effective
            lastHatBits = effective
            push()
        }
    }

    private fun consumedDpadMask(): Int {
        val taken = consumedHotkeyKeys
        return (if (android.view.KeyEvent.KEYCODE_DPAD_UP in taken) 0 else PhobosCore.Input.UP) or
            (if (android.view.KeyEvent.KEYCODE_DPAD_DOWN in taken) 0 else PhobosCore.Input.DOWN) or
            (if (android.view.KeyEvent.KEYCODE_DPAD_LEFT in taken) 0 else PhobosCore.Input.LEFT) or
            (if (android.view.KeyEvent.KEYCODE_DPAD_RIGHT in taken) 0 else PhobosCore.Input.RIGHT)
    }

    /**
     * The trigger axes and the stick directions: their held tokens join [hotkeyKeys], so a combo can
     * use a trigger reported only as an axis, or a stick direction.
     */
    private fun updateHotkeyAxes(axisValue: AxisValues) {
        val tokens = CapturePolicy.hotkeyAxisTokens({ axis -> axisValue.value(axis) }, hotkeyAxisKeys).toSet()
        if (tokens == hotkeyAxisKeys) return
        hotkeyKeys.removeAll(hotkeyAxisKeys)
        hotkeyKeys.addAll(tokens)
        hotkeyAxisKeys.clear()
        hotkeyAxisKeys.addAll(tokens)
        hotkeyKeysVersion++
        onHotkeyKeysChanged?.invoke()
    }

    /** The hotkey token an axis binding's game input is suppressed by, if it is a hotkey key. */
    private fun axisHotkeyToken(binding: InputBindings.AxisBinding): Int? = when (binding.axis) {
        MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y -> null
        MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE -> android.view.KeyEvent.KEYCODE_BUTTON_L2
        MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS -> android.view.KeyEvent.KEYCODE_BUTTON_R2
        else -> HotkeyTokens.axis(binding.axis, binding.positive)
    }

    /**
     * Value of one stick axis from its half-axis bindings ([negBit] and [posBit]), falling back
     * to the platform default axis when neither half is bound to an axis.
     *
     * Raw pass-through, ares-style: no frontend dead zone. ares feeds raw ±32767 values to the
     * core, which applies its own authoritative dead zone and response curve; pre-dead-zoning
     * here would stack with the core's and make sticks feel unresponsive.
     */
    private fun stickAxis(axisValue: AxisValues, bindings: InputBindings, negBit: Int, posBit: Int, defaultAxis: Int): Float {
        var negValue = 0f
        var posValue = 0f
        bindings.axisFor(negBit)?.let { b ->
            val v = axisValue.value(b.axis)
            if (!b.positive && v < 0f) negValue = v
            else if (b.positive && v > 0f) negValue = -v
        }
        bindings.axisFor(posBit)?.let { b ->
            val v = axisValue.value(b.axis)
            if (b.positive && v > 0f) posValue = v
            else if (!b.positive && v < 0f) posValue = -v
        }
        if (negValue != 0f || posValue != 0f) return (negValue + posValue).coerceIn(-1f, 1f)
        return axisValue.value(defaultAxis).coerceIn(-1f, 1f)
    }
}
