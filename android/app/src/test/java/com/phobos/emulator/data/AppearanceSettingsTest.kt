package com.phobos.emulator.data

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class AppearanceSettingsTest {
    @get:Rule
    val folder = TemporaryFolder()

    /** The settings file opened as the app opens it; [use] runs with it open, and it's closed after. */
    private fun withStore(use: suspend (SettingsStore, DataStore<Preferences>) -> Unit) = runBlocking {
        val job = SupervisorJob()
        val file = folder.root.resolve("settings.preferences_pb")
        val dataStore = PreferenceDataStoreFactory.create(scope = CoroutineScope(Dispatchers.IO + job)) { file }
        try {
            use(SettingsStore(dataStore), dataStore)
        } finally {
            job.cancelAndJoin()
        }
    }

    @Test
    fun aRemovedPixelSceneReadsAsTheClosestKeptOne() {
        val expected = mapOf(
            "SPACE" to PixelBackdropScene.SAKURA_NIGHT,
            "CASTLE" to PixelBackdropScene.SAKURA_NIGHT,
            "UNDERWATER" to PixelBackdropScene.SAKURA_DAY,
            "SAKURA" to PixelBackdropScene.SAKURA_NIGHT,
            "A_LATER_SCENE" to PixelBackdropScene.SAKURA_CYCLE,
        )
        withStore { store, dataStore ->
            assertEquals("the default", PixelBackdropScene.SAKURA_CYCLE, store.settings.first().pixelBackdrop)
            for ((stored, scene) in expected) {
                dataStore.edit { it[SettingsStore.PIXEL_BACKDROP] = stored }
                assertEquals(stored, scene, store.settings.first().pixelBackdrop)
            }
        }
    }

    @Test
    fun aScreensaverOnARemovedPixelSceneShowsTheClosestKeptOne() {
        val expected = mapOf(
            "PIXEL_SPACE" to CatalogBackdrop.PIXEL_SAKURA_NIGHT,
            "PIXEL_CASTLE" to CatalogBackdrop.PIXEL_SAKURA_NIGHT,
            "PIXEL_UNDERWATER" to CatalogBackdrop.PIXEL_SAKURA_DAY,
            "PIXEL_SAKURA" to CatalogBackdrop.PIXEL_SAKURA,
            "SPACE" to CatalogBackdrop.MATCH_APP,
            "A_LATER_BACKDROP" to CatalogBackdrop.MATCH_APP,
        )
        withStore { store, dataStore ->
            assertEquals("the default", CatalogBackdrop.MATCH_APP, store.settings.first().screensaverBackdrop)
            for ((stored, backdrop) in expected) {
                dataStore.edit { it[SettingsStore.SCREENSAVER_BACKDROP] = stored }
                assertEquals(stored, backdrop, store.settings.first().screensaverBackdrop)
            }
        }
    }

    @Test
    fun aRemovedIconPackReadsAsMatchStyle() {
        val expected = mapOf(
            "PHOBOS" to PlatformIconPack.MATCH_STYLE,
            "PIXEL" to PlatformIconPack.MATCH_STYLE,
            "MANGA" to PlatformIconPack.MATCH_STYLE,
            "SYSTEMATIC" to PlatformIconPack.SYSTEMATIC,
            "MATCH_STYLE" to PlatformIconPack.MATCH_STYLE,
        )
        withStore { store, dataStore ->
            assertEquals("the default", PlatformIconPack.MATCH_STYLE, store.settings.first().platformIconPack)
            for ((stored, pack) in expected) {
                dataStore.edit { it[SettingsStore.PLATFORM_ICON_PACK] = stored }
                assertEquals(stored, pack, store.settings.first().platformIconPack)
            }
        }
    }

    @Test
    fun everyKeptSceneAndScreensaverBackdropKeepsItsValueAcrossStarts() {
        for (scene in PixelBackdropScene.entries) {
            withStore { store, _ -> store.setPixelBackdrop(scene) }
            withStore { store, _ -> assertEquals(scene, store.settings.first().pixelBackdrop) }
        }
        for (backdrop in CatalogBackdrop.entries) {
            withStore { store, _ -> store.setScreensaverBackdrop(backdrop) }
            withStore { store, _ -> assertEquals(backdrop, store.settings.first().screensaverBackdrop) }
        }
    }
}
