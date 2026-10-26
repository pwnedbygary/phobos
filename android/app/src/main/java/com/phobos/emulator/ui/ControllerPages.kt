package com.phobos.emulator.ui

import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.ControlCapture
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.input.Controls
import com.phobos.emulator.input.DEFAULT_HOTKEYS
import com.phobos.emulator.input.HotkeyAction
import com.phobos.emulator.input.HotkeyBindings
import com.phobos.emulator.input.HotkeyConflict
import com.phobos.emulator.input.HotkeyTokens
import com.phobos.emulator.input.andList
import com.phobos.emulator.input.backSeen
import com.phobos.emulator.input.bindingTokens
import com.phobos.emulator.input.existingHotkeyConflicts
import com.phobos.emulator.input.hotkeyConflictMessage
import com.phobos.emulator.input.hotkeyConflicts
import com.phobos.emulator.ui.theme.pillShape

// The Buttons and Hotkeys pages, shared by Settings (Controller Mapping, Hotkeys) and the pause
// menu's Controller section. Each edits one level: a game, a console or all consoles.

/** The standard pad buttons physical controls are bound to, by the bit their bindings are stored under. */
val PAD_BUTTONS = listOf(
    "Up" to PhobosCore.Input.UP,
    "Down" to PhobosCore.Input.DOWN,
    "Left" to PhobosCore.Input.LEFT,
    "Right" to PhobosCore.Input.RIGHT,
    "A" to PhobosCore.Input.A,
    "B" to PhobosCore.Input.B,
    "X" to PhobosCore.Input.X,
    "Y" to PhobosCore.Input.Y,
    "L1" to PhobosCore.Input.L1,
    "R1" to PhobosCore.Input.R1,
    "L2" to PhobosCore.Input.L2,
    "R2" to PhobosCore.Input.R2,
    "L3" to PhobosCore.Input.L3,
    "R3" to PhobosCore.Input.R3,
    "Select" to PhobosCore.Input.SELECT,
    "Start" to PhobosCore.Input.START,
    "Home" to PhobosCore.Input.HOME,
    "L-Up" to PhobosCore.Input.LS_UP,
    "L-Down" to PhobosCore.Input.LS_DOWN,
    "L-Left" to PhobosCore.Input.LS_LEFT,
    "L-Right" to PhobosCore.Input.LS_RIGHT,
    "R-Up" to PhobosCore.Input.RS_UP,
    "R-Down" to PhobosCore.Input.RS_DOWN,
    "R-Left" to PhobosCore.Input.RS_LEFT,
    "R-Right" to PhobosCore.Input.RS_RIGHT,
)

val HOTKEY_ACTIONS = listOf(
    "Fast Forward (Hold)" to HotkeyAction.FAST_FORWARD_HOLD,
    "Fast Forward (Toggle)" to HotkeyAction.FAST_FORWARD_TOGGLE,
    "Slow Motion (Hold)" to HotkeyAction.SLOW_MOTION,
    "Save State" to HotkeyAction.SAVE_STATE,
    "Load State" to HotkeyAction.LOAD_STATE,
    "Next Slot" to HotkeyAction.NEXT_SLOT,
    "Previous Slot" to HotkeyAction.PREVIOUS_SLOT,
    "Pause Emulation" to HotkeyAction.PAUSE,
    "Reset System" to HotkeyAction.RESET,
    "Reload Current Game" to HotkeyAction.RELOAD,
    "Quit Emulator" to HotkeyAction.QUIT,
    "Capture Screenshot" to HotkeyAction.SCREENSHOT,
    "Mute Audio" to HotkeyAction.MUTE,
    "Frame Advance" to HotkeyAction.FRAME_ADVANCE,
    "Cycle N64 Controller Pak" to HotkeyAction.N64_PAK,
    "Toggle PS1 Analog" to HotkeyAction.PS1_ANALOG_TOGGLE,
    "Toggle On-Screen Keyboard" to HotkeyAction.KEYBOARD,
    "Swap to Library" to HotkeyAction.LIBRARY,
    "Show Screensaver" to HotkeyAction.SHOW_SCREENSAVER,
)

