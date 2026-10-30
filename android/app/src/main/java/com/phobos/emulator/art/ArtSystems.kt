// Folder names in part as Cartridge 0.4.9.3's electron/systems.js has them (its thumbs and thumbsAlt; MIT;
// docs/third-party/cartridge.md), each checked against the server's own listing.
package com.phobos.emulator.art

import com.phobos.emulator.data.GameKey
import com.phobos.emulator.util.romTitle

private const val NES = "Nintendo - Nintendo Entertainment System"
private const val FDS = "Nintendo - Family Computer Disk System"
private const val SNES = "Nintendo - Super Nintendo Entertainment System"
private const val SATELLAVIEW = "Nintendo - Satellaview"
private const val SUFAMI_TURBO = "Nintendo - Sufami Turbo"
private const val N64 = "Nintendo - Nintendo 64"
private const val N64DD = "Nintendo - Nintendo 64DD"
private const val GAME_BOY = "Nintendo - Game Boy"
private const val MEGA_CD = "Sega - Mega-CD - Sega CD"
private const val WONDERSWAN_COLOR = "Bandai - WonderSwan Color"

/**
 * Each system's folders on libretro's thumbnail server, as named there (checked against its root listing,
 * 2026-10-27). Systems with no folder of their own look in the nearest one's, as the owner chose (2026-10-28): the
 * Super Game Boy in the Game Boy's, the Mega CD 32X in the Mega CD's, the Pocket Challenge V2 in the WonderSwan
 * Color's. Mega LD and PC Engine LD have none, so keep their console art.
 */
private val folders: Map<String, List<String>> = mapOf(
    "Arcade" to listOf("MAME"),
    "Atari 2600" to listOf("Atari - 2600"),
    "ColecoVision" to listOf("Coleco - ColecoVision"),
    "Famicom" to listOf(NES, FDS),
    "Super Famicom" to listOf(SNES, SATELLAVIEW, SUFAMI_TURBO),
    "Super Game Boy" to listOf(GAME_BOY),
    "Nintendo 64" to listOf(N64, N64DD),
    "Game Boy" to listOf(GAME_BOY),
    "Game Boy Color" to listOf("Nintendo - Game Boy Color"),
    "Game Boy Advance" to listOf("Nintendo - Game Boy Advance"),
    "SG-1000" to listOf("Sega - SG-1000"),
    "Master System" to listOf("Sega - Master System - Mark III"),
    "Mega Drive" to listOf("Sega - Mega Drive - Genesis"),
    "Mega 32X" to listOf("Sega - 32X"),
    "Game Gear" to listOf("Sega - Game Gear"),
    "Mega CD" to listOf(MEGA_CD),
    "Mega CD 32X" to listOf(MEGA_CD),
    "PlayStation" to listOf("Sony - PlayStation"),
    "PlayStation Portable" to listOf("Sony - PlayStation Portable"),
    "Neo Geo" to listOf("SNK - Neo Geo"),
    "Neo Geo CD" to listOf("SNK - Neo Geo CD"),
    "Neo Geo Pocket" to listOf("SNK - Neo Geo Pocket"),
    "Neo Geo Pocket Color" to listOf("SNK - Neo Geo Pocket Color"),
    "ZX Spectrum" to listOf("Sinclair - ZX Spectrum"),
    "ZX Spectrum 128" to listOf("Sinclair - ZX Spectrum"),
    "PC Engine" to listOf("NEC - PC Engine - TurboGrafx 16"),
    "PC Engine CD" to listOf("NEC - PC Engine CD - TurboGrafx-CD"),
    "SuperGrafx" to listOf("NEC - PC Engine SuperGrafx"),
    "WonderSwan" to listOf("Bandai - WonderSwan"),
    "WonderSwan Color" to listOf(WONDERSWAN_COLOR),
    "Pocket Challenge V2" to listOf(WONDERSWAN_COLOR),
    "MSX" to listOf("Microsoft - MSX"),
    "MSX2" to listOf("Microsoft - MSX2"),
)

/** The systems whose games have no folder on the server, and so keep their console art. */
val systemsWithoutArt = setOf("Mega LD", "PC Engine LD")

/** A file kind that has its own folder, put first for such a file: a disk on the Famicom, say. */
private val firstFolderFor: Map<String, String> = mapOf(
    "fds" to FDS, "bs" to SATELLAVIEW, "st" to SUFAMI_TURBO, "ndd" to N64DD, "n64dd" to N64DD, "d64" to N64DD,
)

/** The folders [system]'s game [fileName] is looked for in, in order; none for a system without art. */
fun artFolders(system: String, fileName: String): List<String> {
    val list = folders[system] ?: return emptyList()
    val first = firstFolderFor[fileName.substringAfterLast('.', "").lowercase()]
    return if (first != null && first in list) listOf(first) + (list - first) else list
}

/** Whether [system]'s games can have pictures from the server. */
fun hasArt(system: String): Boolean = folders.containsKey(system)

/**
 * Whether a name that isn't on the server as it is may be matched loosely ([matchArt]'s fuzzy step) on [system].
 * Not on Arcade: checked against the server's MAME listing (2026-10-28), the loose step found only wrong pictures for
 * Phobos's arcade games (Super Real Mahjong P6 for Super Real Mahjong VS).
 */
fun fuzzyArtMatch(system: String): Boolean = system != "Arcade"

/** A game whose pictures are wanted: [key], and the disc's own title (a PSP disc's) when it has one. */
data class ArtGame(val key: GameKey, val discTitle: String? = null)

/**
 * The names [game] is looked for by on the server, in order. Most games go by their file's name without the
 * extension, which No-Intro and Redump sets share with the server, then a PSP disc's own title. An arcade game's file
 * is MAME's short name ("chboxing"), which the server's MAME folder doesn't use: it goes by its title from Phobos's
 * arcade list ([arcadeTitles]), and has none when the list doesn't know it.
 */
fun artLookups(game: ArtGame, arcadeTitles: Map<String, String>): List<String> {
    val base = romTitle(game.key.name)
    if (game.key.system == "Arcade") return listOfNotNull(arcadeTitles[base.lowercase()])
    return listOfNotNull(base, game.discTitle?.takeIf { it.isNotBlank() && it != base })
}

private val bmlField = Regex("^  (name|title):\\s*(.*)$")

/** Each game's title in Phobos's arcade list (`assets/Database/Arcade.bml`), by its short name in lower case. */
fun arcadeTitles(bml: String): Map<String, String> {
    val titles = HashMap<String, String>()
    var name: String? = null
    for (line in bml.lineSequence()) {
        if (line == "game") name = null
        val field = bmlField.find(line) ?: continue
        val (label, value) = field.destructured
        when (label) {
            "name" -> name = value.trim().lowercase()
            "title" -> name?.let { titles[it] = value.trim() }
        }
    }
    return titles
}
