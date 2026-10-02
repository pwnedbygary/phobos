package com.phobos.emulator.data

/**
 * Every drawable backdrop across all style effects. Style-specific Appearance pickers keep using
 * [PixelBackdropScene] / [MangaBackdropScene] / …; the screensaver can pick any entry here, including
 * ones from styles that are not currently active. New screensaver ideas must also land in their
 * style's enum so they appear under Appearance → Backdrop for that style.
 */
enum class CatalogBackdrop(
    val group: String,
    val label: String,
    val description: String,
) {
    MATCH_APP(
        "Screensaver",
        "Match app",
        "Use whichever backdrop the active style currently has selected",
    ),

    PIXEL_SPACE("Pixel art", PixelBackdropScene.SPACE.label, PixelBackdropScene.SPACE.description),
    PIXEL_NIGHT_DRIVE("Pixel art", PixelBackdropScene.NIGHT_DRIVE.label, PixelBackdropScene.NIGHT_DRIVE.description),
    PIXEL_PLAINS("Pixel art", PixelBackdropScene.PLAINS.label, PixelBackdropScene.PLAINS.description),
    PIXEL_SAKURA("Pixel art", PixelBackdropScene.SAKURA_CYCLE.label, PixelBackdropScene.SAKURA_CYCLE.description),
    PIXEL_SAKURA_NIGHT("Pixel art", PixelBackdropScene.SAKURA_NIGHT.label, PixelBackdropScene.SAKURA_NIGHT.description),
    PIXEL_SAKURA_DAY("Pixel art", PixelBackdropScene.SAKURA_DAY.label, PixelBackdropScene.SAKURA_DAY.description),
    PIXEL_UNDERWATER("Pixel art", PixelBackdropScene.UNDERWATER.label, PixelBackdropScene.UNDERWATER.description),
    PIXEL_CASTLE("Pixel art", PixelBackdropScene.CASTLE.label, PixelBackdropScene.CASTLE.description),

    MANGA_TONE("Manga ink", MangaBackdropScene.TONE.label, MangaBackdropScene.TONE.description),
    MANGA_SPEED_LINES("Manga ink", MangaBackdropScene.SPEED_LINES.label, MangaBackdropScene.SPEED_LINES.description),
    MANGA_SPLASH("Manga ink", MangaBackdropScene.SPLASH.label, MangaBackdropScene.SPLASH.description),

    RPG_LATTICE("16-bit RPG", RpgBackdropScene.LATTICE.label, RpgBackdropScene.LATTICE.description),
    RPG_STARS("16-bit RPG", RpgBackdropScene.STARS.label, RpgBackdropScene.STARS.description),
    RPG_DUNGEON("16-bit RPG", RpgBackdropScene.DUNGEON.label, RpgBackdropScene.DUNGEON.description),

    RETRO_SUNSET("Retrowave", RetrowaveBackdropScene.SUNSET.label, RetrowaveBackdropScene.SUNSET.description),
    RETRO_GRID("Retrowave", RetrowaveBackdropScene.GRID.label, RetrowaveBackdropScene.GRID.description),
    RETRO_CITY("Retrowave", RetrowaveBackdropScene.CITY.label, RetrowaveBackdropScene.CITY.description),

    CRT_GREEN("CRT terminal", CrtBackdropScene.GREEN.label, CrtBackdropScene.GREEN.description),
    CRT_AMBER("CRT terminal", CrtBackdropScene.AMBER.label, CrtBackdropScene.AMBER.description),
    CRT_BLUE("CRT terminal", CrtBackdropScene.BLUE.label, CrtBackdropScene.BLUE.description),

    GLASS_GLOWS("Glass", GlassBackdropScene.GLOWS.label, GlassBackdropScene.GLOWS.description),
    GLASS_AURORA("Glass", GlassBackdropScene.AURORA.label, GlassBackdropScene.AURORA.description),
    GLASS_MESH("Glass", GlassBackdropScene.MESH.label, GlassBackdropScene.MESH.description),

    XMB_WAVES("XMB waves", XmbBackdropScene.WAVES.label, XmbBackdropScene.WAVES.description),
    XMB_CALM("XMB waves", XmbBackdropScene.CALM.label, XmbBackdropScene.CALM.description),
    XMB_DEEP("XMB waves", XmbBackdropScene.DEEP.label, XmbBackdropScene.DEEP.description),
    ;

    /** Label for dropdowns: "Pixel art · Space". */
    val menuLabel: String get() = if (this == MATCH_APP) label else "$group · $label"

    companion object {
        /** Scenes the screensaver picker offers (includes Match app). */
        val screensaverChoices: List<CatalogBackdrop> = entries

        /** Resolves the app's active style backdrop into a concrete catalog entry (never Match app). */
        fun fromAppSettings(settings: EmulatorSettings): CatalogBackdrop = when (settings.uiEffects) {
            UiEffects.PIXEL_ART -> settings.pixelBackdrop.toCatalog()
            UiEffects.MANGA -> settings.mangaBackdrop.toCatalog()
            UiEffects.RPG -> settings.rpgBackdrop.toCatalog()
            UiEffects.RETROWAVE -> settings.retrowaveBackdrop.toCatalog()
            UiEffects.CRT -> settings.crtBackdrop.toCatalog()
            UiEffects.XMB -> settings.xmbBackdrop.toCatalog()
            UiEffects.NONE -> settings.glassBackdrop.toCatalog()
        }
    }
}