/** Direct slot bindings, hidden until the page's toggle is on. */
val DIRECT_SLOT_HOTKEY_ACTIONS = (1..9).flatMap { slot ->
    listOf(
        "Save State: Slot $slot" to "${HotkeyAction.SAVE_SLOT_PREFIX}$slot",
        "Load State: Slot $slot" to "${HotkeyAction.LOAD_SLOT_PREFIX}$slot",
    )
}

enum class ControllerPage(val title: String) { BUTTONS("Buttons"), HOTKEYS("Hotkeys") }

fun ControlLevel.label(): String = when (this) {
    ControlLevel.AllConsoles -> "All consoles"
    is ControlLevel.Console -> system
    is ControlLevel.Game -> "This game"
}

private fun ControlLevel.hint(): String = when (this) {
    ControlLevel.AllConsoles -> "For every game, unless its console or the game changes them."
    is ControlLevel.Console -> "For every $system game, unless the game changes them."
    is ControlLevel.Game -> "For this game only."
}

/** Where a row's value comes from, as the row says it; null on the all-consoles page and when nothing sets it. */
private fun sourceLabel(level: ControlLevel, source: ControlLevel?): String? = when {
    level == ControlLevel.AllConsoles || source == null -> null
    source == level -> if (level is ControlLevel.Game) "set for this game" else "set for ${level.label()}"
    source == ControlLevel.AllConsoles -> "from all consoles"
    else -> "from ${source.label()}"
}

/** A key as the rows show it: "A", "L1", "Start", "D-pad up", "Back". */
fun keyLabel(keyCode: Int): String {
    val name = KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")
    return when {
        name == "BACK" -> "Back"
        name == "BUTTON_THUMBL" -> "L3"
        name == "BUTTON_THUMBR" -> "R3"
        name.startsWith("BUTTON_") -> name.removePrefix("BUTTON_").let { if (it.length > 2) it.lowercase().replaceFirstChar(Char::uppercase) else it }
        name.startsWith("DPAD_") -> "D-pad " + name.removePrefix("DPAD_").lowercase()
        else -> name
    }
}

/** A hotkey token as the rows show it: a key's label, or an axis direction's. */
fun hotkeyTokenLabel(token: Int): String {
    val axis = HotkeyTokens.axisOf(token) ?: return keyLabel(token)
    return bindingLabel("a:${axis.first}:${if (axis.second) 1 else 0}")
}

fun comboLabel(combo: List<Int>): String = combo.joinToString(" + ", transform = ::hotkeyTokenLabel)

/** A stored binding as the rows show it: "L1", "D-pad up", "Left stick right". */
fun bindingLabel(binding: String): String {
    val parts = binding.split(":")
    val code = parts.getOrNull(1)?.toIntOrNull() ?: return binding
    return when {
        parts.size == 2 && parts[0] == "k" -> keyLabel(code)
        parts.size == 3 && parts[0] == "a" -> {
            val positive = parts[2] == "1"
            val across = if (positive) "right" else "left"
            val along = if (positive) "down" else "up"
            when (code) {
                MotionEvent.AXIS_X -> "Left stick $across"
                MotionEvent.AXIS_Y -> "Left stick $along"
                MotionEvent.AXIS_Z -> "Right stick $across"
                MotionEvent.AXIS_RZ -> "Right stick $along"
                MotionEvent.AXIS_HAT_X -> "D-pad $across"
                MotionEvent.AXIS_HAT_Y -> "D-pad $along"
                MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE -> "Left trigger"
                MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS -> "Right trigger"
                else -> "Axis $code ${if (positive) "+" else "−"}"
            }
        }
        else -> binding
    }
}

/**
 * One level's button bindings. [consoleNames] are the running game's names for the pad buttons, on
 * [console]; its own pages leave out the pad buttons it doesn't use. The stick rows always stay,
 * since they also choose the axes of the analog sticks.
 */
