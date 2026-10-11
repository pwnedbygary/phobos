package com.phobos.emulator.ui

/**
 * Resolves a library system name to the platform-art slug shared by asset and Canvas icon packs.
 *
 * The established names intentionally match the Systematic SVG basenames where those assets exist.
 */
fun systemIconSlug(system: String): String? {
    return when {
        system.contains("Neo Geo Pocket Color", ignoreCase = true) -> "neo-geo-pocket-color"
        system.contains("Neo Geo Pocket", ignoreCase = true) -> "neo-geo-pocket"
        system.contains("Neo Geo", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "neo-geo-cd"
        system.contains("Neo Geo", ignoreCase = true) -> "neogeomvs"
        system.contains("Mega CD 32X", ignoreCase = true) ||
            system.contains("Sega 32X", ignoreCase = true) ||
            system.contains("32X", ignoreCase = true) -> "sega32"
        system.contains("Mega Drive", ignoreCase = true) || system.contains("Genesis", ignoreCase = true) -> "genesis"
        system.contains("Mega CD", ignoreCase = true) -> "segacd"
        system.contains("SNES", ignoreCase = true) || system.contains("Super Famicom", ignoreCase = true) -> "snes"
        system.contains("Super Game Boy", ignoreCase = true) -> "sgb"
        system.contains("NES", ignoreCase = true) || system.contains("Famicom", ignoreCase = true) -> "nes"
        system.contains("Nintendo 64", ignoreCase = true) -> "n64"
        system.contains("Game Boy Advance", ignoreCase = true) -> "gba"
        system.contains("Game Boy Color", ignoreCase = true) -> "gbc"
        system.contains("Game Boy", ignoreCase = true) -> "gb"
        system.contains("PlayStation Portable", ignoreCase = true) -> "psp"
        system.contains("PlayStation", ignoreCase = true) -> "psx"
        system.contains("Game Gear", ignoreCase = true) -> "gamegear"
        system.contains("MSX2", ignoreCase = true) -> "msx2"
        system.contains("MSX", ignoreCase = true) -> "msx"
        system.contains("PC Engine LD", ignoreCase = true) -> "pceld"
        system.contains("PC Engine", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "pcecd"
        system.contains("PC Engine", ignoreCase = true) ||
            system.contains("PCE", ignoreCase = true) ||
            system.contains("TurboGrafx", ignoreCase = true) -> "pce"
        system.contains("Master System", ignoreCase = true) ||
            system.contains("Mark III", ignoreCase = true) ||
            system.contains("SG-1000", ignoreCase = true) -> "sms"
        system.contains("ColecoVision", ignoreCase = true) -> "colecovision"
        system.contains("Atari 2600", ignoreCase = true) -> "atari2600"
        system.contains("Mega LD", ignoreCase = true) || system.contains("LaserActive", ignoreCase = true) -> "laseractive"
        system.contains("SuperGrafx", ignoreCase = true) -> "supergrafx"
        system.contains("WonderSwan Color", ignoreCase = true) -> "wonderswan-color"
        system.contains("WonderSwan", ignoreCase = true) -> "wonderswan"
        system.contains("Pocket Challenge", ignoreCase = true) -> "pcv2"
        system.contains("ZX Spectrum 128", ignoreCase = true) || system.contains("ZX Spectrum", ignoreCase = true) -> "zx-spectrum"
        system.contains("Arcade", ignoreCase = true) -> "arcade"
        else -> null
    }
}

/** Every platform slug emitted by [systemIconSlug]. */
val systemIconSlugs: Set<String> = setOf(
    "atari2600", "colecovision", "nes", "snes", "sgb", "arcade", "laseractive", "pceld",
    "n64", "gb", "gbc", "gba", "sms", "genesis", "sega32", "gamegear", "segacd", "psx", "psp",
    "neogeomvs", "neo-geo-cd", "neo-geo-pocket", "neo-geo-pocket-color", "zx-spectrum",
    "pce", "pcecd", "supergrafx", "wonderswan", "wonderswan-color", "pcv2", "msx", "msx2",
)
