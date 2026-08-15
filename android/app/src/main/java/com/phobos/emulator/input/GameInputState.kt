package com.phobos.emulator.input

import android.util.Log
import android.view.InputDevice
import android.view.MotionEvent
import com.phobos.emulator.PhobosCore
import kotlin.math.abs

/**
 * Single shared source of truth for the current virtual controller state.
 *
 * Every input path writes into this object — the Activity-level joystick
 * fallback (MainActivity), the emulator screen's key handler, its SurfaceView
 * motion listener and the on-screen VirtualGamepad — and the combined state is
 * pushed to the native core in a single JNI call.
 *
 * This prevents the classic bug where one input path (e.g. stick motion) pushes
 * a button mask of zero and silently clears buttons held through another path
 * (e.g. hardware key events), which manifested as dropped / "rapid fire" inputs
 * whenever a stick was moved while a button was held.
 */
object GameInputState {
    var lx = 0f
    var ly = 0f
    var rx = 0f
    var ry = 0f
    var hwButtons = 0
    var virtualButtons = 0

    // Hotkey support: many controllers report the D-pad as HAT axes (motion
    // events), not KEYCODE_DPAD_* keys, so the EmulatorScreen's key-only
    // pressedKeys tracker never sees the D-pad. This shared set holds the
    // currently-pressed virtual keycodes (D-pad from hats), so hotkey combos
    // like Z + D-pad-Right can match. Versioned so EmulatorScreen can observe
    // changes and re-run the combo check when the hat moves.
    val hotkeyKeys = mutableSetOf<Int>()
    var hotkeyKeysVersion = 0
        private set
    // Callback the EmulatorScreen sets to re-evaluate hotkey combos when the
    // D-pad hat changes (a motion event doesn't otherwise re-run the check).
    var onHotkeyKeysChanged: (() -> Unit)? = null

    private const val HAT_PRESS_THRESHOLD = 0.5f

    /** Update hotkeyKeys from the D-pad hat axes (AXIS_HAT_X/Y). */
    private fun updateHotkeyDpad(event: MotionEvent) {
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        // D-pad keycodes: UP=19 DOWN=20 LEFT=21 RIGHT=22
        val left = hatX < -HAT_PRESS_THRESHOLD
        val right = hatX > HAT_PRESS_THRESHOLD
        val up = hatY < -HAT_PRESS_THRESHOLD
        val down = hatY > HAT_PRESS_THRESHOLD
        fun setDpad(key: Int, pressed: Boolean) { if (pressed) hotkeyKeys.add(key) else hotkeyKeys.remove(key) }
        setDpad(21, left); setDpad(22, right); setDpad(19, up); setDpad(20, down)
        hotkeyKeysVersion++
        onHotkeyKeysChanged?.invoke()
    }

    // Ares parity: analog stick values pass through RAW — no frontend deadzone.
    // ares desktop feeds the raw ±32767 HID values straight into the core,
    // which applies its own authoritative deadzone and response curve (e.g. PS1
    // DualShock innerDeadzone = 6.0 / ~4.7%). Pre-deadzoning here stacks with
    // the core deadzone and makes sticks feel like they "barely register".

    // Hysteresis thresholds for axis-mapped digital buttons (triggers, D-Pad
    // hats, stick-as-buttons). The press threshold matches ares' digital
    // qualifier threshold (±16384 / 32767 ≈ 50% deflection). A button engages
    // above PRESS_THRESHOLD and only disengages below RELEASE_THRESHOLD, so a
    // noisy axis hovering near the boundary can't rapidly toggle the bit (the
    // PS1 "rapid fire" symptom). ares has no release hysteresis — this is a
    // deliberate, documented deviation for Android's noisier controller axes.
    private const val PRESS_THRESHOLD = 0.5f
    private const val RELEASE_THRESHOLD = 0.4f

