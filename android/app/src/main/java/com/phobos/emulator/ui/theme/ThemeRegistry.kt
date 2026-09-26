package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.ThemeMode

enum class ThemeGroup(val title: String) {
    SYSTEM("System & Phobos"),
    DARK("Dark"),
    LIGHT("Light"),
    RETROWAVE("Retrowave"),
}

/**
 * A selectable theme. Adaptive themes (System, Phobos) carry a dark and a light palette and follow
 * the Light / Dark / Auto mode. The others have one palette and may name a [sibling] of the opposite
 * brightness, which "Auto" switches to when the system appearance doesn't match.
 */
class AppTheme(
    val id: String,
    val name: String,
    val group: ThemeGroup,
    val dark: Palette?,
    val light: Palette?,
    val sibling: String? = null,
    /** Uses Material You wallpaper colors on Android 12+, and the palettes elsewhere. */
    val dynamic: Boolean = false,
    val tagline: String? = null,
) {
    val adaptive: Boolean get() = dark != null && light != null

    /** Brightness of a single-palette theme. */
    val isDark: Boolean get() = dark != null

    private val darkColors by lazy { dark?.let(SchemeBuilder::build) }
    private val lightColors by lazy { light?.let(SchemeBuilder::build) }

    fun colors(dark: Boolean): ThemeColors = checkNotNull(if (dark) darkColors ?: lightColors else lightColors ?: darkColors)
}

/** The theme and brightness actually shown for a selection. */
data class ResolvedTheme(val theme: AppTheme, val isDark: Boolean) {
    val colors: ThemeColors get() = theme.colors(isDark)
}

object ThemeRegistry {
    const val SYSTEM_ID = "system"

    val all: List<AppTheme> = listOf(
        AppTheme(
            SYSTEM_ID, "System", ThemeGroup.SYSTEM, Palettes.PhobosDark, Palettes.PhobosLight,
            dynamic = true, tagline = "Material You wallpaper colors",
        ),
        AppTheme("phobos", "Phobos", ThemeGroup.SYSTEM, Palettes.PhobosDark, Palettes.PhobosLight, tagline = "Gold on deep space"),

        dark("one_dark", "One Dark", Palettes.OneDark, sibling = "one_light"),
        dark("dracula", "Dracula", Palettes.Dracula),
        dark("nord", "Nord", Palettes.Nord),
        dark("tokyo_night", "Tokyo Night", Palettes.TokyoNight, sibling = "tokyo_night_day"),
        dark("tokyo_night_storm", "Tokyo Night Storm", Palettes.TokyoNightStorm, sibling = "tokyo_night_day"),
        dark("catppuccin_mocha", "Catppuccin Mocha", Palettes.CatppuccinMocha, sibling = "catppuccin_latte"),
        dark("catppuccin_macchiato", "Catppuccin Macchiato", Palettes.CatppuccinMacchiato, sibling = "catppuccin_latte"),
        dark("catppuccin_frappe", "Catppuccin Frappé", Palettes.CatppuccinFrappe, sibling = "catppuccin_latte"),
        dark("github_dark", "GitHub Dark", Palettes.GitHubDark, sibling = "github_light"),
        dark("vscode_dark", "VS Code Dark", Palettes.VsCodeDark, sibling = "vscode_light"),
        dark("monokai", "Monokai", Palettes.Monokai),
        dark("gruvbox_dark", "Gruvbox Dark", Palettes.GruvboxDark, sibling = "gruvbox_light"),
        dark("solarized_dark", "Solarized Dark", Palettes.SolarizedDark, sibling = "solarized_light"),
        dark("ayu_dark", "Ayu Dark", Palettes.AyuDark, sibling = "ayu_light"),
        dark("ayu_mirage", "Ayu Mirage", Palettes.AyuMirage, sibling = "ayu_light"),
        dark("night_owl", "Night Owl", Palettes.NightOwl),
        dark("rose_pine", "Rosé Pine", Palettes.RosePine, sibling = "rose_pine_dawn"),
        dark("rose_pine_moon", "Rosé Pine Moon", Palettes.RosePineMoon, sibling = "rose_pine_dawn"),
        dark("everforest_dark", "Everforest Dark", Palettes.EverforestDark, sibling = "everforest_light"),
        dark("kanagawa", "Kanagawa", Palettes.Kanagawa, sibling = "kanagawa_lotus"),
        dark("material_palenight", "Material Palenight", Palettes.MaterialPalenight),

        light("one_light", "One Light", Palettes.OneLight, sibling = "one_dark"),
        light("github_light", "GitHub Light", Palettes.GitHubLight, sibling = "github_dark"),
        light("vscode_light", "VS Code Light", Palettes.VsCodeLight, sibling = "vscode_dark"),
        light("catppuccin_latte", "Catppuccin Latte", Palettes.CatppuccinLatte, sibling = "catppuccin_mocha"),
        light("tokyo_night_day", "Tokyo Night Day", Palettes.TokyoNightDay, sibling = "tokyo_night"),
        light("solarized_light", "Solarized Light", Palettes.SolarizedLight, sibling = "solarized_dark"),
        light("gruvbox_light", "Gruvbox Light", Palettes.GruvboxLight, sibling = "gruvbox_dark"),
        light("ayu_light", "Ayu Light", Palettes.AyuLight, sibling = "ayu_dark"),
        light("rose_pine_dawn", "Rosé Pine Dawn", Palettes.RosePineDawn, sibling = "rose_pine"),
        light("everforest_light", "Everforest Light", Palettes.EverforestLight, sibling = "everforest_dark"),
        light("kanagawa_lotus", "Kanagawa Lotus", Palettes.KanagawaLotus, sibling = "kanagawa"),

        dark("synthwave_84", "Synthwave '84", Palettes.Synthwave84, group = ThemeGroup.RETROWAVE),
        dark("laserwave", "LaserWave", Palettes.LaserWave, group = ThemeGroup.RETROWAVE),
        dark("shades_of_purple", "Shades of Purple", Palettes.ShadesOfPurple, group = ThemeGroup.RETROWAVE),
    )

    private val byId = all.associateBy { it.id }

    /** The theme with [id]; unknown ids (e.g. a theme removed in an update) fall back to System. */
    fun find(id: String): AppTheme = byId[id] ?: byId.getValue(SYSTEM_ID)

    fun sibling(theme: AppTheme): AppTheme? = theme.sibling?.let(byId::get)

    fun resolve(themeId: String, mode: ThemeMode, followSystem: Boolean, systemDark: Boolean): ResolvedTheme {
        val theme = find(themeId)
        if (theme.adaptive) {
            val dark = when (mode) {
                ThemeMode.LIGHT -> false
                ThemeMode.DARK -> true
                ThemeMode.AUTO -> systemDark
            }
            return ResolvedTheme(theme, dark)
        }
        val shown = sibling(theme)?.takeIf { followSystem && theme.isDark != systemDark } ?: theme
        return ResolvedTheme(shown, shown.isDark)
    }

    private fun dark(id: String, name: String, palette: Palette, sibling: String? = null, group: ThemeGroup = ThemeGroup.DARK) =
        AppTheme(id, name, group, dark = palette, light = null, sibling = sibling)

    private fun light(id: String, name: String, palette: Palette, sibling: String) =
        AppTheme(id, name, ThemeGroup.LIGHT, dark = null, light = palette, sibling = sibling)
}
