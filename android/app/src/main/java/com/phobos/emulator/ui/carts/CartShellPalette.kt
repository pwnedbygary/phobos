package com.phobos.emulator.ui.carts

/**
 * The plastic a baked shell is made of when the game has no look of its own. Cartridge's shells carry their
 * colours in their materials, but Phobos's per-game look overrides `shell` with one default for every game; this
 * table puts the real plastics back. The shapes are the 4a outputs under `assets/carts` (plus the cassette case),
 * so the table is also the list the asset test checks.
 */
object CartShellPalette {

    private val colours: Map<String, String> = mapOf(
        "famicom" to "#c63a2e",   // Famicom cartridges: red with a cream label frame
        "sfc" to "#9a94a8",       // Super Famicom: light grey plastic
        "gb" to "#8a8f98",        // Game Boy: grey
        "gbc" to "#4f6d8a",       // Game Boy Color: teal, the common opaque shell
        "gba" to "#5b4b9e",       // Game Boy Advance: indigo
        "gba-badge" to "#5b4b9e", // the GBA shell with the badge engraving
        "gba-lines" to "#5b4b9e", // the GBA shell with the line engraving
        "n64" to "#3b3c42",       // Nintendo 64: charcoal
        "sms" to "#1d1d21",       // Master System: black
        "md" to "#1d1d21",        // Mega Drive: black
        "small" to "#3b3c42",     // Game Gear / Neo Geo Pocket / WonderSwan class: charcoal
        "aes" to "#1d1d21",       // Neo Geo AES: black
        "hucard" to "#e8e4da",    // PC Engine HuCard: cream
        "a26" to "#1d1d21",       // Atari 2600: black
        "pcb" to "#1f5a3a",       // Arcade board: dark green
        "tape" to "#1d1d21",      // ZX Spectrum cassette case: black
        "cd" to "#2a2a30",        // CD jewel case / disc: near-black
        "psx" to "#1d1d21",       // PlayStation disc: black
        "minidisc" to "#6a6a70",  // PSP's nearest caddied disc: grey
    )

    /** The shell colour for [shape], or Cartridge's generic plastic when the shape is unknown. */
    fun forShape(shape: String?): String = colours[shape] ?: "#6f4fb0"

    /** Every shape with a colour, for the asset/registry test. */
    internal fun shapes(): Set<String> = colours.keys
}
