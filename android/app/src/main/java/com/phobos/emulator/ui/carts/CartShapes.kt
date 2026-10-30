package com.phobos.emulator.ui.carts

import com.phobos.emulator.art.LabelCrop

/**
 * Phobos system names mapped to Cartridge 0.4.9.3's systems (electron/systems.js): the entry used for label scans
 * (its system id keys the bundled label index, and its media decides the crop shape exactly as scraper.js does)
 * and the 4a-baked 3D shell each system shows.
 *
 * Systems Cartridge does not have keep console art (no [cartShape]) and take no label scans (no [entry]); the
 * owner's nearest-shape choices for existing shapes are the scope's (SG-1000 -> Master System, Neo Geo CD -> CD,
 * SuperGrafx -> HuCard, Pocket Challenge V2 -> WonderSwan Color, ColecoVision -> Atari 2600).
 */
object CartShapes {

    /** A Cartridge system entry: its id (label-index key), media type ('cart'/'card'/'disc') and media shape. */
    data class Entry(val id: String, val mediaType: String, val mediaShape: String)

    private val entries: Map<String, Entry> = mapOf(
        "Famicom" to Entry("nes", "cart", "nes"),
        "Super Famicom" to Entry("snes", "cart", "snes"),
        "Super Game Boy" to Entry("gb", "cart", "gb"),
        "Game Boy" to Entry("gb", "cart", "gb"),
        "Game Boy Color" to Entry("gbc", "cart", "gb"),
        "Game Boy Advance" to Entry("gba", "cart", "gba"),
        "Nintendo 64" to Entry("n64", "cart", "n64"),
        "Master System" to Entry("mastersystem", "cart", "sms"),
        "SG-1000" to Entry("mastersystem", "cart", "sms"),
        "Mega Drive" to Entry("genesis", "cart", "md"),
        "Mega 32X" to Entry("sega32x", "cart", "md"),
        "Game Gear" to Entry("gamegear", "cart", "small"),
        "Mega CD" to Entry("segacd", "disc", "cd"),
        "Mega CD 32X" to Entry("segacd", "disc", "cd"),
        "PlayStation" to Entry("psx", "disc", "psx"),
        "PlayStation Portable" to Entry("psp", "cart", "umd"),
        "Neo Geo" to Entry("neogeo", "cart", "aes"),
        "Neo Geo Pocket" to Entry("ngp", "cart", "small"),
        "Neo Geo Pocket Color" to Entry("ngpc", "cart", "small"),
        "PC Engine" to Entry("pcengine", "card", "hucard"),
        "PC Engine CD" to Entry("pcenginecd", "disc", "cd"),
        "WonderSwan" to Entry("wonderswan", "cart", "small"),
        "WonderSwan Color" to Entry("wonderswancolor", "cart", "small"),
        "Pocket Challenge V2" to Entry("wonderswancolor", "cart", "small"),
        "Atari 2600" to Entry("atari2600", "cart", "a26"),
        "Arcade" to Entry("arcade", "card", "pcb"),
    )

    /** The 4a-baked shell per Phobos system (file base name under assets/carts), nearest as the scope chose. */
    private val shapes: Map<String, String> = mapOf(
        "Famicom" to "famicom",
        "Super Famicom" to "sfc",
        "Super Game Boy" to "gb",
        "Game Boy" to "gb",
        "Game Boy Color" to "gbc",
        "Game Boy Advance" to "gba",
        "Nintendo 64" to "n64",
        "Master System" to "sms",
        "SG-1000" to "sms",
        "Mega Drive" to "md",
        "Mega 32X" to "md",
        "Game Gear" to "small",
        "Mega CD" to "cd",
        "Mega CD 32X" to "cd",
        "PlayStation" to "psx",
        "PlayStation Portable" to "minidisc",
        "Neo Geo" to "aes",
        "Neo Geo CD" to "cd",
        "Neo Geo Pocket" to "small",
        "Neo Geo Pocket Color" to "small",
        "PC Engine" to "hucard",
        "PC Engine CD" to "cd",
        "SuperGrafx" to "hucard",
        "WonderSwan" to "small",
        "WonderSwan Color" to "small",
        "Pocket Challenge V2" to "small",
        "Atari 2600" to "a26",
        "ColecoVision" to "a26",
        "Arcade" to "pcb",
    )

    /** The shape map for the asset-existence test (the shapes are private otherwise). */
    internal fun allShapeEntries(): Map<String, String> = shapes

    /** The disc shapes among the 4a outputs (everything else is a cart shell). */
    private val discShapes = setOf("cd", "psx", "minidisc")

    /** The Cartridge system entry for [system], or null when Cartridge has no such system (no label scans). */
    fun entry(system: String): Entry? = entries[system]

    /** The 4a shell for [system] under assets/carts, or null (keep console art). */
    fun cartShape(system: String): String? = shapes[system]

    /** Whether [system]'s shell is one of the disc outputs. */
    fun isDisc(system: String): Boolean = shapes[system] in discShapes

    /**
     * The crop shape exactly as Cartridge's scraper.js `labelShape` decides it: 'disc' for disc media, 'gbc' for the
     * Game Boy Color, otherwise the system's media shape when it has a crop spec (PSP's 'umd' included); null when
     * no scan can be cropped for it.
     */
    fun labelShape(system: String): String? {
        val e = entry(system) ?: return null
        if (e.mediaType == "disc") return "disc"
        val k = if (e.id == "gbc") "gbc" else e.mediaShape
        return if (LabelCrop.SPECS.containsKey(k)) k else null
    }
}
