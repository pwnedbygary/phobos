package com.phobos.emulator.ui.theme

enum class ThemeGroup(val title: String) {
    PHOBOS("Phobos"),
    DARK("Dark"),
    LIGHT("Light"),
    RETROWAVE("Retrowave"),
}

/** A selectable theme: one palette, dark or light. */
class AppTheme(
    val id: String,
    val name: String,
    val group: ThemeGroup,
    val palette: Palette,
    val tagline: String? = null,
) {
    val isDark: Boolean get() = palette.isDark

    val colors: ThemeColors by lazy { SchemeBuilder.build(palette) }
}

object ThemeRegistry {
    const val DEFAULT_ID = "phobos"

    val all: List<AppTheme> = listOf(
        AppTheme(DEFAULT_ID, "Phobos", ThemeGroup.PHOBOS, Palettes.Phobos, tagline = "Mars orange and violet on deep space"),

        dark("dracula", "Dracula", Palettes.Dracula),
        dark("monokai", "Monokai", Palettes.Monokai),
        dark("gruvbox_dark", "Gruvbox Dark", Palettes.GruvboxDark),
        dark("ayu_dark", "Ayu Dark", Palettes.AyuDark),
        dark("night_owl", "Night Owl", Palettes.NightOwl),
        dark("rose_pine", "Rosé Pine", Palettes.RosePine),
        dark("everforest_dark", "Everforest Dark", Palettes.EverforestDark),
        dark("kanagawa", "Kanagawa", Palettes.Kanagawa),

        AppTheme("rose_pine_dawn", "Rosé Pine Dawn", ThemeGroup.LIGHT, Palettes.RosePineDawn),

        AppTheme("synthwave_84", "Synthwave '84", ThemeGroup.RETROWAVE, Palettes.Synthwave84),
        AppTheme("laserwave", "LaserWave", ThemeGroup.RETROWAVE, Palettes.LaserWave),
        AppTheme("shades_of_purple", "Shades of Purple", ThemeGroup.RETROWAVE, Palettes.ShadesOfPurple),
    )

    private val byId = all.associateBy { it.id }

    /** The theme with [id]; unknown ids (System and the other themes since removed among them) are Phobos. */
    fun find(id: String): AppTheme = byId[id] ?: byId.getValue(DEFAULT_ID)

    private fun dark(id: String, name: String, palette: Palette) = AppTheme(id, name, ThemeGroup.DARK, palette)
}