    // Ares parity: ares desktop's InputManager polls HID devices at a fixed
    // 200 Hz (pollFrequency = 5 ms) and the cores sample that polled state when
    // the emulated hardware polls the controller. Android delivers MotionEvents
    // at the controller's own rate (up to ~500 Hz), so coalesce JNI pushes to
    // the same 5 ms cadence: the core never sees state older than one poll
    // interval and we don't flood JNI with redundant calls. Button state
    // changes force an immediate push.
    private const val POLL_INTERVAL_NANOS = 5_000_000L
    private var lastPushNanos = 0L

    /** Last latched press state per digital bit driven by an axis binding. */
    private val axisDigitalState = mutableMapOf<Int, Boolean>()

    private var logCounter = 0

    val buttons: Int get() = hwButtons or virtualButtons

    /** Reset all state (used when leaving the emulator / starting a new session). */
    fun reset() {
        lx = 0f; ly = 0f; rx = 0f; ry = 0f
        hwButtons = 0
        virtualButtons = 0
        axisDigitalState.clear()
        lastPushNanos = 0L
    }

    /**
     * Release every held button without touching stick state. Called when the
     * emulator loses focus (dialog, pause menu, screen tap): Android can drop
     * the matching KeyUp for a held button, which would otherwise leave the bit
     * latched forever — the game then sees a permanently-held button (twitch /
     * random swings / rapid-fire).
     */
    fun releaseAllButtons() {
        if (hwButtons != 0 || virtualButtons != 0) {
            hwButtons = 0
            virtualButtons = 0
            axisDigitalState.clear()
            push(force = true)
        }
    }

    /** Toggle a hardware-mapped digital button and push the combined state. */
    fun setButton(bit: Int, down: Boolean) {
        hwButtons = if (down) hwButtons or bit else hwButtons and bit.inv()
        push(force = true)
    }

    /** Replace the full on-screen (virtual) button mask and push. */
    fun updateVirtualButtons(mask: Int) {
        virtualButtons = mask
        push(force = true)
    }

