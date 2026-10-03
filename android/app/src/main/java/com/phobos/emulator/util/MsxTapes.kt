package com.phobos.emulator.util

/** Files mia's MSX medium reads as tapes (mia/medium/msx.cpp). */
val MSX_TAPE_EXTENSIONS = setOf("cas", "wav", "tzx", "tsx")

/**
 * The firmware an MSX game in [fileName] still lacks: a tape loads through BASIC, which only a real MSX BIOS has
 * (the bundled C-BIOS has none), so without one the game can't load. [mapped] holds the firmware keys this load
 * handed to native code. Cartridge games run on C-BIOS and need nothing.
 */
fun msxTapeFirmware(system: String, fileName: String, mapped: Set<String>): List<String> {
    if (fileName.substringAfterLast('.', "").lowercase() !in MSX_TAPE_EXTENSIONS) return emptyList()
    return when (system) {
        "MSX" -> if ("fw_msx" in mapped) emptyList() else listOf("fw_msx_basic")
        "MSX2" -> if ("fw_msx2_main" in mapped && "fw_msx2_sub" in mapped) emptyList() else listOf("fw_msx2_basic")
        else -> emptyList()
    }
}

/**
 * The MSX's deck as the tape controls show it: the [tape] in it, whether that's the game's [dataTape] (a .wav kept
 * with its saves), and its recording, which the motor relay starts and stops while [recordArmed].
 */
data class MsxDeck(
    val tape: ZxTape = ZxTape(),
    val dataTape: Boolean = false,
    val recordArmed: Boolean = false,
    val recording: Boolean = false,
) {
    /** What the deck is doing: "Recording, 0:12", "Blank", or the tape's own status. */
    val status: String get() = when {
        recording -> "Recording, ${ZxTape.time(tape.positionMs)}"
        dataTape && tape.lengthMs == 0 -> if (recordArmed) "Blank; records when the MSX saves" else "Blank"
        recordArmed -> "Records after the ${ZxTape.time(tape.lengthMs)} already on it"
        else -> tape.status
    }

    companion object {
        /** From PhobosCore.getMsxTapeState(). */
        fun of(state: IntArray): MsxDeck = MsxDeck(
            tape = ZxTape.of(state),
            dataTape = state.getOrNull(4) == 1,
            recordArmed = state.getOrNull(5) == 1,
            recording = state.getOrNull(6) == 1,
        )
    }
}
