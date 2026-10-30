package com.phobos.emulator.ui.carts

/**
 * Cart look resolution, ported from Cartridge's `src/carts/pipeline.js`: the shell finish and colour lists,
 * the 29 special-edition filename rules (`SPECIAL_CART_LOOKS`, `findSpecialEditionLook`), the precedence of
 * per-game over special-edition over per-system looks (`resolveCartLook`), and the play-time wear levels
 * (`resolveWearLevel`).
 *
 * The fixtures in `android/app/src/test/resources/carts/look-fixtures.json` are generated from the original
 * code by `tools/cart-fixtures/make-fixtures.mjs`; `LookResolverTest` compares this port against them.
 *
 * Behaviour kept exactly: `isCartLook` treats an empty finish or colour as no look; the first matching
 * special rule wins; `specialCarts = false` skips special editions but still honours configured looks.
 */
object LookResolver {

    data class ShellFinish(val id: String, val label: String)

    data class ShellColor(val name: String, val hex: String)

    data class Look(val finish: String, val color: String, val special: Boolean = false)

    /** The game fields the resolver reads (Cartridge: `e.id`, `e.file`). */
    data class GameInput(val id: Long, val file: String)

    val shellFinishes: List<ShellFinish> = listOf(
        ShellFinish("solid", "Solid"),
        ShellFinish("translucent", "Translucent"),
        ShellFinish("clear", "Clear"),
        ShellFinish("metallic", "Metallic"),
        ShellFinish("glitter", "Glitter"),
        ShellFinish("glow", "Glow"),
    )

    val shellColors: List<ShellColor> = listOf(
        ShellColor("Classic Grey", "#a7a3ae"),
        ShellColor("Charcoal", "#3b3c42"),
        ShellColor("Black", "#1d1d21"),
        ShellColor("White", "#ecedf0"),
        ShellColor("Atomic Purple", "#6f4fb0"),
        ShellColor("Grape", "#5a2f8e"),
        ShellColor("Berry", "#c22e6e"),
        ShellColor("Bubblegum", "#f39ac0"),
        ShellColor("Fire Red", "#d8342b"),
        ShellColor("Ruby", "#a8102a"),
        ShellColor("Tangerine", "#f07a22"),
        ShellColor("Dandelion", "#f2c230"),
        ShellColor("Kiwi", "#8cc63f"),
        ShellColor("Jungle Green", "#1f8a4c"),
        ShellColor("Teal", "#18a0a0"),
        ShellColor("Ice Blue", "#8fd3e8"),
        ShellColor("Sapphire", "#1e3fa8"),
        ShellColor("Midnight", "#1f2a52"),
        ShellColor("Gold", "#c9a349"),
        ShellColor("Silver", "#b9bcc2"),
    )

    val defaultShellColors: Map<String, String> = mapOf(
        "solid" to "#6f4fb0",
        "translucent" to "#6f4fb0",
        "clear" to "#e8ecf2",
        "metallic" to "#c9a349",
        "glitter" to "#5a2f8e",
        "glow" to "#b8f0a0",
    )

    private const val POKEMON = "(Pokemon|Pocket Monsters)"

    private class Rule(
        val systems: Set<String>,
        val match: Regex,
        val not: Regex? = null,
        val only: Regex? = null,
        val color: String,
        val finish: String,
    )

    private fun japan() = Regex("\\(Japan\\)")
    private fun usa() = Regex("\\(USA")

