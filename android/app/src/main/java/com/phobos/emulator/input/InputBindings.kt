package com.phobos.emulator.input

import android.view.KeyEvent
import com.phobos.emulator.PhobosCore

/**
 * Parsed form of the user's controller mappings, `bit -> "k:KEYCODE"` or `bit -> "a:AXIS:DIR"`
 * (DIR 1 = positive half of the axis, 0 = negative half).
 *
 * Parsing once per mappings change keeps string handling off the per-event input path.
 */
class InputBindings private constructor(private val source: Map<Int, String>) {
    class AxisBinding(val bit: Int, val axis: Int, val positive: Boolean)

    private val keyBits = HashMap<Int, Int>()
    private val axisByBit = HashMap<Int, AxisBinding>()

    /** Every axis-driven binding, for latching triggers, hats and stick-as-button bits. */
    val axisBindings: List<AxisBinding>

    init {
        val axes = ArrayList<AxisBinding>()
        for ((bit, binding) in source) {
            when {
                binding.startsWith("k:") -> {
                    val keyCode = binding.substring(2).toIntOrNull() ?: continue
                    keyBits[keyCode] = (keyBits[keyCode] ?: 0) or bit
                }
                binding.startsWith("a:") -> {
                    val parts = binding.split(':')
                    val axis = parts.getOrNull(1)?.toIntOrNull() ?: continue
                    val axisBinding = AxisBinding(bit, axis, parts.getOrNull(2) == "1")
                    axes += axisBinding
                    axisByBit[bit] = axisBinding
                }
            }
        }
        axisBindings = axes
    }

    /** OR of every core bit bound to [keyCode] (0 when unbound). */
    fun bitsForKey(keyCode: Int): Int = keyBits[keyCode] ?: 0

    fun axisFor(bit: Int): AxisBinding? = axisByBit[bit]

    companion object {
        @Volatile
        private var cached: InputBindings? = null

        /** Bindings for [mappings], re-parsed only when a new mappings map is published. */
        fun of(mappings: Map<Int, String>): InputBindings {
            val current = cached
            if (current != null && current.source === mappings) return current
            return InputBindings(mappings).also { cached = it }
        }
    }
}

/** Default core bit for a gamepad keycode (used by the ZX keyboard rebind capture). */
fun mapKeyCodeToBit(code: Int): Int = when (code) {
    KeyEvent.KEYCODE_BUTTON_A -> PhobosCore.Input.A
    KeyEvent.KEYCODE_BUTTON_B -> PhobosCore.Input.B
    KeyEvent.KEYCODE_BUTTON_X -> PhobosCore.Input.X
    KeyEvent.KEYCODE_BUTTON_Y -> PhobosCore.Input.Y
    KeyEvent.KEYCODE_BUTTON_L1 -> PhobosCore.Input.L1
    KeyEvent.KEYCODE_BUTTON_R1 -> PhobosCore.Input.R1
    KeyEvent.KEYCODE_BUTTON_L2 -> PhobosCore.Input.L2
    KeyEvent.KEYCODE_BUTTON_R2 -> PhobosCore.Input.R2
    KeyEvent.KEYCODE_BUTTON_SELECT -> PhobosCore.Input.SELECT
    KeyEvent.KEYCODE_BUTTON_START -> PhobosCore.Input.START
    KeyEvent.KEYCODE_DPAD_UP -> PhobosCore.Input.UP
    KeyEvent.KEYCODE_DPAD_DOWN -> PhobosCore.Input.DOWN
    KeyEvent.KEYCODE_DPAD_LEFT -> PhobosCore.Input.LEFT
    KeyEvent.KEYCODE_DPAD_RIGHT -> PhobosCore.Input.RIGHT
    else -> 0
}
