package com.phobos.emulator.data

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.ui.DIRECT_SLOT_HOTKEY_ACTIONS
import com.phobos.emulator.ui.HOTKEY_ACTIONS
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
 * Hotkeys in the settings file: the values builds from before several bindings wrote, every action the Hotkeys page
 * binds read from its name, the bindings written now, a key in Back's place written at every level, and the controllers
 * seen sending Back. The keys are written out as the file holds them, so a renamed prefix, scope or action, which would
 * lose what players stored, fails here.
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

    private fun key(name: String) = stringPreferencesKey(name)

    @Test
    fun aValueAnEarlierBuildWroteIsTheFirstBindingInPlaceOfTheActionsDefaults() {
        withStore { store, dataStore ->
            dataStore.edit {
                it[key("hotkey_combo_save")] = "109,103"  // Select + R1: one combo, as those builds wrote it
                it[key("hotkey_combo_mute")] = "109,-1"  // Select + the left stick pushed right, an axis token
                it[key("hotkey_combo_pause")] = ""  // unbound
                it[key("scoped_hotkey|n64|load")] = "109,102"
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
    fun everyActionTheHotkeysPageBindsIsReadFromTheNameItIsStoredUnder() {
        val names = listOf(
            "ff_hold", "ff_toggle", "slow_motion", "save", "load", "inc_slot", "dec_slot", "pause", "reset", "reload",
            "quit", "screenshot", "mute", "frame_advance", "n64_pak", "analog_toggle", "keyboard", "library",
            "save_slot_1", "load_slot_1", "save_slot_2", "load_slot_2", "save_slot_3", "load_slot_3",
            "save_slot_4", "load_slot_4", "save_slot_5", "load_slot_5", "save_slot_6", "load_slot_6",
            "save_slot_7", "load_slot_7", "save_slot_8", "load_slot_8", "save_slot_9", "load_slot_9",
        )
        withStore { store, dataStore ->
            dataStore.edit { p -> names.forEachIndexed { i, name -> p[key("hotkey_combo_$name")] = "${i + 1}" } }
            val hotkeys = store.settings.first().hotkeys
            assertEquals(
                "the page's actions, then the direct slots, each with the value stored under its name",
                names.indices.map { listOf(listOf(it + 1)) },
                (HOTKEY_ACTIONS + DIRECT_SLOT_HOTKEY_ACTIONS).map { hotkeys[it.second] },
            )
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
            assertEquals("109,103;;135", stored[key("hotkey_combo_save")])
            assertEquals(";138", stored[key("hotkey_combo_screenshot")])
            assertEquals("", stored[key("hotkey_combo_pause")])
            assertEquals(";139", stored[key("scoped_hotkey|n64/Mario Tennis (USA).z64|load")])
            assertEquals("", stored[key("scoped_hotkey|n64/Mario Tennis (USA).z64|mute")])
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
            assertNull("inherited again", dataStore.data.first()[key("scoped_hotkey|n64/Mario Tennis (USA).z64|load")])
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
            assertEquals("110,103;135", stored[key("hotkey_combo_save")])
            assertEquals("110,97", stored[key("scoped_hotkey|n64|quit")])
            assertEquals(";110,102", stored[key("scoped_hotkey|n64/Mario Tennis (USA).z64|load")])
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
