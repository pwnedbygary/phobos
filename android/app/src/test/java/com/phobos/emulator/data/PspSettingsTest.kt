package com.phobos.emulator.data

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import com.phobos.emulator.input.PspRightStick
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

class PspSettingsTest {
    @get:Rule
    val folder = TemporaryFolder()

    /** The settings file opened as the app opens it at each start; [use] runs with it open, and it's closed after. */
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

    private suspend fun SettingsStore.current() = settings.first()

    @Test
    fun theRightStickIsOffUntilChosenAndKeepsEachChoiceAcrossStarts() {
        withStore { store, _ ->
            assertEquals(PspRightStick.OFF, store.current().pspRightStick)
            for (choice in PspRightStick.entries) {
                store.setPspRightStick(choice)
                assertEquals(choice, store.current().pspRightStick)
            }
            store.setPspRightStick(PspRightStick.FACE_BUTTONS)
        }
        withStore { store, _ -> assertEquals(PspRightStick.FACE_BUTTONS, store.current().pspRightStick) }
    }

    @Test
    fun aRightStickChoiceThisBuildDoesntKnowIsOff() {
        withStore { store, dataStore ->
            dataStore.edit { it[SettingsStore.PSP_RIGHT_STICK] = "BOTH_STICKS" }  // a later build's, say
            assertEquals(PspRightStick.OFF, store.current().pspRightStick)
            dataStore.edit {
                it.remove(SettingsStore.PSP_RIGHT_STICK)
                it[intPreferencesKey(SettingsStore.PSP_RIGHT_STICK.name)] = 1
            }
            assertEquals(PspRightStick.OFF, store.current().pspRightStick)
        }
    }

    @Test
    fun everyPspSettingKeepsItsValueAcrossStarts() {
        withStore { store, _ ->
            val defaults = store.current()
            assertEquals(0, defaults.pspRenderer)
            assertEquals(1, defaults.pspResolution)
            assertEquals(0, defaults.pspDrawingThreads)
            store.setPspRenderer(2)
            store.setPspResolution(4)
            store.setPspDrawingThreads(6)
            store.setPspRightStick(PspRightStick.DPAD)
        }
        withStore { store, _ ->
            val saved = store.current()
            assertEquals(2, saved.pspRenderer)
            assertEquals(4, saved.pspResolution)
            assertEquals(6, saved.pspDrawingThreads)
            assertEquals(PspRightStick.DPAD, saved.pspRightStick)
        }
    }
}
