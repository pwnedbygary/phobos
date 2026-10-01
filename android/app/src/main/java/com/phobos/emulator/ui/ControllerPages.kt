package com.phobos.emulator.ui

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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.ControlCapture
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.input.Controls
import com.phobos.emulator.input.HotkeyAction
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
    "Toggle PS1 Analog" to HotkeyAction.PS1_ANALOG_TOGGLE,
    "Toggle On-Screen Keyboard" to HotkeyAction.KEYBOARD,
    "Swap to Library" to HotkeyAction.LIBRARY,
)

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

/** A key as the rows show it: "A", "L1", "Start", "D-pad up". */
fun keyLabel(keyCode: Int): String {
    val name = KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")
    return when {
        name == "BUTTON_THUMBL" -> "L3"
        name == "BUTTON_THUMBR" -> "R3"
        name.startsWith("BUTTON_") -> name.removePrefix("BUTTON_").let { if (it.length > 2) it.lowercase().replaceFirstChar(Char::uppercase) else it }
        name.startsWith("DPAD_") -> "D-pad " + name.removePrefix("DPAD_").lowercase()
        else -> name
    }
}

fun comboLabel(combo: List<Int>): String = combo.joinToString(" + ", transform = ::keyLabel)

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
                        onCapture = { capture.captureButton(bit, title) { viewModel.bindButton(level, bit, it) } },
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
    val capture = viewModel.controlCapture
    var confirmUnbindPause by remember { mutableStateOf(false) }
    LazyColumn(modifier = modifier.fillMaxSize(), contentPadding = listPadding(inMenu), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        header?.let { item { it() } }
        item { ControlsHint("${level.hint()} Select a hotkey, then hold its buttons together.", inMenu) }
        item {
            ControlsCard(inMenu) {
                HOTKEY_ACTIONS.forEach { (name, action) ->
                    val combo = resolved[action].orEmpty()
                    ControlRow(
                        title = name,
                        overline = null,
                        value = if (combo.isEmpty()) "Not bound" else comboLabel(combo),
                        source = sourceLabel(level, Controls.hotkeySource(level, action, global, overrides)),
                        onCapture = { capture.captureHotkey(action, name) { viewModel.bindHotkey(level, action, it) } },
                        onUnbind = if (combo.isEmpty()) null else ({
                            if (action == HotkeyAction.PAUSE) confirmUnbindPause = true else viewModel.bindHotkey(level, action, emptyList())
                        }),
                        onReset = if (overrides.changesHotkey(level, action)) ({ viewModel.inheritHotkey(level, action) }) else null,
                    )
                }
            }
        }
        if (level != ControlLevel.AllConsoles) {
            item { ResetLevelButton("Reset ${level.possessive()} hotkeys", enabled = overrides.hasHotkeys(level)) { viewModel.inheritAllHotkeys(level) } }
        }
    }
    if (confirmUnbindPause) {
        PhobosAlertDialog(
            onDismissRequest = { confirmUnbindPause = false },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = inMenu); Text("Unbind Pause?") },
            text = { Text("Without a Pause hotkey, the screen is the only way into the pause menu: tap the game, then the menu button.") },
            confirmButton = {
                TextButton(onClick = { confirmUnbindPause = false; viewModel.bindHotkey(level, HotkeyAction.PAUSE, emptyList()) }) { Text("Unbind") }
            },
            dismissButton = { TextButton(onClick = { confirmUnbindPause = false }) { Text("Cancel") } },
        )
    }
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

/** A row: tap (or select) to capture, long-press or the clear icon to unbind, the undo icon to inherit again. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ControlRow(
    title: String, overline: String?, value: String, source: String?,
    onCapture: () -> Unit, onUnbind: (() -> Unit)?, onReset: (() -> Unit)?,
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
        modifier = Modifier.focusRing(ring).combinedClickable(onClick = onCapture, onLongClick = onUnbind),
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
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.5f))
            .clickable(onClick = capture::cancel),
        contentAlignment = Alignment.Center,
    ) {
        Card {
            Column(modifier = Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(if (hotkey) "Capturing combo for" else "Binding", style = MaterialTheme.typography.labelLarge)
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
                    if (hotkey) "Hold all the buttons together, then let go" else "Press a button or move a stick",
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