@Composable
fun ControllerButtonsList(
    viewModel: MainViewModel, level: ControlLevel, consoleNames: Map<Int, String>, inMenu: Boolean,
    modifier: Modifier = Modifier, console: String? = null, header: (@Composable () -> Unit)? = null,
) {
    val settings by viewModel.settings.collectAsState()
    val global = settings.inputMappings
    val overrides = settings.controlOverrides
    val resolved = remember(global, overrides, level) { Controls.mappings(level, global, overrides) }
    val capture = viewModel.controlCapture
    LazyColumn(modifier = modifier.fillMaxSize(), contentPadding = listPadding(inMenu), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        header?.let { item { it() } }
        item { ControlsHint("${level.hint()} Select a button, then press a control or move a stick.", inMenu) }
        item {
            ControlsCard(inMenu) {
                PAD_BUTTONS.forEach { (padName, bit) ->
                    val binding = resolved[bit]
                    val consoleName = consoleNames[bit]
                    val unused = console != null && consoleNames.isNotEmpty() && consoleName == null && bit < PhobosCore.Input.LS_UP
                    if (unused && level != ControlLevel.AllConsoles) return@forEach
                    val title = consoleName ?: padName
                    ControlRow(
                        title = title,
                        overline = padName.takeIf { consoleName != null && consoleName != padName },
                        value = binding?.let(::bindingLabel) ?: "Not bound",
                        source = if (unused) "unused on $console" else sourceLabel(level, Controls.mappingSource(level, bit, global, overrides)),
                        onSelect = { capture.captureButton(bit, title) { viewModel.bindButton(level, bit, it) } },
                        onUnbind = if (binding != null) ({ viewModel.unbindButton(level, bit) }) else null,
                        onReset = if (overrides.changesMapping(level, bit)) ({ viewModel.inheritButton(level, bit) }) else null,
                    )
                }
            }
        }
        item {
            if (level == ControlLevel.AllConsoles) {
                Row(modifier = Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Button(
                        onClick = { viewModel.clearAllMappings() },
                        modifier = Modifier.weight(1f).focusRing(MaterialTheme.colorScheme.primary, pillShape()),
                        shape = pillShape(),
                        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.errorContainer, contentColor = MaterialTheme.colorScheme.onErrorContainer),
                    ) { Text("Clear All") }
                    Button(
                        onClick = { viewModel.resetDefaultMapping() },
                        modifier = Modifier.weight(1f).focusRing(MaterialTheme.colorScheme.primary, pillShape()),
                        shape = pillShape(),
                        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.secondaryContainer, contentColor = MaterialTheme.colorScheme.onSecondaryContainer),
                    ) { Text("Reset Defaults") }
                }
            } else {
                ResetLevelButton("Reset ${level.possessive()} buttons", enabled = overrides.hasMappings(level)) { viewModel.inheritAllButtons(level) }
            }
        }
    }
}

