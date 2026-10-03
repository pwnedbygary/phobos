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
