package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.EmulatorSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ThemeRegistryTest {

    @Test
    fun idsAreUniqueAndGroupsArePopulated() {
        val ids = ThemeRegistry.all.map { it.id }
        assertEquals(ids.distinct(), ids)
        ThemeGroup.entries.forEach { group -> assertTrue(group.name, ThemeRegistry.all.any { it.group == group }) }
    }

    @Test
    fun theThirteenKeptThemesUnderTheirStoredIdsAndNoOthers() {
        // The IDs players' settings store them by: one renamed would read as Phobos, and be stored as Phobos at the
        // next start.
        val kept = mapOf(
            "phobos" to "Phobos", "synthwave_84" to "Synthwave '84", "laserwave" to "LaserWave",
            "shades_of_purple" to "Shades of Purple", "rose_pine_dawn" to "Rosé Pine Dawn", "ayu_dark" to "Ayu Dark",
            "night_owl" to "Night Owl", "kanagawa" to "Kanagawa", "rose_pine" to "Rosé Pine",
            "everforest_dark" to "Everforest Dark", "gruvbox_dark" to "Gruvbox Dark", "monokai" to "Monokai",
            "dracula" to "Dracula",
        )
        assertEquals(kept, ThemeRegistry.all.associate { it.id to it.name })
    }

    @Test
    fun rosePineDawnIsTheOneLightTheme() {
        assertEquals(listOf("Rosé Pine Dawn"), ThemeRegistry.all.filterNot { it.isDark }.map { it.name })
    }

    @Test
    fun phobosIsTheDefaultAndWhatRemovedOrUnknownIdsAre() {
        assertEquals("phobos", EmulatorSettings().themeId)
        for (id in listOf("system", "catppuccin_mocha", "one_light", "tokyo_night_day", "no_such_theme", "")) {
            assertEquals(id, "phobos", ThemeRegistry.find(id).id)
        }
    }

    @Test
    fun phobosWearsTheWebsitesColorsMarsOrangeLeading() {
        // The website's stylesheet: deep space, its text and muted text; Mars orange, then violet and gold.
        val p = ThemeRegistry.find("phobos").palette
        assertEquals(listOf(0xFF07050F, 0xFFEFEAFF, 0xFFB4A9D8), listOf(p.background, p.foreground, p.subtle))
        assertEquals(listOf(0xFFFF5A1F, 0xFF8B6CF0, 0xFFF2B84B), listOf(p.primary, p.secondary, p.tertiary))
    }

    @Test
    fun synthwave84LeadsWithItsCoralAndLaserWaveTakesItsNeonColors() {
        // Synthwave '84's published colors, its neon pink leading, are all LaserWave's; Synthwave '84 has them too,
        // but led by its sunset coral.
        val neon = Palette(
            isDark = true, background = 0xFF262335, foreground = 0xFFFFFFFF,
            primary = 0xFFFF7EDB, secondary = 0xFF36F9F6, tertiary = 0xFFFEDE5D,
            error = 0xFFFE4450, success = 0xFF72F1B8, warning = 0xFFFF8B39,
            raised = 0xFF463465, backgroundAlt = 0xFF171520, muted = 0xFF848BBD,
        )
        assertEquals(neon, ThemeRegistry.find("laserwave").palette)
        assertEquals(neon.copy(primary = 0xFFF97E72), ThemeRegistry.find("synthwave_84").palette)
    }
}