/** One level's hotkeys. */
@Composable
fun ControllerHotkeysList(
    viewModel: MainViewModel, level: ControlLevel, inMenu: Boolean,
    modifier: Modifier = Modifier, header: (@Composable () -> Unit)? = null,
) {
    val settings by viewModel.settings.collectAsState()
    val global = settings.hotkeys
    val overrides = settings.controlOverrides
    val resolved = remember(global, overrides, level) { Controls.hotkeys(level, global, overrides) }
    // What the level's button bindings hold, for the rows whose first key is also a game button.
    val gameTokens = remember(settings.inputMappings, overrides, level) {
        Controls.mappings(level, settings.inputMappings, overrides).values.flatMap(::bindingTokens).toSet()
    }
    // A button in Back's place starts the hotkeys at every level at once: one any level gives the game is asked about.
    val gameTokensEverywhere = remember(settings.inputMappings, overrides) {
        Controls.gameTokensEverywhere(settings.inputMappings, overrides)
    }
    val collisions = remember(resolved) { existingHotkeyConflicts(resolved) }
    // Controllers come and go while the page is open: the Back check is made again when they do.
    val context = LocalContext.current
    var padsChanged by remember { mutableIntStateOf(0) }
    DisposableEffect(context) {
        val inputManager = context.getSystemService(InputManager::class.java)
        val listener = object : InputManager.InputDeviceListener {
            override fun onInputDeviceAdded(deviceId: Int) { padsChanged++ }
            override fun onInputDeviceRemoved(deviceId: Int) { padsChanged++ }
            override fun onInputDeviceChanged(deviceId: Int) { padsChanged++ }
        }
        inputManager.registerInputDeviceListener(listener, null)
        onDispose { inputManager.unregisterInputDeviceListener(listener) }
    }
    val backMissing = remember(resolved, padsChanged, settings.backSeenPads) {
        resolved.values.flatten().any { it.firstOrNull() == KeyEvent.KEYCODE_BACK } &&
            !connectedDeviceSentBack(settings.backSeenPads)
    }
    val capture = viewModel.controlCapture
    // The unbinding that would leave Pause with no binding, run once the question about it is answered.
    var confirmUnbindPause by remember { mutableStateOf<(() -> Unit)?>(null) }
    var showSlotHotkeys by remember { mutableStateOf(false) }
    var pendingConflict by remember { mutableStateOf<PendingHotkey?>(null) }
    // The action whose bindings are open, one at a time.
    var expanded by remember(level) { mutableStateOf<String?>(null) }
    // Bound slot rows always show; the rest wait for the toggle.
    val slotsBound = HotkeyAction.DIRECT_SLOTS.any { !resolved[it].isNullOrEmpty() }
    val actions = if (showSlotHotkeys || slotsBound) HOTKEY_ACTIONS + DIRECT_SLOT_HOTKEY_ACTIONS else HOTKEY_ACTIONS
    fun bind(action: String, slot: Int, combo: List<Int>) {
        val conflicts = hotkeyConflicts(resolved, action, combo)
        if (conflicts.isEmpty()) viewModel.bindHotkey(level, action, slot, combo)
        else pendingConflict = PendingHotkey(level, action, slot, combo, conflicts)
    }
    // One binding, or every one with no slot.
    fun unbind(action: String, slot: Int?) {
        val run: () -> Unit = { if (slot == null) viewModel.unbindHotkey(level, action) else viewModel.bindHotkey(level, action, slot, emptyList()) }
        val left = if (slot == null) emptyList() else HotkeyBindings.with(resolved[action].orEmpty(), slot, emptyList())
        if (action == HotkeyAction.PAUSE && left.none { it.isNotEmpty() }) confirmUnbindPause = run else run()
    }
    // A button chosen for Back's place: one the game uses, while the question about it waits; or why one can't be.
    var pendingLead by remember { mutableStateOf<Int?>(null) }
    var leadRefused by remember { mutableStateOf<String?>(null) }
    fun chooseForBack(to: Int) {
        val back = KeyEvent.KEYCODE_BACK
        // Back itself: its press has just shown that a controller sends it, so the warning goes.
        if (to == back) return
        val blockers = Controls.replaceHotkeyLeadEverywhere(back, to, global, overrides).blockers
        when {
            blockers.isNotEmpty() -> {
                val names = blockers.map { (at, action) -> hotkeyName(action) + at.inParentheses() }.distinct()
                leadRefused = "${hotkeyTokenLabel(to)} can't take Back's place: ${andList(names)} " +
                    "${if (names.size > 1) "use" else "uses"} it already. Choose another button (on Xbox and PlayStation " +
                    "pads, Guide or PS), or bind ${if (names.size > 1) "those" else "that"} again first."
            }
            to in gameTokensEverywhere -> pendingLead = to
            else -> viewModel.replaceHotkeyLeadEverywhere(back, to)
        }
    }
    LazyColumn(modifier = modifier.fillMaxSize(), contentPadding = listPadding(inMenu), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        header?.let { item { it() } }
        item {
            ControlsHint(
                "${level.hint()} Select a hotkey for its bindings (up to three, any of which runs it), then a binding, " +
                    "and hold its keys together. A binding's first key starts it and is held back from the game; a key " +
                    "alone that starts other hotkeys acts when tapped.",
                inMenu,
            )
        }
        if (backMissing) {
            item {
                ControlsWarning(
                    "No controller here has sent Back yet, and Back starts these hotkeys: press it once to show it works, " +
                        "or, with no Back button (most controllers have none), choose another button to start every hotkey " +
                        "in its place, on every console and game.",
                    inMenu,
                )
            }
            item {
                TextButton(onClick = { capture.captureLead("Back") { chooseForBack(it) } }) { Text("Choose another button") }
            }
        }
        if (collisions.isNotEmpty()) {
            item {
                val pairs = collisions.joinToString("; ") { (a, b) -> "${hotkeyName(a)} and ${hotkeyName(b)}" }
                ControlsWarning("These hotkeys collide: $pairs. Bind one of each pair again.", inMenu)
            }
        }
        if (!slotsBound) {
            item {
                TextButton(onClick = { showSlotHotkeys = !showSlotHotkeys }) {
                    Text(if (showSlotHotkeys) "Hide direct slot hotkeys" else "Show direct slot hotkeys (1–9)")
                }
            }
        }
        item {
            ControlsCard(inMenu) {
                actions.forEach { (name, action) ->
                    val bindings = resolved[action].orEmpty()
                    val bound = bindings.filter { it.isNotEmpty() }
                    ControlRow(
                        title = name,
                        overline = null,
                        value = if (bound.isEmpty()) "Not bound" else bound.joinToString(" or ", transform = ::comboLabel),
                        source = listOfNotNull(
                            sourceLabel(level, Controls.hotkeySource(level, action, global, overrides)),
                            "a binding's first key is also a game button".takeIf { bound.any { it.first() in gameTokens } },
                        ).joinToString(" · ").ifEmpty { null },
                        onSelect = { expanded = action.takeIf { it != expanded } },
                        onUnbind = if (bound.isEmpty()) null else ({ unbind(action, null) }),
                        onReset = if (overrides.changesHotkey(level, action)) ({ viewModel.inheritHotkey(level, action) }) else null,
                    )
                    if (expanded == action) {
                        repeat(HotkeyBindings.LIMIT) { slot ->
                            val combo = bindings.getOrElse(slot) { emptyList() }
                            Box(Modifier.padding(start = 24.dp)) {
                                ControlRow(
                                    title = "Binding ${slot + 1}",
                                    overline = null,
                                    value = if (combo.isEmpty()) "Not bound" else comboLabel(combo),
                                    source = "its first key is also a game button".takeIf { combo.firstOrNull()?.let { it in gameTokens } == true },
                                    onSelect = { capture.captureHotkey(action, "$name, binding ${slot + 1}") { bind(action, slot, it) } },
                                    onUnbind = if (combo.isEmpty()) null else ({ unbind(action, slot) }),
                                    onReset = null,
                                )
                            }
                        }
                    }
                }
            }
        }
        if (level == ControlLevel.AllConsoles) {
            item { ResetLevelButton("Reset hotkeys to the defaults", enabled = global != DEFAULT_HOTKEYS) { viewModel.resetHotkeysToDefaults() } }
        } else {
            item { ResetLevelButton("Reset ${level.possessive()} hotkeys", enabled = overrides.hasHotkeys(level)) { viewModel.inheritAllHotkeys(level) } }
        }
    }
    confirmUnbindPause?.let { unbindPause ->
        PhobosAlertDialog(
            onDismissRequest = { confirmUnbindPause = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = inMenu); Text("Unbind Pause?") },
            text = { Text("Without a Pause hotkey, the screen is the only way into the pause menu: tap the game, then the menu button.") },
            confirmButton = {
                TextButton(onClick = { confirmUnbindPause = null; unbindPause() }) { Text("Unbind") }
            },
            dismissButton = { TextButton(onClick = { confirmUnbindPause = null }) { Text("Cancel") } },
        )
    }
    pendingConflict?.let { pending ->
        PhobosAlertDialog(
            onDismissRequest = { pendingConflict = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = inMenu); Text("Hotkey already bound") },
            text = { Text(hotkeyConflictMessage(pending.conflicts, ::hotkeyName)) },
            confirmButton = {
                TextButton(onClick = {
                    pendingConflict = null
                    viewModel.bindHotkey(pending.level, pending.action, pending.slot, pending.combo, pending.conflicts.map { it.action to it.slot })
                }) { Text("Overwrite") }
            },
            dismissButton = { TextButton(onClick = { pendingConflict = null }) { Text("Cancel") } },
        )
    }
    pendingLead?.let { to ->
        val key = hotkeyTokenLabel(to)
        PhobosAlertDialog(
            onDismissRequest = { pendingLead = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = inMenu); Text("Use $key in Back's place?") },
            text = { Text("$key is also a game button: once it starts the hotkeys, the game no longer gets it.") },
            confirmButton = {
                TextButton(onClick = {
                    pendingLead = null
                    viewModel.replaceHotkeyLeadEverywhere(KeyEvent.KEYCODE_BACK, to)
                }) { Text("Use it") }
            },
            dismissButton = { TextButton(onClick = { pendingLead = null }) { Text("Cancel") } },
        )
    }
    leadRefused?.let { message ->
        PhobosAlertDialog(
            onDismissRequest = { leadRefused = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = inMenu); Text("Back not replaced") },
            text = { Text(message) },
            confirmButton = { TextButton(onClick = { leadRefused = null }) { Text("OK") } },
        )
    }
}

