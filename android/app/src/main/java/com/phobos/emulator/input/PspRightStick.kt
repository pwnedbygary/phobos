package com.phobos.emulator.input

import com.phobos.emulator.PhobosCore.Input

/**
 * The PSP's "Right stick" setting: what a controller's right stick does in a PSP game, which has one analog stick. As
 * buttons, its four directions are latched as the N64's C-buttons are ([GameInputState.stickDirections]), so a
 * diagonal presses two.
 */
enum class PspRightStick(val label: String, val description: String) {
    OFF("Off", "Off: it does nothing."),
    FACE_BUTTONS(
        "Face buttons",
        "Face buttons: up presses Triangle, right Circle, down Cross and left Square, to aim and fire in twin-stick shooters such as Super Stardust Portable.",
    ),
    DPAD("D-pad", "D-pad: it presses the D-pad's directions, for games that turn the camera with the D-pad."),
    ANALOG("Analog stick", "Analog stick: it's the PSP's stick, and the left stick does nothing."),
    ;

    /** The buttons the stick presses while it holds [directions] (D-pad bits). */
    fun buttons(directions: Int): Int = when (this) {
        // The core reads Triangle, Circle, Cross and Square from the Y, B, A and X bits (PhobosRunner's resolveButtonBit).
        FACE_BUTTONS -> (if ((directions and Input.UP) != 0) Input.Y else 0) or
            (if ((directions and Input.RIGHT) != 0) Input.B else 0) or
            (if ((directions and Input.DOWN) != 0) Input.A else 0) or
            (if ((directions and Input.LEFT) != 0) Input.X else 0)
        DPAD -> directions
        OFF, ANALOG -> 0
    }
}
