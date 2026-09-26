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
 * (editor background/foreground, panel and sidebar tones, comment color and syntax accents).
 */
object Palettes {
    val PhobosDark = Palette(
        isDark = true, background = DeepSpace, foreground = 0xFFF1F3F8,
        primary = GoldMoon, secondary = 0xFFB9A2E8, tertiary = 0xFFE3905B,
        error = 0xFFFF6B6B, success = 0xFF7BD88F, warning = 0xFFFFB454,
        raised = 0xFF1A202B, backgroundAlt = 0xFF05070B, subtle = 0xFFAEB5C4, muted = 0xFF5C6577,
        // The brand's dark purple, lifted so the selection pill still reads on deep space.
        secondaryContainer = 0xFF2B2144, tertiaryContainer = MartianSurface,
    )
    val PhobosLight = Palette(
        isDark = false, background = 0xFFFFFFFF, foreground = SoftBlack,
        primary = 0xFF8A6A00, secondary = 0xFF6B4FA8, tertiary = 0xFF97522A,
        error = 0xFFBA1A1A, success = 0xFF1E7B34, warning = 0xFF8A5A00,
        raised = 0xFFEEEFF3, subtle = 0xFF4B505C, muted = 0xFF7D828D,
        primaryContainer = GoldMoon, secondaryContainer = SkyPurple, tertiaryContainer = 0xFFF6E1D3,
    )

    // Atom One
    val OneDark = Palette(
        isDark = true, background = 0xFF282C34, foreground = 0xFFABB2BF,
        primary = 0xFF61AFEF, secondary = 0xFFC678DD, tertiary = 0xFF56B6C2,
        error = 0xFFE06C75, success = 0xFF98C379, warning = 0xFFE5C07B,
        raised = 0xFF3E4451, backgroundAlt = 0xFF21252B, subtle = 0xFF828997, muted = 0xFF5C6370,
    )
    val OneLight = Palette(
        isDark = false, background = 0xFFFAFAFA, foreground = 0xFF383A42,
        primary = 0xFF4078F2, secondary = 0xFFA626A4, tertiary = 0xFF0184BC,
        error = 0xFFE45649, success = 0xFF50A14F, warning = 0xFFC18401,
        raised = 0xFFEAEAEB, subtle = 0xFF696C77, muted = 0xFFA0A1A7,
    )

    val Dracula = Palette(
        isDark = true, background = 0xFF282A36, foreground = 0xFFF8F8F2,
        primary = 0xFFBD93F9, secondary = 0xFFFF79C6, tertiary = 0xFF8BE9FD,
        error = 0xFFFF5555, success = 0xFF50FA7B, warning = 0xFFF1FA8C,
        raised = 0xFF44475A, backgroundAlt = 0xFF21222C, muted = 0xFF6272A4,
    )

    val Nord = Palette(
        isDark = true, background = 0xFF2E3440, foreground = 0xFFECEFF4,
        primary = 0xFF88C0D0, secondary = 0xFF81A1C1, tertiary = 0xFFB48EAD,
        error = 0xFFBF616A, success = 0xFFA3BE8C, warning = 0xFFEBCB8B,
        raised = 0xFF434C5E, subtle = 0xFFD8DEE9, muted = 0xFF616E88,
    )

    val SolarizedDark = Palette(
        isDark = true, background = 0xFF002B36, foreground = 0xFF93A1A1,
        primary = 0xFF268BD2, secondary = 0xFF2AA198, tertiary = 0xFFB58900,
        error = 0xFFDC322F, success = 0xFF859900, warning = 0xFFCB4B16,
        raised = 0xFF073642, subtle = 0xFF839496, muted = 0xFF586E75,
    )
    val SolarizedLight = Palette(
        isDark = false, background = 0xFFFDF6E3, foreground = 0xFF586E75,
        primary = 0xFF268BD2, secondary = 0xFF2AA198, tertiary = 0xFFB58900,
        error = 0xFFDC322F, success = 0xFF859900, warning = 0xFFCB4B16,
        raised = 0xFFEEE8D5, subtle = 0xFF657B83, muted = 0xFF93A1A1,
    )