    /**
     * Processes a joystick MotionEvent using the user's input mappings.
     *
     * Resolves each stick axis through its configured binding (falling back to
     * the platform default axis), applies deadzone/saturation, updates
     * axis-mapped digital buttons (triggers, D-Pad hats, stick-as-buttons) and
     * pushes the combined state to the core.
     *
     * @return true if the event was a joystick move and was consumed.
     */
    fun handleMotionEvent(event: MotionEvent, mappings: Map<Int, String>, systemName: String): Boolean {
        if ((event.source and InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        updateHotkeyDpad(event)  // D-pad hats → virtual keycodes for hotkeys

        val nLx = getAxisValueForStick(event, mappings, 1 shl 19, 1 shl 20, MotionEvent.AXIS_X)
        val nLy = getAxisValueForStick(event, mappings, 1 shl 17, 1 shl 18, MotionEvent.AXIS_Y)
        val nRx = getAxisValueForStick(event, mappings, 1 shl 23, 1 shl 24, MotionEvent.AXIS_Z)
        val nRy = getAxisValueForStick(event, mappings, 1 shl 21, 1 shl 22, MotionEvent.AXIS_RZ)

        // Only the N64 latches stick-as-button bits (17-24) — its right stick drives
        // the C-buttons (C-Up/Down/Left/Right → RS_Up/Down/Left/Right). Every other
        // system treats the right stick as purely analog (e.g. PS1 DualShock camera),
        // so stick bits are skipped there to avoid touching the analog path at all.
        val latchStickBits = systemName.contains("Nintendo 64", ignoreCase = true)

        // Non-stick axis bindings behave as digital buttons (e.g. L2/R2 triggers,
        // D-Pad on hat axes). For N64, stick-axis bindings ALSO latch as digital
        // buttons (the C-buttons) with the same hysteresis as triggers — matches
        // ares' InputAnalog digital qualifiers; deflection past the threshold
        // presses the button, hysteresis prevents chatter.
        mappings.forEach { (bit, binding) ->
            if (binding.startsWith("a:")) {
                val parts = binding.split(":")
                val axisId = parts.getOrNull(1)?.toIntOrNull() ?: return@forEach
                val pos = parts.getOrNull(2) == "1"
                val value = event.getAxisValue(axisId)
                val isStickBit = bit >= (1 shl 17) && bit <= (1 shl 24)
                if (isStickBit && !latchStickBits) return@forEach
                // Hysteresis: engage above PRESS_THRESHOLD, disengage below
                // RELEASE_THRESHOLD. Prevents a noisy axis hovering near the
                // boundary from toggling the bit on/off rapidly (PS1 rapid-fire).
                val magnitude = if (pos) value else -value
                val wasPressed = axisDigitalState[bit] ?: false
                val pressed = if (wasPressed) {
                    magnitude > RELEASE_THRESHOLD
                } else {
                    magnitude > PRESS_THRESHOLD
                }
                axisDigitalState[bit] = pressed
                hwButtons = if (pressed) hwButtons or bit else hwButtons and bit.inv()
            }
        }

        lx = nLx.coerceIn(-1f, 1f)
        ly = nLy.coerceIn(-1f, 1f)
        rx = nRx.coerceIn(-1f, 1f)
        ry = nRy.coerceIn(-1f, 1f)
        push()
        return true
    }

    /** Push the combined sticks + buttons to the native core. */
    fun push(force: Boolean = false) {
        // NOTE: The previous "stick isolation" logic purged all D-Pad bits
        // (1<<0..1<<3) whenever ANY analog stick was displaced beyond
        // STICK_ISOLATION_THRESHOLD. In ares, the D-Pad and the analog sticks
        // are independent virtual inputs (D-Pad = bits 0-3, sticks = bits
        // 17-24), so the purge only ever dropped legitimate D-Pad presses while
        // the user was simultaneously using the analog stick. It has been
        // removed; D-Pad input now flows exclusively through key events (k:),
        // hat axes (a:15/a:16) and the on-screen gamepad, matching ares' model.
        val fBtn = buttons

        // Coalesce to ares' 200 Hz poll cadence unless forced (button change).
        val now = System.nanoTime()
        if (!force && now - lastPushNanos < POLL_INTERVAL_NANOS) return
        lastPushNanos = now

        if (logCounter++ % 120 == 0) {
            if (abs(lx) > 0.05f || abs(ly) > 0.05f || abs(rx) > 0.05f || abs(ry) > 0.05f || fBtn != 0) {
                Log.i("PhobosInput", "JNI Push: LS(%.2f, %.2f) RS(%.2f, %.2f) BTNS=%08x".format(lx, ly, rx, ry, fBtn))
            }
        }
        PhobosCore.setInput(lx, ly, rx, ry, fBtn)
    }

    /**
     * Resolves the value of a stick axis from the user's bindings, falling back
     * to the platform default axis when the stick isn't (re)mapped.
     */
    private fun getAxisValueForStick(
        event: MotionEvent,
        mappings: Map<Int, String>,
        negBit: Int,
        posBit: Int,
        defaultAxis: Int
    ): Float {
        val negBinding = mappings[negBit]
        val posBinding = mappings[posBit]

        var negValue = 0f
        var posValue = 0f

        if (negBinding != null && negBinding.startsWith("a:")) {
            val parts = negBinding.split(":")
            val axisId = parts[1].toInt()
            val pos = parts[2] == "1"
            val v = event.getAxisValue(axisId)
            // Raw pass, ares-style: InputAnalog sums abs(value) for the Lo/Hi
            // half bindings with no frontend threshold; the core deadzones.
            if (!pos && v < 0f) negValue = v
            else if (pos && v > 0f) negValue = -v
        }

        if (posBinding != null && posBinding.startsWith("a:")) {
            val parts = posBinding.split(":")
            val axisId = parts[1].toInt()
            val pos = parts[2] == "1"
            val v = event.getAxisValue(axisId)
            if (pos && v > 0f) posValue = v
            else if (!pos && v < 0f) posValue = -v
        }

        if (negValue != 0f || posValue != 0f) {
            return (negValue + posValue).coerceIn(-1f, 1f)
        }

        // Unmapped stick: raw platform axis value (Android already normalizes
        // it to ±1); the core applies its authoritative deadzone.
        return event.getAxisValue(defaultAxis).coerceIn(-1f, 1f)
    }
}
