package com.phobos.emulator.ui.theme

/**
 * A colorway's published colors (ARGB). [SchemeBuilder] maps them onto Material 3 roles, derives
 * the in-between surface tones and lifts any color too faint to read as text, so the values here
 * stay the colorway's own.
 */
data class Palette(
    val isDark: Boolean,
    /** Editor / window background. */
    val background: Long,
    /** Main text. */
    val foreground: Long,
    val primary: Long,
    val secondary: Long,
    val tertiary: Long,
    val error: Long,
    val success: Long,
    val warning: Long,
    /** Panel / popup tone one step up from [background]; derived when null. */
    val raised: Long? = null,
    /** Sidebar / gutter tone on the far side of [background] from [raised]; derived when null. */
    val backgroundAlt: Long? = null,
    /** Secondary text; derived from [foreground] when null. */
    val subtle: Long? = null,
    /** Comment / border tone; derived when null. */
    val muted: Long? = null,
    val primaryContainer: Long? = null,
    val secondaryContainer: Long? = null,
    val tertiaryContainer: Long? = null,
)

/**
 * Every built-in palette. Values come from each colorway's official palette or its VS Code theme
 * (editor background/foreground, panel and sidebar tones, comment color and syntax accents), and
 * Phobos's from the project's website.
 */
object Palettes {
    // The website's background, text, muted text (subtle), three accents (Mars orange leading) and line color (muted).
    val Phobos = Palette(
        isDark = true, background = DeepSpace, foreground = 0xFFEFEAFF,
        primary = MarsOrange, secondary = Violet, tertiary = Gold,
        error = 0xFFFF6B6B, success = 0xFF7BD88F, warning = 0xFFFFB454,
        raised = 0xFF1C1633, backgroundAlt = 0xFF030208, subtle = 0xFFB4A9D8, muted = 0xFF373147,
    )

    val Dracula = Palette(
        isDark = true, background = 0xFF282A36, foreground = 0xFFF8F8F2,
        primary = 0xFFBD93F9, secondary = 0xFFFF79C6, tertiary = 0xFF8BE9FD,
        error = 0xFFFF5555, success = 0xFF50FA7B, warning = 0xFFF1FA8C,
        raised = 0xFF44475A, backgroundAlt = 0xFF21222C, muted = 0xFF6272A4,
    )

    val GruvboxDark = Palette(
        isDark = true, background = 0xFF282828, foreground = 0xFFEBDBB2,
        primary = 0xFFFE8019, secondary = 0xFF8EC07C, tertiary = 0xFFFABD2F,
        error = 0xFFFB4934, success = 0xFFB8BB26, warning = 0xFFFABD2F,
        raised = 0xFF3C3836, backgroundAlt = 0xFF1D2021, subtle = 0xFFD5C4A1, muted = 0xFF928374,
    )

    val Monokai = Palette(
        isDark = true, background = 0xFF272822, foreground = 0xFFF8F8F2,
        primary = 0xFFA6E22E, secondary = 0xFF66D9EF, tertiary = 0xFFAE81FF,
        error = 0xFFF92672, success = 0xFFA6E22E, warning = 0xFFE6DB74,
        raised = 0xFF3E3D32, backgroundAlt = 0xFF1E1F1C, muted = 0xFF75715E,
    )

    val AyuDark = Palette(
        isDark = true, background = 0xFF10141C, foreground = 0xFFBFBDB6,
        primary = 0xFFE6B450, secondary = 0xFF59C2FF, tertiary = 0xFFD2A6FF,
        error = 0xFFD95757, success = 0xFFAAD94C, warning = 0xFFFF8F40,
        raised = 0xFF141821, backgroundAlt = 0xFF0D1017, muted = 0xFF5A6378,
    )

    val NightOwl = Palette(
        isDark = true, background = 0xFF011627, foreground = 0xFFD6DEEB,
        primary = 0xFFC792EA, secondary = 0xFF82AAFF, tertiary = 0xFF7FDBCA,
        error = 0xFFEF5350, success = 0xFFC5E478, warning = 0xFFECC48D,
        raised = 0xFF1D3B53, backgroundAlt = 0xFF01111D, muted = 0xFF5F7E97,
    )

    val RosePine = Palette(
        isDark = true, background = 0xFF191724, foreground = 0xFFE0DEF4,
        primary = 0xFFC4A7E7, secondary = 0xFFEBBCBA, tertiary = 0xFF9CCFD8,
        error = 0xFFEB6F92, success = 0xFF9CCFD8, warning = 0xFFF6C177,
        raised = 0xFF26233A, subtle = 0xFF908CAA, muted = 0xFF6E6A86,
    )
    val RosePineDawn = Palette(
        isDark = false, background = 0xFFFAF4ED, foreground = 0xFF575279,
        primary = 0xFF907AA9, secondary = 0xFFD7827E, tertiary = 0xFF56949F,
        error = 0xFFB4637A, success = 0xFF286983, warning = 0xFFEA9D34,
        raised = 0xFFF2E9E1, backgroundAlt = 0xFFFFFAF3, subtle = 0xFF797593, muted = 0xFF9893A5,
    )

    // Everforest, medium contrast
    val EverforestDark = Palette(
        isDark = true, background = 0xFF2D353B, foreground = 0xFFD3C6AA,
        primary = 0xFFA7C080, secondary = 0xFF83C092, tertiary = 0xFFE69875,
        error = 0xFFE67E80, success = 0xFFA7C080, warning = 0xFFDBBC7F,
        raised = 0xFF3D484D, backgroundAlt = 0xFF232A2E, subtle = 0xFF9DA9A0, muted = 0xFF859289,
    )
    // Kanagawa Wave
    val Kanagawa = Palette(
        isDark = true, background = 0xFF1F1F28, foreground = 0xFFDCD7BA,
        primary = 0xFF7E9CD8, secondary = 0xFF957FB8, tertiary = 0xFFE6C384,
        error = 0xFFFF5D62, success = 0xFF98BB6C, warning = 0xFFFF9E3B,
        raised = 0xFF363646, backgroundAlt = 0xFF16161D, subtle = 0xFFC8C093, muted = 0xFF727169,
    )

    // Retrowave
    // Led by its sunset coral; its neon pink leads LaserWave.
    val Synthwave84 = Palette(
        isDark = true, background = 0xFF262335, foreground = 0xFFFFFFFF,
        primary = 0xFFF97E72, secondary = 0xFF36F9F6, tertiary = 0xFFFEDE5D,
        error = 0xFFFE4450, success = 0xFF72F1B8, warning = 0xFFFF8B39,
        raised = 0xFF463465, backgroundAlt = 0xFF171520, muted = 0xFF848BBD,
    )
    // Synthwave '84's colors, its neon pink leading, in place of LaserWave's own.
    val LaserWave = Synthwave84.copy(primary = 0xFFFF7EDB)
    val ShadesOfPurple = Palette(
        isDark = true, background = 0xFF2D2B55, foreground = 0xFFFFFFFF,
        primary = 0xFFFAD000, secondary = 0xFF9EFFFF, tertiary = 0xFFFB94FF,
        error = 0xFFEC3A37, success = 0xFFA5FF90, warning = 0xFFFF9D00,
        backgroundAlt = 0xFF1E1E3F, subtle = 0xFFA599E9,
        // Its buttons are solid yellow; a yellow tint over purple would turn brown.
        primaryContainer = 0xFFFAD000,
    )
}