    // GitHub Primer
    val GitHubDark = Palette(
        isDark = true, background = 0xFF0D1117, foreground = 0xFFE6EDF3,
        primary = 0xFF58A6FF, secondary = 0xFFD2A8FF, tertiary = 0xFFFFA657,
        error = 0xFFF85149, success = 0xFF3FB950, warning = 0xFFD29922,
        raised = 0xFF21262D, backgroundAlt = 0xFF010409, subtle = 0xFF7D8590, muted = 0xFF6E7681,
    )
    val GitHubLight = Palette(
        isDark = false, background = 0xFFFFFFFF, foreground = 0xFF1F2328,
        primary = 0xFF0969DA, secondary = 0xFF8250DF, tertiary = 0xFF953800,
        error = 0xFFCF222E, success = 0xFF1A7F37, warning = 0xFF9A6700,
        raised = 0xFFEAEEF2, subtle = 0xFF656D76, muted = 0xFF6E7781,
    )

    val GruvboxDark = Palette(
        isDark = true, background = 0xFF282828, foreground = 0xFFEBDBB2,
        primary = 0xFFFE8019, secondary = 0xFF8EC07C, tertiary = 0xFFFABD2F,
        error = 0xFFFB4934, success = 0xFFB8BB26, warning = 0xFFFABD2F,
        raised = 0xFF3C3836, backgroundAlt = 0xFF1D2021, subtle = 0xFFD5C4A1, muted = 0xFF928374,
    )
    val GruvboxLight = Palette(
        isDark = false, background = 0xFFFBF1C7, foreground = 0xFF3C3836,
        primary = 0xFFAF3A03, secondary = 0xFF427B58, tertiary = 0xFFB57614,
        error = 0xFF9D0006, success = 0xFF79740E, warning = 0xFFB57614,
        raised = 0xFFEBDBB2, backgroundAlt = 0xFFF9F5D7, subtle = 0xFF504945, muted = 0xFF928374,
    )

    val Monokai = Palette(
        isDark = true, background = 0xFF272822, foreground = 0xFFF8F8F2,
        primary = 0xFFA6E22E, secondary = 0xFF66D9EF, tertiary = 0xFFAE81FF,
        error = 0xFFF92672, success = 0xFFA6E22E, warning = 0xFFE6DB74,
        raised = 0xFF3E3D32, backgroundAlt = 0xFF1E1F1C, muted = 0xFF75715E,
    )

    val TokyoNight = Palette(
        isDark = true, background = 0xFF1A1B26, foreground = 0xFFC0CAF5,
        primary = 0xFF7AA2F7, secondary = 0xFFBB9AF7, tertiary = 0xFF7DCFFF,
        error = 0xFFF7768E, success = 0xFF9ECE6A, warning = 0xFFE0AF68,
        raised = 0xFF292E42, backgroundAlt = 0xFF16161E, subtle = 0xFFA9B1D6, muted = 0xFF565F89,
    )
    val TokyoNightStorm = Palette(
        isDark = true, background = 0xFF24283B, foreground = 0xFFC0CAF5,
        primary = 0xFF7AA2F7, secondary = 0xFFBB9AF7, tertiary = 0xFF7DCFFF,
        error = 0xFFF7768E, success = 0xFF9ECE6A, warning = 0xFFE0AF68,
        raised = 0xFF3B4261, backgroundAlt = 0xFF1F2335, subtle = 0xFFA9B1D6, muted = 0xFF565F89,
    )
    val TokyoNightDay = Palette(
        isDark = false, background = 0xFFE1E2E7, foreground = 0xFF3760BF,
        primary = 0xFF2E7DE9, secondary = 0xFF9854F1, tertiary = 0xFF007197,
        error = 0xFFF52A65, success = 0xFF587539, warning = 0xFF8C6C3E,
        raised = 0xFFD0D5E3, subtle = 0xFF6172B0, muted = 0xFF848CB5,
    )

