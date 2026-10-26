package com.phobos.emulator.input

/**
 * The keys that put the screensaver away (MainActivity's window, after the hotkeys' own keys). While it shows, every
 * key is its own and puts it away; a key pressed then stays its own until let go of, and so does every key pressed
 * while one of those is still held, its repeats and release with it. So the rest of a combo pressed to put it away
 * (Back + Start, which shows it, pressed again: Back's press puts it away) reaches neither the pages nor, as the
 * fallback Android makes of a key they leave (Start's centre), the item they have focused. The volume keys are the
 * system's, and once the screensaver's gone a key's new press decides afresh, its last release lost or not. Used from
 * the main thread.
 */
class ScreensaverKeys {
    /** The keys held that were pressed with the screensaver showing, or while another of them was held. */
    private val held = mutableSetOf<Int>()

    /** A key event ([down]: a press, repeats included) with the screensaver [showing] or not; true when it's taken. */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int, showing: Boolean): Boolean {
        if (keyCode in VOLUME_KEYS) return false
        val press = down && repeatCount == 0
        if (press && !showing) held -= keyCode
        when {
            showing -> if (press) held += keyCode else if (!down) held -= keyCode
            keyCode in held -> if (!down) held -= keyCode
            press && held.isNotEmpty() -> held += keyCode
            else -> return false
        }
        return true
    }

    /** Forgets the keys held: a controller went away, so their releases won't come. */
    fun reset() = held.clear()
}
