package com.phobos.emulator.data

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.input.ScopedKeys
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Hotkeys in the settings file: the values builds from before several bindings wrote, the bindings written now, a key in
 * Back's place written at every level, and the controllers seen sending Back.
 */
class HotkeySettingsTest {
    @get:Rule
    val folder = TemporaryFolder()

    private val console = ControlLevel.Console("n64").scope!!
    private val game = ControlLevel.Game("n64", "Mario Tennis (USA).z64").scope!!

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

    private fun global(action: String) = stringPreferencesKey(SettingsStore.HOTKEYS_PREFIX + action)

    private fun scoped(scope: String, action: String) = stringPreferencesKey(ScopedKeys.hotkey(scope, action))

    @Test
    fun aValueAnEarlierBuildWroteIsTheFirstBindingInPlaceOfTheActionsDefaults() {
        withStore { store, dataStore ->
            dataStore.edit {
                it[global(HotkeyAction.SAVE_STATE)] = "109,103"  // Select + R1: one combo, as those builds wrote it
                it[global(HotkeyAction.MUTE)] = "109,-1"  // Select + the left stick pushed right, an axis token
                it[global(HotkeyAction.PAUSE)] = ""  // unbound
                it[scoped(console, HotkeyAction.LOAD_STATE)] = "109,102"
            }
            val settings = store.settings.first()
            assertEquals(
                "no F5 beside it: a stored value replaces the action's defaults",
                listOf(listOf(SELECT, R1)),
                settings.hotkeys[HotkeyAction.SAVE_STATE],
            )
            assertEquals(listOf(listOf(SELECT, -1)), settings.hotkeys[HotkeyAction.MUTE])
            assertFalse("an empty value unbinds", HotkeyAction.PAUSE in settings.hotkeys)
            assertEquals(
                "an action never bound keeps its defaults, the keyboard's included",
                listOf(listOf(BACK, L1), listOf(F9)),
                settings.hotkeys[HotkeyAction.LOAD_STATE],
            )
            assertEquals(listOf(listOf(SELECT, L1)), settings.controlOverrides.hotkeys[console]?.get(HotkeyAction.LOAD_STATE))
        }
    }

    @Test
    fun bindingsAreWrittenJoinedBySemicolonsAndComeBackInTheirSlotsAtTheNextStart() {
        withStore { store, dataStore ->
            store.setHotkeys(
                mapOf(
                    HotkeyAction.SAVE_STATE to listOf(listOf(SELECT, R1), emptyList(), listOf(F5)),
                    HotkeyAction.SCREENSHOT to listOf(emptyList(), listOf(F8)),
                    HotkeyAction.PAUSE to emptyList<List<Int>>(),
                ),
            )
            store.setScopedHotkey(game, HotkeyAction.LOAD_STATE, listOf(emptyList(), listOf(F9)))
            store.setScopedHotkey(game, HotkeyAction.MUTE, emptyList())
            val stored = dataStore.data.first()
            assertEquals("109,103;;135", stored[global(HotkeyAction.SAVE_STATE)])
            assertEquals(";138", stored[global(HotkeyAction.SCREENSHOT)])
            assertEquals("", stored[global(HotkeyAction.PAUSE)])
            assertEquals(";139", stored[scoped(game, HotkeyAction.LOAD_STATE)])
            assertEquals("", stored[scoped(game, HotkeyAction.MUTE)])
        }
        withStore { store, dataStore ->
            val settings = store.settings.first()
            assertEquals(listOf(listOf(SELECT, R1), emptyList(), listOf(F5)), settings.hotkeys[HotkeyAction.SAVE_STATE])
            assertEquals(listOf(emptyList(), listOf(F8)), settings.hotkeys[HotkeyAction.SCREENSHOT])
            assertFalse(HotkeyAction.PAUSE in settings.hotkeys)
            val atGame = settings.controlOverrides.hotkeys[game]
            assertEquals(listOf(emptyList(), listOf(F9)), atGame?.get(HotkeyAction.LOAD_STATE))
            assertEquals("unbound at the game", emptyList<List<Int>>(), atGame?.get(HotkeyAction.MUTE))
            store.setScopedHotkey(game, HotkeyAction.LOAD_STATE, null)
            assertNull("inherited again", dataStore.data.first()[scoped(game, HotkeyAction.LOAD_STATE)])
        }
    }

    @Test
    fun aKeyInBacksPlaceIsWrittenToAllConsolesAndToEachLevelAsWholeActions() {
        withStore { store, dataStore ->
            store.setHotkeysEverywhere(
                mapOf(HotkeyAction.SAVE_STATE to listOf(listOf(MODE, R1), listOf(F5))),
                mapOf(
                    console to mapOf(HotkeyAction.QUIT to listOf(listOf(MODE, B))),
                    game to mapOf(HotkeyAction.LOAD_STATE to listOf(emptyList(), listOf(MODE, L1))),
                ),
            )
            val stored = dataStore.data.first()
            assertEquals("110,103;135", stored[stringPreferencesKey("hotkey_combo_save")])
            assertEquals("110,97", stored[stringPreferencesKey("scoped_hotkey|n64|quit")])
            assertEquals(";110,102", stored[stringPreferencesKey("scoped_hotkey|n64/Mario Tennis (USA).z64|load")])
        }
    }

    @Test
    fun theControllersSeenSendingBackAreKeptForTheNextStartThroughAResetOfTheHotkeys() {
        withStore { store, _ ->
            assertEquals(emptySet<String>(), store.settings.first().backSeenPads)
            store.addBackSeenPad("descriptor-1")
            store.addBackSeenPad("descriptor-2")
            store.addBackSeenPad("descriptor-1")
            store.clearHotkeys()
        }
        withStore { store, dataStore ->
            assertEquals(setOf("descriptor-1", "descriptor-2"), store.settings.first().backSeenPads)
            assertEquals(setOf("descriptor-1", "descriptor-2"), dataStore.data.first()[stringSetPreferencesKey("back_seen_pads")])
        }
    }

    companion object {
        // KeyEvent constants (android.view).
        const val BACK = 4
        const val B = 97
        const val L1 = 102
        const val R1 = 103
        const val SELECT = 109
        const val MODE = 110
        const val F5 = 135
        const val F8 = 138
        const val F9 = 139
    }
}
