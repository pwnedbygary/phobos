package com.phobos.emulator.input

import android.view.MotionEvent
import android.view.InputDevice
import kotlin.math.abs

/**
 * Mimics the Ares "VirtualPad" architecture.
 * Handles the mapping from raw hardware inputs (MotionEvent, KeyEvents) to 
 * a set of virtual input nodes (D-Pad, Sticks, Triggers).
 *
 * This class handles:
 * 1. Hardware to Virtual Mapping
 * 2. Deadzone filtering (Ares standard: 6.0)
 * 3. Saturation Radius mapping
 * 4. Isolation of Stick inputs from D-Pad inputs
 */
class InputProcessor(private val mappings: Map<Int, String>) {
    var lx = 0f
    var ly = 0f
    var rx = 0f
    var ry = 0f
    var buttons = 0
    
    private val deadzone = 0.05f
    private val saturationRadius = 32767f
    private val dpadMask = (1 shl 0) or (1 shl 1) or (1 shl 2) or (1 shl 3)

    private val axisMappings = mappings.filter { it.value.startsWith("a:") }.mapValues {
        val parts = it.value.split(":")
        Pair(parts[1].toInt(), parts[2] == "1")
    }

    /**
     * Processes a MotionEvent from a joystick source.
     * Updates the internal virtual state.
     *
     * @param event The raw MotionEvent
     * @param currentButtons The current button bitmask from hardware
     * @return The updated button bitmask
     */
    fun processMotionEvent(event: MotionEvent, currentButtons: Int): Int {
        var newButtons = currentButtons
        
        if (event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK &&
            event.action == MotionEvent.ACTION_MOVE) {
            
            var newLx = 0f
            var newLy = 0f
            var newRx = 0f
            var newRy = 0f

            axisMappings.forEach { (bit, pair) ->
                val axis = pair.first
                val pos = pair.second
                val rawValue = event.getAxisValue(axis)
                
                val isStickBit = bit >= (1 shl 17) && bit <= (1 shl 24)
                val isDpadBit = bit >= (1 shl 0) && bit <= (1 shl 3)

                if (isStickBit) {
                    // Apply Ares deadzone and radius mapping
                    val filteredValue = if (abs(rawValue) < deadzone) 0f else rawValue
                    
                    // Map to specific axes
                    when(bit) {
                        1 shl 17 -> if (!pos && filteredValue < 0f) newLy += filteredValue
                        1 shl 18 -> if (pos && filteredValue > 0f) newLy += filteredValue
                        1 shl 19 -> if (!pos && filteredValue < 0f) newLx += filteredValue
                        1 shl 20 -> if (pos && filteredValue > 0f) newLx += filteredValue
                        1 shl 21 -> if (!pos && filteredValue < 0f) newRy += filteredValue
                        1 shl 22 -> if (pos && filteredValue > 0f) newRy += filteredValue
                        1 shl 23 -> if (!pos && filteredValue < 0f) newRx += filteredValue
                        1 shl 24 -> if (pos && filteredValue > 0f) newRx += filteredValue
                    }
                } else {
                    // Non-stick digital bits
                    val isMainStickAxis = axis == MotionEvent.AXIS_X || axis == MotionEvent.AXIS_Y ||
                                       axis == MotionEvent.AXIS_Z || axis == MotionEvent.AXIS_RZ
                    
                    if (!(isDpadBit && isMainStickAxis)) {
                        val pressed = if (pos) rawValue > 0.4f else rawValue < -0.4f
                        newButtons = if (pressed) newButtons or bit else newButtons and bit.inv()
                    }
                }
            }
            
            // Normalize and Clamp
            lx = newLx.coerceIn(-1f, 1f)
            ly = newLy.coerceIn(-1f, 1f)
            rx = newRx.coerceIn(-1f, 1f)
            ry = newRy.coerceIn(-1f, 1f)

            // Isolation: If sticks are moving, purge D-Pad bits to prevent interference
            val hasStickDisplacement = abs(lx) > 0.05f || abs(ly) > 0.05f || 
                                     abs(rx) > 0.05f || abs(ry) > 0.05f
            if (hasStickDisplacement) {
                newButtons = newButtons and dpadMask.inv()
            }

            buttons = newButtons
        }
        return buttons
    }

    /**
     * Processes a KeyEvent for digital inputs.
     *
     * @param keyCode The key code
     * @param isDown Whether the key is pressed
     * @param currentButtons The current button bitmask
     * @return The updated button bitmask
     */
    fun processKeyEvent(keyCode: Int, isDown: Boolean, currentButtons: Int): Int {
        var newButtons = currentButtons
        
        mappings.forEach { (bit, binding) ->
            if (binding == "k:$keyCode") {
                newButtons = if (isDown) newButtons or bit else newButtons and bit.inv()
            }
        }
        
        return newButtons
    }
}