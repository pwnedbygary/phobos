package com.phobos.emulator.ui

import kotlinx.coroutines.delay

/** The core's keyboard matrix as the ZX Spectrum keyboard drives it: PhobosCore's in the app, a fake in the tests. */
interface ZxKeyboardCore {
    /** Holds or lets go of the key with this matrix label. */
    fun setKey(label: String, pressed: Boolean)

    /** Lets go of every key the on-screen controls hold, this keyboard's or not. */
    fun releaseAll()
}

/**
 * The keys one ZX Spectrum keyboard holds down in the core. A press lets go of its keys however its gesture ends: the
 * finger lifted, the gesture cancelled as the keyboard leaves the composition, or an exception. [releaseHeld] lets go
 * of whatever is still held as the keyboard goes away. The SYMBOL SHIFT and CAPS SHIFT latches, set by [latch], aren't
 * held keys: they stay set, as EmulatorScreen keeps their state while the keyboard is hidden.
 * Called from the keyboard's gesture handlers and its disposal, all on the main thread.
 */
class ZxKeyPresses(
    private val core: ZxKeyboardCore,
    private val pause: suspend (Long) -> Unit = { delay(it) },
) {
    private val held = mutableSetOf<String>()

    /** Holds [keys] down while [whilePressed] runs, and lets go of them however it ends. */
    suspend fun hold(keys: List<String>, whilePressed: suspend () -> Unit) {
        for (key in keys) {
            held += key
            core.setKey(key, true)
        }
        try {
            whilePressed()
        } finally {
            for (key in keys) {
                if (held.remove(key)) core.setKey(key, false)
            }
        }
    }

    /** Sets or clears a shift latch. */
    fun latch(key: String, latched: Boolean) = core.setKey(key, latched)

    /** Lets go of every key a press still holds; the latches stay as they are. */
    fun releaseHeld() {
        val keys = held.toList()
        held.clear()
        for (key in keys) core.setKey(key, false)
    }

    /**
     * Types [steps] as LOAD "" does. First every key held on screen is let go of, this keyboard's and the touch
     * controls': a held SPACE is BREAK to the 48K's tape loader, and the ROM refuses a key typed while another is held.
     * Then [clearShifts] runs, and each step's keys go down together as a quick blip WITHOUT waiting for the finger to
     * lift (a key held on the ZX ROM repeats, which can type a neighbour: J held -> 'L'), 90 ms apart so chords stay
     * distinct.
     */
    suspend fun type(steps: List<List<String>>, clearShifts: () -> Unit) {
        core.releaseAll()
        held.clear()
        clearShifts()
        pause(40)
        for (step in steps) {
            hold(step) { pause(40) }
            pause(90)
        }
    }
}