private data class PendingHotkey(
    val level: ControlLevel,
    val action: String,
    val slot: Int,
    val combo: List<Int>,
    val conflicts: List<HotkeyConflict>,
)

/** An action's name as the Hotkeys page shows it, the direct slots' included. */
fun hotkeyName(action: String): String =
    (HOTKEY_ACTIONS + DIRECT_SLOT_HOTKEY_ACTIONS).firstOrNull { it.second == action }?.first ?: action

/** [backSeen] for the devices connected now. */
private fun connectedDeviceSentBack(seen: Set<String>): Boolean {
    val devices = InputDevice.getDeviceIds().toList().mapNotNull { InputDevice.getDevice(it) }
        .map { it.descriptor to ((it.sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) }
    return backSeen(devices, seen)
}

/** The level after an action's name, " (Game Boy)" or " (Tetris.gb)"; nothing for all consoles. */
private fun ControlLevel.inParentheses(): String = when (this) {
    ControlLevel.AllConsoles -> ""
    is ControlLevel.Console -> " ($system)"
    is ControlLevel.Game -> " ($rom)"
}

private fun ControlLevel.possessive(): String = when (this) {
    ControlLevel.AllConsoles -> "all consoles'"
    is ControlLevel.Console -> system
    is ControlLevel.Game -> "this game's"
}

@Composable
private fun listPadding(inMenu: Boolean): PaddingValues =
    if (inMenu) PaddingValues(0.dp) else PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp + LocalDockInset.current)