    // SPECIAL_CART_LOOKS in the original order: the first match wins.
    private val specialRules: List<Rule> = listOf(
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Red Version|Rote Edition|Version Rouge|Versione Rossa|Edicion Roja)", RegexOption.IGNORE_CASE), not = japan(), color = "#c8242c", finish = "solid"),
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Blue Version|Blaue Edition|Version Bleue|Versione Blu|Edicion Azul)", RegexOption.IGNORE_CASE), not = japan(), color = "#2a4fa8", finish = "solid"),
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Yellow Version|Gelbe Edition|Version Jaune|Versione Gialla|Edicion Amarilla)", RegexOption.IGNORE_CASE), not = japan(), color = "#f2c81e", finish = "solid"),
        Rule(setOf("gb", "gbc"), Regex("^Donkey Kong Land", RegexOption.IGNORE_CASE), not = japan(), color = "#f5c518", finish = "solid"),
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Gold Version|Goldene Edition|Version Or|Versione Oro|Edicion Oro)", RegexOption.IGNORE_CASE), not = japan(), color = "#c9a349", finish = "metallic"),
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Silver Version|Silberne Edition|Version Argent|Versione Argento|Edicion Plata)", RegexOption.IGNORE_CASE), not = japan(), color = "#b9bcc2", finish = "metallic"),
        Rule(setOf("gb", "gbc"), Regex("^$POKEMON - (Crystal Version|Kristall-Edition|Version Cristal|Versione Cristallo|Edicion Cristal)", RegexOption.IGNORE_CASE), color = "#8fd3e8", finish = "translucent"),
        Rule(setOf("gb", "gbc"), Regex("^(Kirby Tilt 'n' Tumble|Koro Koro Kirby)", RegexOption.IGNORE_CASE), color = "#f39ac0", finish = "translucent"),
        Rule(setOf("gb", "gbc"), Regex("^Pokemon Pinball", RegexOption.IGNORE_CASE), not = japan(), color = "#1e1e1e", finish = "solid"),
        Rule(setOf("gba"), Regex("^$POKEMON - (Ruby|Rubin|Version Rubis|Versione Rubino|Edicion Rubi)", RegexOption.IGNORE_CASE), color = "#a8102a", finish = "translucent"),
        Rule(setOf("gba"), Regex("^$POKEMON - (Sapphire|Saphir|Version Saphir|Versione Zaffiro|Edicion Zafiro)", RegexOption.IGNORE_CASE), color = "#1e3fa8", finish = "translucent"),
        Rule(setOf("gba"), Regex("^$POKEMON - (Emerald|Smaragd|Version Emeraude|Versione Smeraldo|Edicion Esmeralda)", RegexOption.IGNORE_CASE), color = "#1a8a4a", finish = "translucent"),
        Rule(setOf("gba"), Regex("^$POKEMON - (Fire ?Red|Feuerrote|Version Rouge Feu|Versione Rosso Fuoco|Edicion Rojo Fuego)", RegexOption.IGNORE_CASE), color = "#e0452a", finish = "translucent"),
        Rule(setOf("gba"), Regex("^$POKEMON - (Leaf ?Green|Blattgrune|Version Vert Feuille|Versione Verde Foglia|Edicion Verde Hoja)", RegexOption.IGNORE_CASE), color = "#5cae3c", finish = "translucent"),
        Rule(setOf("gba"), Regex("^(Boktai|Bokura no Taiyou|Zoku Bokura no Taiyou)", RegexOption.IGNORE_CASE), color = "#e8eef0", finish = "clear"),
        Rule(setOf("gba"), Regex("^(Yoshi Topsy-Turvy|Yoshi's Universal Gravitation)", RegexOption.IGNORE_CASE), not = japan(), color = "#4caf50", finish = "solid"),
        Rule(setOf("nes"), Regex("^(Legend of Zelda, The|Zelda II - The Adventure of Link)", RegexOption.IGNORE_CASE), not = japan(), color = "#c9a44a", finish = "metallic"),
        Rule(setOf("snes"), Regex("^Killer Instinct", RegexOption.IGNORE_CASE), only = usa(), color = "#1c1c1c", finish = "solid"),
        Rule(setOf("snes"), Regex("^(Doom|Spider-Man (&|and) Venom - Maximum Carnage)\\b", RegexOption.IGNORE_CASE), only = usa(), color = "#b3262a", finish = "solid"),
        Rule(setOf("genesis"), Regex("^Spider-Man (&|and) Venom - Maximum Carnage", RegexOption.IGNORE_CASE), only = usa(), color = "#b3262a", finish = "solid"),
        Rule(setOf("n64"), Regex("^Legend of Zelda, The - Ocarina of Time \\(USA\\)(?!.*Rev)", RegexOption.IGNORE_CASE), color = "#c9a44a", finish = "metallic"),
        Rule(setOf("n64"), Regex("^Legend of Zelda, The - Majora's Mask", RegexOption.IGNORE_CASE), only = Regex("\\((USA|Europe)"), color = "#c9a44a", finish = "metallic"),
        Rule(setOf("n64"), Regex("^Pokemon Stadium 2", RegexOption.IGNORE_CASE), only = Regex("\\((USA|Europe)"), color = "#c9a44a", finish = "metallic"),
        Rule(setOf("n64"), Regex("^(Donkey Kong 64|Earthworm Jim 3D|Tony Hawk's Pro Skater 2)", RegexOption.IGNORE_CASE), only = usa(), color = "#f2c522", finish = "solid"),
        Rule(setOf("n64"), Regex("^(All-Star Baseball 2001|Jeremy McGrath Supercross 2000|NBA Jam 2000|NFL Quarterback Club 2001|Rocket - Robot on Wheels|Spider-Man)\\b", RegexOption.IGNORE_CASE), only = usa(), color = "#b3262a", finish = "solid"),
        Rule(setOf("n64"), Regex("^(007 - The World Is Not Enough|Bass Masters 2000|Hydro Thunder|Madden NFL 2001|Tony Hawk's Pro Skater)\\b(?! 2| 3)", RegexOption.IGNORE_CASE), only = usa(), color = "#2c56a8", finish = "solid"),
        Rule(setOf("n64"), Regex("^(Army Men - Air Combat|Army Men - Sarge's Heroes 2|BattleTanx - Global Assault|Rayman 2)", RegexOption.IGNORE_CASE), only = usa(), color = "#3f7a3a", finish = "solid"),
        Rule(setOf("n64"), Regex("^(Aidyn Chronicles|Armorines|Batman Beyond|Battlezone|ECW Hardcore Revolution|Fighter Destiny 2|Madden NFL 2002|Nuclear Strike 64|Power Rangers - Lightspeed Rescue|Rally Challenge 2000|Road Rash 64|Rugrats in Paris|Scooby-Doo! - Classic Creep Capers|Tom Clancy's Rainbow Six|Tony Hawk's Pro Skater 3|Turok 2|Turok 3|Turok - Rage Wars|WWF No Mercy|WWF WrestleMania 2000)", RegexOption.IGNORE_CASE), only = usa(), color = "#1c1c1c", finish = "solid"),
        Rule(setOf("nds"), Regex("^$POKEMON - (HeartGold|SoulSilver|Black|White|Black Version 2|White Version 2|Black 2|White 2)", RegexOption.IGNORE_CASE), color = "#1a1418", finish = "solid"),
    )

    /** Cartridge's `findSpecialEditionLook`: first matching rule for the system, with its `not`/`only` checks. */
    fun findSpecialEdition(file: String, system: String): Look? {
        // The rules are keyed by Cartridge's system ids; Phobos's UI passes its display names ("Game Boy",
        // "Mega Drive"). Map here, in the one place every caller reaches, so no UI site can miss it.
        val cartridgeSystem = CartShapes.entry(system)?.id ?: system
        for (rule in specialRules) {
            if (cartridgeSystem !in rule.systems) continue
            if (!rule.match.containsMatchIn(file)) continue
            if (rule.not != null && rule.not.containsMatchIn(file)) continue
            if (rule.only != null && !rule.only.containsMatchIn(file)) continue
            return Look(rule.finish, rule.color, special = true)
        }
        return null
    }

    /** Cartridge's `isCartLook`: a look needs both fields (an empty string is no look). */
    private fun isLook(look: Look?): Boolean = look != null && look.finish.isNotEmpty() && look.color.isNotEmpty()

    /**
     * Cartridge's `resolveCartLook`: the per-game key `g:<id>` wins, then a special edition when enabled, then the
     * per-system key `s:<system>`; null when none applies.
     */
    fun resolve(
        game: GameInput?,
        system: String,
        cartLooks: Map<String, Look> = emptyMap(),
        specialCarts: Boolean = true,
    ): Look? {
        if (game != null) {
            val byGame = cartLooks["g:${game.id}"]
            if (isLook(byGame)) return byGame
        }
        if (game != null && specialCarts) {
            findSpecialEdition(game.file, system)?.let { return it }
        }
        val bySystem = cartLooks["s:$system"]
        return if (isLook(bySystem)) bySystem else null
    }

    /**
     * Cartridge's `resolveWearLevel`: 0 (off, or under an hour), 1 under 8 h, 2 under 30 h, 3 beyond; the
     * `loved` mode always shows the heaviest wear.
     */
    fun wearLevel(playtimeSeconds: Long, wearMode: String = "off"): Int {
        if (wearMode == "off") return 0
        if (wearMode == "loved") return 3
        val hours = playtimeSeconds.toDouble() / 3600.0
        return when {
            hours < 1 -> 0
            hours < 8 -> 1
            hours < 30 -> 2
            else -> 3
        }
    }
}
