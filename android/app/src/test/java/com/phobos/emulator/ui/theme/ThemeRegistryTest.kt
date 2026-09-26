package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.ThemeMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
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
    fun includesTheRequestedColorways() {
        val names = ThemeRegistry.all.map { it.name }.toSet()
        listOf(
            "System", "Phobos", "One Dark", "One Light", "Dracula", "Nord", "Solarized Dark", "Solarized Light",
            "GitHub Dark", "GitHub Light", "Gruvbox Dark", "Gruvbox Light", "Monokai", "Tokyo Night",
            "Catppuccin Latte", "Catppuccin Frappé", "Catppuccin Macchiato", "Catppuccin Mocha",
            "Ayu Dark", "Ayu Mirage", "Ayu Light", "Night Owl", "Rosé Pine", "Rosé Pine Dawn",
            "Everforest Dark", "Kanagawa", "Material Palenight", "Synthwave '84",
        ).forEach { assertTrue("missing $it", it in names) }
    }

    @Test
    fun siblingsExistWithOppositeBrightness() {
        ThemeRegistry.all.filter { it.sibling != null }.forEach { theme ->
            val sibling = ThemeRegistry.sibling(theme)
            assertNotNull("${theme.name} names a missing sibling", sibling)
            assertFalse("${theme.name} is adaptive and can't have a sibling", theme.adaptive)
            assertTrue("${theme.name} and ${sibling!!.name} have the same brightness", theme.isDark != sibling.isDark)
        }
    }

    @Test
    fun defaultAndUnknownIdsResolveToSystem() {
        assertEquals(ThemeRegistry.SYSTEM_ID, ThemeRegistry.find("no_such_theme").id)
        val resolved = ThemeRegistry.resolve("no_such_theme", ThemeMode.AUTO, followSystem = false, systemDark = true)
        assertEquals(ThemeRegistry.SYSTEM_ID, resolved.theme.id)
        assertTrue(resolved.isDark)
    }

    @Test
    fun adaptiveThemesFollowTheMode() {
        val id = ThemeRegistry.SYSTEM_ID
        assertTrue(ThemeRegistry.resolve(id, ThemeMode.AUTO, followSystem = false, systemDark = true).isDark)
        assertFalse(ThemeRegistry.resolve(id, ThemeMode.AUTO, followSystem = false, systemDark = false).isDark)
        assertFalse(ThemeRegistry.resolve(id, ThemeMode.LIGHT, followSystem = false, systemDark = true).isDark)
        assertTrue(ThemeRegistry.resolve("phobos", ThemeMode.DARK, followSystem = false, systemDark = false).isDark)
    }

    @Test
    fun pairedThemesSwitchToTheirSiblingOnlyWhenFollowingTheSystem() {
        assertEquals("one_dark", ThemeRegistry.resolve("one_dark", ThemeMode.AUTO, followSystem = false, systemDark = false).theme.id)
        assertEquals("one_light", ThemeRegistry.resolve("one_dark", ThemeMode.AUTO, followSystem = true, systemDark = false).theme.id)
        assertEquals("one_dark", ThemeRegistry.resolve("one_dark", ThemeMode.AUTO, followSystem = true, systemDark = true).theme.id)
        assertEquals("dracula", ThemeRegistry.resolve("dracula", ThemeMode.LIGHT, followSystem = true, systemDark = false).theme.id)
    }
}
