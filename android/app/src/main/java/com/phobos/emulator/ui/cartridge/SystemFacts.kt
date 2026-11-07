package com.phobos.emulator.ui.cartridge

import androidx.compose.ui.graphics.Color
import com.phobos.emulator.ui.carts.CartShapes
import com.phobos.emulator.util.displayTitle
import com.phobos.emulator.util.romTitle

/**
 * What the Cartridge-style room says about a system: its short mark, who made it, when it came out, whether it
 * was a handheld, and the colour its plate, label and glow take. The data is Phobos's own; it is a table so the
 * next system is one row, and a test can prove every Library system is covered.
 */
data class SystemFacts(
    val system: String,
    val short: String,
    val maker: String,
    val year: Int,
    val handheld: Boolean,
    val color: Color,
) {
    /** "Nintendo · 1990 · Handheld", the line above the selected system's name. */
    val eyebrow: String get() = buildString {
        append(maker)
        append(" · ")
        append(year)
        if (handheld) append(" · Handheld")
    }

    /** The 4a-baked shell this system's games and media plate use, if any. */
    val mediaShape: String? get() = CartShapes.cartShape(system)
    val isDisc: Boolean get() = CartShapes.isDisc(system)
}

private val facts: Map<String, SystemFacts> = listOf(
    SystemFacts("Famicom", "FC", "Nintendo", 1983, false, Color(0xFFE0443E)),
    SystemFacts("Super Famicom", "SFC", "Nintendo", 1990, false, Color(0xFF8B7CD8)),
    SystemFacts("Super Game Boy", "SGB", "Nintendo", 1994, false, Color(0xFF6C5CB8)),
    SystemFacts("Nintendo 64", "N64", "Nintendo", 1996, false, Color(0xFF3E9E5B)),
    SystemFacts("Game Boy", "GB", "Nintendo", 1989, true, Color(0xFF8A8F98)),
    SystemFacts("Game Boy Color", "GBC", "Nintendo", 1998, true, Color(0xFF6A5ACD)),
    SystemFacts("Game Boy Advance", "GBA", "Nintendo", 2001, true, Color(0xFF5B4B9E)),
    SystemFacts("Master System", "SMS", "Sega", 1985, false, Color(0xFF2F6FD0)),
    SystemFacts("SG-1000", "SG-1000", "Sega", 1983, false, Color(0xFF3F7FC0)),
    SystemFacts("Mega Drive", "MD", "Sega", 1988, false, Color(0xFF2F6FD0)),
    SystemFacts("Mega 32X", "32X", "Sega", 1994, false, Color(0xFF8A5FD0)),
    SystemFacts("Game Gear", "GG", "Sega", 1990, true, Color(0xFF4F8FE0)),
    SystemFacts("Mega CD", "MCD", "Sega", 1991, false, Color(0xFF2E5C9A)),
    SystemFacts("Mega CD 32X", "MCD 32X", "Sega", 1994, false, Color(0xFF6A4FB0)),
    SystemFacts("PlayStation", "PS", "Sony", 1994, false, Color(0xFF8A8F98)),
    SystemFacts("PlayStation Portable", "PSP", "Sony", 2004, true, Color(0xFF4A6FA5)),
    SystemFacts("Neo Geo", "AES", "SNK", 1990, false, Color(0xFFC62828)),
    SystemFacts("Neo Geo CD", "NGCD", "SNK", 1994, false, Color(0xFF8E1B1B)),
    SystemFacts("Neo Geo Pocket", "NGP", "SNK", 1998, true, Color(0xFFB08D57)),
    SystemFacts("Neo Geo Pocket Color", "NGPC", "SNK", 1999, true, Color(0xFFC9A227)),
    SystemFacts("PC Engine", "PCE", "NEC", 1987, false, Color(0xFFE8A33D)),
    SystemFacts("PC Engine CD", "PCE-CD", "NEC", 1988, false, Color(0xFFC77B2E)),
    SystemFacts("SuperGrafx", "SGX", "NEC", 1989, false, Color(0xFF7A5AA8)),
    SystemFacts("WonderSwan", "WS", "Bandai", 1999, true, Color(0xFF3A7BD5)),
    SystemFacts("WonderSwan Color", "WSC", "Bandai", 2000, true, Color(0xFF6A5ACD)),
    SystemFacts("Pocket Challenge V2", "PCV2", "Benesse", 2000, true, Color(0xFF2E8B57)),
    SystemFacts("Atari 2600", "2600", "Atari", 1977, false, Color(0xFFC9A227)),
    SystemFacts("ColecoVision", "CV", "Coleco", 1982, false, Color(0xFF6B4F9E)),
    SystemFacts("MSX", "MSX", "ASCII · Microsoft", 1983, false, Color(0xFF2E5C9A)),
    SystemFacts("MSX2", "MSX2", "ASCII · Microsoft", 1985, false, Color(0xFF4A6FA5)),
    SystemFacts("ZX Spectrum", "ZX", "Sinclair", 1982, false, Color(0xFFD6453D)),
    SystemFacts("ZX Spectrum 128", "ZX128", "Sinclair", 1985, false, Color(0xFFE05A4F)),
    SystemFacts("Mega LD", "MLD", "Pioneer · Sega", 1993, false, Color(0xFF5B4B9E)),
    SystemFacts("PC Engine LD", "PCE-LD", "Pioneer · NEC", 1993, false, Color(0xFF8A5FD0)),
    SystemFacts("Arcade", "ARC", "Various", 1971, false, Color(0xFF7B6CF0)),
).associateBy { it.system }

/** The facts for [system]; a system the table has missed still gets a usable card rather than an exception. */
fun factsFor(system: String): SystemFacts = facts[system] ?: SystemFacts(
    system = system,
    short = system.take(4).uppercase(),
    maker = "Phobos",
    year = 0,
    handheld = false,
    color = Color(0xFF7B6CF0),
)

/** Every row, for the registry test. */
internal fun allSystemFacts(): List<SystemFacts> = facts.values.toList()


/**
 * A file name as the room prints it: no extension, no region/version/revision tags and no disc number. The tags
 * stay in the record and the saves; only the page and the cart's label show the clean title.
 */
fun cartTitle(fileName: String, discTitle: String? = null): String {
    val base = displayTitle(romTitle(fileName), discTitle)
    val cleaned = base
        .replace(Regex("""\s*[\[(][^\])]*[\])]"""), "")
        .replace(Regex("""\s{2,}"""), " ")
        .trim()
    return cleaned.ifEmpty { base }
}