    val CatppuccinLatte = Palette(
        isDark = false, background = 0xFFEFF1F5, foreground = 0xFF4C4F69,
        primary = 0xFF8839EF, secondary = 0xFF1E66F5, tertiary = 0xFFFE640B,
        error = 0xFFD20F39, success = 0xFF40A02B, warning = 0xFFDF8E1D,
        raised = 0xFFDCE0E8, subtle = 0xFF6C6F85, muted = 0xFF9CA0B0,
    )
    val CatppuccinFrappe = Palette(
        isDark = true, background = 0xFF303446, foreground = 0xFFC6D0F5,
        primary = 0xFFCA9EE6, secondary = 0xFF8CAAEE, tertiary = 0xFFEF9F76,
        error = 0xFFE78284, success = 0xFFA6D189, warning = 0xFFE5C890,
        raised = 0xFF414559, backgroundAlt = 0xFF232634, subtle = 0xFFA5ADCE, muted = 0xFF737994,
    )
    val CatppuccinMacchiato = Palette(
        isDark = true, background = 0xFF24273A, foreground = 0xFFCAD3F5,
        primary = 0xFFC6A0F6, secondary = 0xFF8AADF4, tertiary = 0xFFF5A97F,
        error = 0xFFED8796, success = 0xFFA6DA95, warning = 0xFFEED49F,
        raised = 0xFF363A4F, backgroundAlt = 0xFF181926, subtle = 0xFFA5ADCB, muted = 0xFF6E738D,
    )
    val CatppuccinMocha = Palette(
        isDark = true, background = 0xFF1E1E2E, foreground = 0xFFCDD6F4,
        primary = 0xFFCBA6F7, secondary = 0xFF89B4FA, tertiary = 0xFFFAB387,
        error = 0xFFF38BA8, success = 0xFFA6E3A1, warning = 0xFFF9E2AF,
        raised = 0xFF313244, backgroundAlt = 0xFF11111B, subtle = 0xFFA6ADC8, muted = 0xFF6C7086,
    )