@Composable
private fun ControlsHint(text: String, inMenu: Boolean) {
    val modifier = Modifier.padding(horizontal = if (inMenu) 4.dp else 16.dp, vertical = 8.dp)
    if (inMenu) Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
    else BackdropText(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
}

@Composable
private fun ControlsWarning(text: String, inMenu: Boolean) {
    val modifier = Modifier.padding(horizontal = if (inMenu) 4.dp else 16.dp, vertical = 4.dp)
    val color = MaterialTheme.colorScheme.error
    if (inMenu) Text(text, style = MaterialTheme.typography.bodyMedium, color = color, modifier = modifier)
    else BackdropText(text, style = MaterialTheme.typography.bodyMedium, color = color, modifier = modifier)
}

@Composable
private fun ControlsCard(inMenu: Boolean, content: @Composable ColumnScope.() -> Unit) {
    if (inMenu) {
        Surface(shape = MaterialTheme.shapes.large, color = MaterialTheme.colorScheme.surfaceContainerHigh, contentColor = MaterialTheme.colorScheme.onSurface) {
            Column(Modifier.fillMaxWidth().padding(8.dp), content = content)
        }
    } else {
        SettingsCard(content = content)
    }
}

@Composable
private fun ResetLevelButton(label: String, enabled: Boolean, onClick: () -> Unit) {
    FilledTonalButton(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.fillMaxWidth().padding(top = 8.dp).focusRing(MaterialTheme.colorScheme.primary, pillShape()),
        shape = pillShape(),
    ) {
        Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text(label)
    }
}

/**
 * A row: tap (or select) to capture, or to open a hotkey's bindings; long-press or the clear icon to unbind, the undo
 * icon to inherit again.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ControlRow(
    title: String, overline: String?, value: String, source: String?,
    onSelect: () -> Unit, onUnbind: (() -> Unit)?, onReset: (() -> Unit)?,
) {
    val ring = MaterialTheme.colorScheme.primary
    ListItem(
        overlineContent = overline?.let { { Text(it) } },
        headlineContent = { Text(title) },
        supportingContent = { Text(listOfNotNull(value, source).joinToString(" · ")) },
        trailingContent = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (onReset != null) {
                    IconButton(onClick = onReset, modifier = Modifier.focusRing(ring, CircleShape)) {
                        Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = "Reset $title")
                    }
                }
                if (onUnbind != null) {
                    IconButton(onClick = onUnbind, modifier = Modifier.focusRing(ring, CircleShape)) {
                        Icon(Icons.Default.Clear, contentDescription = "Unbind $title")
                    }
                }
            }
        },
        colors = transparentListItemColors(),
        modifier = Modifier.focusRing(ring).combinedClickable(onClick = onSelect, onLongClick = onUnbind),
    )
}

/**
 * Covers the page while [capture] waits for a press; a tap cancels. A capture never outlives the
 * page that started it.
 */
@Composable
fun CaptureOverlay(capture: ControlCapture) {
    DisposableEffect(capture) { onDispose { capture.cancel() } }
    val target by capture.target.collectAsState()
    val combo by capture.combo.collectAsState()
    val current = target ?: return
    val hotkey = current is ControlCapture.Target.Hotkey
    val lead = current is ControlCapture.Target.Lead
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.5f))
            .clickable(onClick = capture::cancel),
        contentAlignment = Alignment.Center,
    ) {
        Card {
            Column(modifier = Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    when {
                        hotkey -> "Capturing combo for"
                        lead -> "A button in place of"
                        else -> "Binding"
                    },
                    style = MaterialTheme.typography.labelLarge,
                )
                Text(current.label, style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
                Spacer(Modifier.height(16.dp))
                if (hotkey) {
                    Text(
                        if (combo.isEmpty()) "Waiting for input…" else comboLabel(combo),
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Spacer(Modifier.height(8.dp))
                }
                Text(
                    when {
                        hotkey -> "Press the combo's first key, then hold the rest with it. The first key is held back from the game."
                        lead -> "Press the button to start every hotkey with instead, on every console and game. Like any " +
                            "first key, it's held back from the game."
                        else -> "Press a button or move a stick"
                    },
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    "Nothing changes if you wait or tap outside",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

/** Settings' level choice: all consoles, or one console. A game's own settings are in its pause menu. */
@Composable
fun SettingsLevelPicker(level: ControlLevel, changed: (ControlLevel) -> Boolean, onLevel: (ControlLevel) -> Unit) {
    val consoles = remember { PhobosCore.enumerateSystems().sorted() }
    SettingsCard {
        SettingsDropdownItem(
            title = "Applies to",
            description = "A game's own settings are in its pause menu",
            current = level,
            options = listOf<ControlLevel>(ControlLevel.AllConsoles) + consoles.map { ControlLevel.Console(it) },
            label = { option -> option.label() + if (option != ControlLevel.AllConsoles && changed(option)) " (changed)" else "" },
            onSelect = onLevel,
        )
    }
}

/** The pause menu's Buttons or Hotkeys page, in place of the menu list. It opens on the running [game]. */
@Composable
fun ColumnScope.ControllerMenuPage(viewModel: MainViewModel, game: ControlLevel.Game, page: ControllerPage, onBack: () -> Unit) {
    var level by remember(game) { mutableStateOf<ControlLevel>(game) }
    // The WonderSwan's names follow its orientation.
    val vertical = viewModel.settings.collectAsState().value.orientationVertical
    val consoleNames by produceState(emptyMap<Int, String>(), game, vertical) { value = viewModel.consoleButtonNames() }
    Row(modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
        Text(page.title, style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.width(16.dp))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
            Row(modifier = Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(game, ControlLevel.Console(game.system), ControlLevel.AllConsoles).forEach { option ->
                    FilterChip(
                        selected = option == level,
                        // A capture binds at the level it started on, so another level ends it.
                        onClick = { viewModel.controlCapture.cancel(); level = option },
                        label = { Text(option.label()) },
                        modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary, MaterialTheme.shapes.small),
                    )
                }
            }
        }
    }
    Box(Modifier.weight(1f)) {
        when (page) {
            ControllerPage.BUTTONS -> ControllerButtonsList(viewModel, level, consoleNames, inMenu = true, console = game.system)
            ControllerPage.HOTKEYS -> ControllerHotkeysList(viewModel, level, inMenu = true)
        }
        CaptureOverlay(viewModel.controlCapture)
    }
}
