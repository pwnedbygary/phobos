package com.phobos.emulator.util

import java.util.Locale

/** The ZX Spectrum's tape as the tape controls show it. */
data class ZxTape(
    val inserted: Boolean = false,
    val playing: Boolean = false,
    val positionMs: Int = 0,
    val lengthMs: Int = 0,
) {
    /** How far the tape has played, 0 to 1. */
    val progress: Float get() = if (lengthMs > 0) (positionMs.toFloat() / lengthMs).coerceIn(0f, 1f) else 0f

    /** Stopped part way through, as a multi-load game leaves it between its parts. */
    val paused: Boolean get() = inserted && !playing && positionMs in 1 until lengthMs

    /** What the tape is doing, with the counter: "Stopped at 1:23 of 4:56". */
    val status: String get() = when {
        !inserted -> "No tape"
        playing -> "Playing, ${time(positionMs)} of ${time(lengthMs)}"
        positionMs <= 0 -> "At the start, ${time(lengthMs)} long"
        positionMs >= lengthMs -> "At the end"
        else -> "Stopped at ${time(positionMs)} of ${time(lengthMs)}"
    }

    companion object {
        /** From PhobosCore.getZxTapeState(): [in, playing, position ms, length ms]. */
        fun of(state: IntArray): ZxTape =
            if (state.size >= 4 && state[0] != 0) ZxTape(true, state[1] != 0, state[2], state[3]) else ZxTape()

        /** A tape counter's minutes and seconds. */
        fun time(ms: Int): String {
            val seconds = ms.coerceAtLeast(0) / 1000
            return String.format(Locale.US, "%d:%02d", seconds / 60, seconds % 60)
        }
    }
}