    val AyuDark = Palette(
        isDark = true, background = 0xFF10141C, foreground = 0xFFBFBDB6,
        primary = 0xFFE6B450, secondary = 0xFF59C2FF, tertiary = 0xFFD2A6FF,
        error = 0xFFD95757, success = 0xFFAAD94C, warning = 0xFFFF8F40,
        raised = 0xFF141821, backgroundAlt = 0xFF0D1017, muted = 0xFF5A6378,
    )
    val AyuMirage = Palette(
        isDark = true, background = 0xFF242936, foreground = 0xFFCCCAC2,
        primary = 0xFFFFCC66, secondary = 0xFF73D0FF, tertiary = 0xFFDFBFFF,
        error = 0xFFFF6666, success = 0xFFD5FF80, warning = 0xFFFFA659,
        raised = 0xFF282E3B, backgroundAlt = 0xFF1F2430, muted = 0xFF707A8C,
    )
    val AyuLight = Palette(
        isDark = false, background = 0xFFFCFCFC, foreground = 0xFF5C6166,
        primary = 0xFFF29718, secondary = 0xFF22A4E6, tertiary = 0xFFA37ACC,
        error = 0xFFE65050, success = 0xFF86B300, warning = 0xFFFA8532,
        raised = 0xFFF8F9FA, backgroundAlt = 0xFFFFFFFF, muted = 0xFF828E9F,
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
    val RosePineMoon = Palette(
        isDark = true, background = 0xFF232136, foreground = 0xFFE0DEF4,
        primary = 0xFFC4A7E7, secondary = 0xFFEA9A97, tertiary = 0xFF9CCFD8,
        error = 0xFFEB6F92, success = 0xFF9CCFD8, warning = 0xFFF6C177,
        raised = 0xFF393552, subtle = 0xFF908CAA, muted = 0xFF6E6A86,
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
    val EverforestLight = Palette(
        isDark = false, background = 0xFFFDF6E3, foreground = 0xFF5C6A72,
        primary = 0xFF8DA101, secondary = 0xFF35A77C, tertiary = 0xFFF57D26,
        error = 0xFFF85552, success = 0xFF8DA101, warning = 0xFFDFA000,
        raised = 0xFFEFEBD4, subtle = 0xFF829181, muted = 0xFF939F91,
    )

    // Kanagawa Wave and Lotus
    val Kanagawa = Palette(
        isDark = true, background = 0xFF1F1F28, foreground = 0xFFDCD7BA,
        primary = 0xFF7E9CD8, secondary = 0xFF957FB8, tertiary = 0xFFE6C384,
        error = 0xFFFF5D62, success = 0xFF98BB6C, warning = 0xFFFF9E3B,
        raised = 0xFF363646, backgroundAlt = 0xFF16161D, subtle = 0xFFC8C093, muted = 0xFF727169,
    )
    val KanagawaLotus = Palette(
        isDark = false, background = 0xFFF2ECBC, foreground = 0xFF545464,
        primary = 0xFF4D699B, secondary = 0xFF624C83, tertiary = 0xFFCC6D00,
        error = 0xFFC84053, success = 0xFF6F894E, warning = 0xFFE98A00,
        raised = 0xFFE7DBA0, subtle = 0xFF716E61, muted = 0xFF8A8980,
    )

    val MaterialPalenight = Palette(
        isDark = true, background = 0xFF292D3E, foreground = 0xFFA6ACCD,
        primary = 0xFFC792EA, secondary = 0xFF82AAFF, tertiary = 0xFF89DDFF,
        error = 0xFFFF5370, success = 0xFFC3E88D, warning = 0xFFFFCB6B,
        raised = 0xFF34324A, backgroundAlt = 0xFF202331, muted = 0xFF676E95,
    )

    // VS Code Dark/Light Modern UI with the Dark+/Light+ syntax colors
    val VsCodeDark = Palette(
        isDark = true, background = 0xFF1F1F1F, foreground = 0xFFCCCCCC,
        primary = 0xFF4DAAFC, secondary = 0xFF4EC9B0, tertiary = 0xFFC586C0,
        error = 0xFFF85149, success = 0xFF2EA043, warning = 0xFFCCA700,
        raised = 0xFF313131, backgroundAlt = 0xFF181818, subtle = 0xFF9D9D9D, muted = 0xFF6E7681,
    )
    val VsCodeLight = Palette(
        isDark = false, background = 0xFFFFFFFF, foreground = 0xFF3B3B3B,
        primary = 0xFF005FB8, secondary = 0xFF267F99, tertiary = 0xFFAF00DB,
        error = 0xFFF85149, success = 0xFF2EA043, warning = 0xFFBF8803,
        raised = 0xFFF8F8F8, subtle = 0xFF616161, muted = 0xFF6E7681,
    )

    // Retrowave
    val Synthwave84 = Palette(
        isDark = true, background = 0xFF262335, foreground = 0xFFFFFFFF,
        primary = 0xFFFF7EDB, secondary = 0xFF36F9F6, tertiary = 0xFFFEDE5D,
        error = 0xFFFE4450, success = 0xFF72F1B8, warning = 0xFFFF8B39,
        raised = 0xFF463465, backgroundAlt = 0xFF171520, muted = 0xFF848BBD,
    )
    val LaserWave = Palette(
        isDark = true, background = 0xFF27212E, foreground = 0xFFFFFFFF,
        primary = 0xFFEB64B9, secondary = 0xFF40B4C4, tertiary = 0xFFFFE261,
        error = 0xFFFF3E7B, success = 0xFF74DFC4, warning = 0xFFFFB85B,
        raised = 0xFF3E3549, backgroundAlt = 0xFF242029, subtle = 0xFFDDDDDD, muted = 0xFF91889B,
    )
    val ShadesOfPurple = Palette(
        isDark = true, background = 0xFF2D2B55, foreground = 0xFFFFFFFF,
        primary = 0xFFFAD000, secondary = 0xFF9EFFFF, tertiary = 0xFFFB94FF,
        error = 0xFFEC3A37, success = 0xFFA5FF90, warning = 0xFFFF9D00,
        backgroundAlt = 0xFF1E1E3F, subtle = 0xFFA599E9,
        // Its buttons are solid yellow; a yellow tint over purple would turn brown.
        primaryContainer = 0xFFFAD000,
    )
}