fun PixelBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    PixelBackdropScene.SPACE -> CatalogBackdrop.PIXEL_SPACE
    PixelBackdropScene.NIGHT_DRIVE -> CatalogBackdrop.PIXEL_NIGHT_DRIVE
    PixelBackdropScene.PLAINS -> CatalogBackdrop.PIXEL_PLAINS
    PixelBackdropScene.SAKURA_CYCLE -> CatalogBackdrop.PIXEL_SAKURA
    PixelBackdropScene.SAKURA_NIGHT -> CatalogBackdrop.PIXEL_SAKURA_NIGHT
    PixelBackdropScene.SAKURA_DAY -> CatalogBackdrop.PIXEL_SAKURA_DAY
    PixelBackdropScene.UNDERWATER -> CatalogBackdrop.PIXEL_UNDERWATER
    PixelBackdropScene.CASTLE -> CatalogBackdrop.PIXEL_CASTLE
}

fun MangaBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    MangaBackdropScene.TONE -> CatalogBackdrop.MANGA_TONE
    MangaBackdropScene.SPEED_LINES -> CatalogBackdrop.MANGA_SPEED_LINES
    MangaBackdropScene.SPLASH -> CatalogBackdrop.MANGA_SPLASH
}

fun RpgBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    RpgBackdropScene.LATTICE -> CatalogBackdrop.RPG_LATTICE
    RpgBackdropScene.STARS -> CatalogBackdrop.RPG_STARS
    RpgBackdropScene.DUNGEON -> CatalogBackdrop.RPG_DUNGEON
}

fun RetrowaveBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    RetrowaveBackdropScene.SUNSET -> CatalogBackdrop.RETRO_SUNSET
    RetrowaveBackdropScene.GRID -> CatalogBackdrop.RETRO_GRID
    RetrowaveBackdropScene.CITY -> CatalogBackdrop.RETRO_CITY
}

fun CrtBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    CrtBackdropScene.GREEN -> CatalogBackdrop.CRT_GREEN
    CrtBackdropScene.AMBER -> CatalogBackdrop.CRT_AMBER
    CrtBackdropScene.BLUE -> CatalogBackdrop.CRT_BLUE
}

fun GlassBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    GlassBackdropScene.GLOWS -> CatalogBackdrop.GLASS_GLOWS
    GlassBackdropScene.AURORA -> CatalogBackdrop.GLASS_AURORA
    GlassBackdropScene.MESH -> CatalogBackdrop.GLASS_MESH
}

fun XmbBackdropScene.toCatalog(): CatalogBackdrop = when (this) {
    XmbBackdropScene.WAVES -> CatalogBackdrop.XMB_WAVES
    XmbBackdropScene.CALM -> CatalogBackdrop.XMB_CALM
    XmbBackdropScene.DEEP -> CatalogBackdrop.XMB_DEEP
}

/** Concrete scene to draw for the screensaver (Match app resolves against [settings]). */
fun CatalogBackdrop.resolve(settings: EmulatorSettings): CatalogBackdrop =
    if (this == CatalogBackdrop.MATCH_APP) CatalogBackdrop.fromAppSettings(settings) else this
