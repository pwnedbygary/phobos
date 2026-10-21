package com.phobos.emulator.ui

import android.graphics.BitmapFactory
import android.util.Log
import android.view.KeyEvent as AndroidKeyEvent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Save
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.DialogProperties
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.ActiveControls
import com.phobos.emulator.input.FallbackKeys
import com.phobos.emulator.input.InputBindings
import com.phobos.emulator.input.KeyHandler
import com.phobos.emulator.input.isSystemBack
import com.phobos.emulator.util.PSP_INPUT_URL
import com.phobos.emulator.util.PspDialogAnswer
import com.phobos.emulator.util.PspDialogRequest
import com.phobos.emulator.util.PspKeyboardField
import com.phobos.emulator.util.PspSave
import com.phobos.emulator.util.backBit
import com.phobos.emulator.util.backGlyph
import com.phobos.emulator.util.confirmBit
import com.phobos.emulator.util.confirmGlyph
import com.phobos.emulator.util.digitsOnly
import com.phobos.emulator.util.entered
import com.phobos.emulator.util.installProgress
import com.phobos.emulator.util.keyboardAnswer
import com.phobos.emulator.util.keyboardTexts
import com.phobos.emulator.util.listed
import com.phobos.emulator.util.nothingThere
import com.phobos.emulator.util.pspSaveHeading
import com.phobos.emulator.util.pspSaveQuestion
import com.phobos.emulator.util.pspSaveSize
import com.phobos.emulator.util.typing
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/**
 * The PSP's system dialogs over the running game (util/PspDialogs.kt): a message, the keyboard, and saving and loading,
 * answered for the game by the player, and a game data install's progress, which asks nothing. Touch and controller
 * both work. A controller's D-pad moves the choice, and the PSP buttons the user mapped confirm and back out, circle or
 * cross confirming as the game asks (Japanese games often confirm with circle); Start finishes the keyboard.
 * [controls] are the game's bindings. [hotkeys] has each key first, the game's hotkeys running as over the game
 * (GameKeys.onDialogKey): the keys it takes aren't the dialog's, and its Pause opens the pause menu, which the dialog
 * waits behind.
 */
@Composable
fun PspSystemDialog(
    request: PspDialogRequest,
    controls: ActiveControls,
    fullScreen: Boolean,
    hotkeys: KeyHandler,
    onAnswer: (PspDialogAnswer) -> Unit,
) {
    val keys = PspDialogKeys(controls, hotkeys)
    when (request.kind) {
        PspDialogRequest.KIND_MESSAGE -> PspMessageDialog(request, keys, fullScreen, onAnswer)
        PspDialogRequest.KIND_KEYBOARD -> PspKeyboardDialog(request, keys, fullScreen, onAnswer)
        PspDialogRequest.KIND_SAVEDATA -> PspSaveDialog(request, keys, fullScreen, onAnswer)
        PspDialogRequest.KIND_INSTALL -> PspInstallDialog(request, keys, fullScreen)
    }
}

/** The bindings a dialog reads its keys by, and the game's hotkeys, which have its keys first. */
private class PspDialogKeys(val controls: ActiveControls, val hotkeys: KeyHandler)

/** What a key does in a PSP dialog. */
private enum class PspKey { UP, DOWN, LEFT, RIGHT, CONFIRM, BACK, START }

/**
 * A key as the PSP's buttons by the user's [bindings] (a D-pad's own keys moving too); in the keyboard ([typing]) only
 * a controller's buttons count, the D-pad and keys moving through the text.
 */
private fun pspKey(event: KeyEvent, request: PspDialogRequest, bindings: InputBindings, typing: Boolean): PspKey? {
    val code = event.nativeKeyEvent.keyCode
    if (typing && !AndroidKeyEvent.isGamepadButton(code)) return null
    val bits = bindings.bitsForKey(code)
    return when {
        bits and request.confirmBit() != 0 -> PspKey.CONFIRM
        bits and request.backBit() != 0 -> PspKey.BACK
        bits and PhobosCore.Input.START != 0 -> PspKey.START
        typing -> null
        code == AndroidKeyEvent.KEYCODE_DPAD_UP || bits and PhobosCore.Input.UP != 0 -> PspKey.UP
        code == AndroidKeyEvent.KEYCODE_DPAD_DOWN || bits and PhobosCore.Input.DOWN != 0 -> PspKey.DOWN
        code == AndroidKeyEvent.KEYCODE_DPAD_LEFT || bits and PhobosCore.Input.LEFT != 0 -> PspKey.LEFT
        code == AndroidKeyEvent.KEYCODE_DPAD_RIGHT || bits and PhobosCore.Input.RIGHT != 0 -> PspKey.RIGHT
        code == AndroidKeyEvent.KEYCODE_DPAD_CENTER || code == AndroidKeyEvent.KEYCODE_ENTER ||
            code == AndroidKeyEvent.KEYCODE_NUMPAD_ENTER -> PspKey.CONFIRM
        else -> null
    }
}

/** The focus a controller's keys need to reach a dialog, and the keys Android makes up in its window. */
private class PspDialogInput {
    val focus = FocusRequester()
    var focused by mutableStateOf(false)
    val fallbackKeys = FallbackKeys()
}

/**
 * The dialog's keys: the game's hotkeys first ([PspDialogKeys.hotkeys]), which keep the keys they take, but not a key
 * Android makes up, as at the activity's window (a fallback, or the navigation bar's or gesture's Back, which backs out
 * of the dialog); then [onKey] on each press of a PSP key (the D-pad's repeating while held; its release taken too, so
 * Android makes nothing else of it, such as Back from a controller's B). The dialog takes [input]'s focus
 * ([PspTakeFocus]); in the keyboard ([typing]) its first field taking text does.
 */
private fun Modifier.pspKeys(request: PspDialogRequest, keys: PspDialogKeys, input: PspDialogInput,
                             typing: Boolean = false, onKey: (PspKey) -> Unit): Modifier {
    val bindings = InputBindings.of(keys.controls.mappings)
    return this
        .onPreviewKeyEvent { event ->
            val native = event.nativeKeyEvent
            val code = native.keyCode
            val down = event.type == KeyEventType.KeyDown
            val pressedOrLetGo = down || event.type == KeyEventType.KeyUp
            val madeUp = input.fallbackKeys.isFallback(code, native.deviceId, native.scanCode, native.downTime, native.flags) ||
                isSystemBack(code, native.deviceId)
            if (pressedOrLetGo && !madeUp && keys.hotkeys.onKey(code, down, native.repeatCount)) {
                return@onPreviewKeyEvent true
            }
            val key = pspKey(event, request, bindings, typing) ?: return@onPreviewKeyEvent false
            val moves = key == PspKey.UP || key == PspKey.DOWN || key == PspKey.LEFT || key == PspKey.RIGHT
            if (down && (event.nativeKeyEvent.repeatCount == 0 || moves)) onKey(key)
            true
        }
        .then(if (typing) Modifier else Modifier.pspFocus(input).focusable())
}

/** Where [input]'s focus goes: what it focuses, and whether it (or something in it) has focus. */
private fun Modifier.pspFocus(input: PspDialogInput): Modifier =
    onFocusChanged { input.focused = it.hasFocus }.focusRequester(input.focus)

/**
 * Takes [input]'s focus, from inside the dialog's content: a Dialog has a window and a composition of its own, which
 * may not be there yet when the composable opening it runs its effects. Tried again each frame for up to a second, as
 * without focus a controller's keys don't reach the dialog; [then] once it's taken (the soft keyboard shown).
 */
@Composable
private fun PspTakeFocus(request: PspDialogRequest, input: PspDialogInput, then: () -> Unit = {}) {
    LaunchedEffect(request.id) {
        val start = withFrameNanos { it }
        var now = start
        while (!input.focused && now - start < 1_000_000_000L) {
            runCatching { input.focus.requestFocus() }
            now = withFrameNanos { it }
        }
        if (input.focused) then() else Log.w("Phobos", "PSP dialog ${request.id}: no focus, so no controller")
    }
}

/** A button the D-pad can choose ([selected] ringed in the theme's colour), or a tap. */
@Composable
private fun PspButton(label: String, selected: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(12.dp)
    TextButton(
        onClick = onClick,
        modifier = Modifier
            .then(if (selected) Modifier.border(3.dp, MaterialTheme.colorScheme.primary, shape) else Modifier)
            .semantics { this.selected = selected },
        shape = shape,
    ) { Text(label) }
}

/** The PSP's hints at a dialog's foot: which button confirms, which backs out. */
@Composable
private fun PspHints(request: PspDialogRequest, confirm: String?, back: String?) {
    val hints = listOfNotNull(confirm?.let { "${request.confirmGlyph()} $it" }, back?.let { "${request.backGlyph()} $it" })
    if (hints.isEmpty()) return
    Text(
        hints.joinToString("    "),
        modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * A message: its text (or an error's number), and its buttons: Yes and No, OK, or none (the game takes it down itself),
 * and Back unless the game took it away. The D-pad moves between Yes and No.
 */
@Composable
private fun PspMessageDialog(
    request: PspDialogRequest,
    keys: PspDialogKeys,
    fullScreen: Boolean,
    onAnswer: (PspDialogAnswer) -> Unit,
) {
    val choices = when (request.buttons) {
        PspDialogRequest.MESSAGE_YES_NO -> listOf("Yes" to PspDialogAnswer.BUTTON_YES, "No" to PspDialogAnswer.BUTTON_NO)
        PspDialogRequest.MESSAGE_OK -> listOf(request.okLabel.ifEmpty { "OK" } to PspDialogAnswer.BUTTON_YES)
        else -> emptyList()
    }
    var selected by remember(request.id) { mutableIntStateOf(if (request.noFirst && choices.size == 2) 1 else 0) }
    val input = remember { PspDialogInput() }
    val backLabel = request.backLabel.ifEmpty { "Back" }
    fun answer(button: Int) = onAnswer(PspDialogAnswer(request.id, button = button))
    fun back() { if (request.back) answer(PspDialogAnswer.BUTTON_BACK) }
    val text = request.text.ifEmpty { "An error has occurred.\n(Error code %08X)".format(request.errorCode) }
    PhobosAlertDialog(
        onDismissRequest = ::back,
        modifier = Modifier.pspKeys(request, keys, input) { key ->
            when (key) {
                PspKey.LEFT, PspKey.UP -> if (choices.isNotEmpty()) selected = 0
                PspKey.RIGHT, PspKey.DOWN -> if (choices.isNotEmpty()) selected = choices.size - 1
                PspKey.CONFIRM -> if (choices.isNotEmpty()) answer(choices[selected].second) else back()
                PspKey.BACK -> back()
                PspKey.START -> {}
            }
        },
        properties = DialogProperties(dismissOnBackPress = request.back, dismissOnClickOutside = false),
        text = {
            Column {
                DialogSystemBars(fullScreen, inGame = true)
                PspTakeFocus(request, input)
                Text(text, Modifier.heightIn(max = 280.dp).verticalScroll(rememberScrollState()))
                PspHints(request, if (choices.isNotEmpty()) "Enter" else null, if (request.back) backLabel else null)
            }
        },
        confirmButton = {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                choices.forEachIndexed { index, (label, button) ->
                    PspButton(label, selected == index) { answer(button) }
                }
            }
        },
        // Back as a button of its own only where it's the one answer; beside Yes and No or OK it's the back button
        // (and Android's back), as a PSP shows it only in the hints.
        dismissButton = if (request.back && choices.isEmpty()) {
            { TextButton(onClick = ::back) { Text(backLabel) } }
        } else null,
    )
}

/**
 * The keyboard: a text field for each of the game's fields (its description the title, or each one's label), with
 * Android's own keyboard, holding the game's own text at first. Each takes no more than the game allows, and what's
 * typed is entered as its input types take it (digits alone where it asks for digits, letters typed in ASCII made
 * full-width where it takes only full-width ones), while the game's own text left as it was is entered as it is: the
 * field shows what's typed, as Android's keyboard composes it, and below it what will be entered where that differs.
 * OK, Done or the confirm button (or Start) enters the text; Cancel or the back button backs out.
 */
@Composable
private fun PspKeyboardDialog(
    request: PspDialogRequest,
    keys: PspDialogKeys,
    fullScreen: Boolean,
    onAnswer: (PspDialogAnswer) -> Unit,
) {
    val fields = request.fields
    var texts by remember(request.id) { mutableStateOf(request.keyboardTexts()) }
    fun enter() = onAnswer(request.keyboardAnswer(texts))
    fun cancel() = onAnswer(PspDialogAnswer(request.id, cancelled = true))
    val input = remember { PspDialogInput() }
    val first = fields.indexOfFirst { it.maxLength > 0 }  //(the field taking the focus; with none, the dialog does)
    PhobosAlertDialog(
        onDismissRequest = ::cancel,
        modifier = Modifier.pspKeys(request, keys, input, typing = first >= 0) { key ->
            when (key) {
                PspKey.CONFIRM, PspKey.START -> enter()
                PspKey.BACK -> cancel()
                else -> {}
            }
        },
        properties = DialogProperties(dismissOnClickOutside = false),
        title = {
            DialogSystemBars(fullScreen, inGame = true)
            Text(fields.singleOrNull()?.description?.ifEmpty { null } ?: "Enter Text")
        },
        text = {
            val keyboard = LocalSoftwareKeyboardController.current  //(the dialog window's)
            PspTakeFocus(request, input) { keyboard?.show() }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                fields.forEachIndexed { index, field ->
                    PspTextField(
                        field = field,
                        value = texts[index],
                        label = if (fields.size > 1) field.description else null,
                        last = index == fields.size - 1,
                        modifier = if (index == first) Modifier.pspFocus(input) else Modifier,
                        onValue = { value -> texts = texts.toMutableList().also { it[index] = field.typing(value) } },
                        onDone = ::enter,
                    )
                }
                PspHints(request, "Enter", "Cancel")
            }
        },
        confirmButton = { TextButton(onClick = ::enter) { Text("OK") } },
        dismissButton = { TextButton(onClick = ::cancel) { Text("Cancel") } },
    )
}

@Composable
private fun PspTextField(
    field: PspKeyboardField,
    value: String,
    label: String?,
    last: Boolean,
    modifier: Modifier,
    onValue: (String) -> Unit,
    onDone: () -> Unit,
) {
    val type = when {
        field.digitsOnly() -> KeyboardType.Number
        field.inputType and PSP_INPUT_URL != 0 -> KeyboardType.Uri
        else -> KeyboardType.Text
    }
    OutlinedTextField(
        value = value,
        onValueChange = onValue,
        modifier = modifier.fillMaxWidth(),
        enabled = field.maxLength > 0,
        label = label?.let { { Text(it) } },
        singleLine = field.lines <= 1,
        maxLines = field.lines.coerceIn(1, 6),
        supportingText = {
            // (the core cuts the game's own text so, but for a pair at the cut, shown as two U+FFFD: it leaves it out)
            val entered = field.entered(value).take(field.maxLength)
            val count = "${entered.length} / ${field.maxLength}"
            Text(
                when {
                    entered == value -> count
                    entered.isEmpty() -> "(nothing entered)  $count"
                    else -> "$entered  $count"
                }
            )
        },
        keyboardOptions = KeyboardOptions(
            capitalization = KeyboardCapitalization.Sentences,
            keyboardType = type,
            imeAction = if (last) ImeAction.Done else ImeAction.Next,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
    )
}

/**
 * Saving and loading: a list of the game's saves to load, save to (new data among them) or delete, each with its icon,
 * titles, date and size; or one save and the question a PSP asks before loading, saving over or deleting it; or the
 * PSP's notices: there's no save data, the data is corrupted. Choosing from a list asks its question where a PSP does
 * (saving over a save, deleting one); No goes back to the list.
 */
@Composable
private fun PspSaveDialog(
    request: PspDialogRequest,
    keys: PspDialogKeys,
    fullScreen: Boolean,
    onAnswer: (PspDialogAnswer) -> Unit,
) {
    val saves = request.saves
    val listed = request.listed()
    var row by remember(request.id) { mutableIntStateOf(request.focus.coerceIn(0, (saves.size - 1).coerceAtLeast(0))) }
    val single = if (listed || request.nothingThere() || saves.isEmpty()) null else 0
    var asked by remember(request.id) { mutableStateOf(single) }
    var yes by remember(request.id) {
        mutableStateOf(single?.let { pspSaveQuestion(request.mode, saves[it])?.second != true } ?: true)
    }
    val input = remember { PspDialogInput() }
    val question = asked?.let { pspSaveQuestion(request.mode, saves[it]) }
    val notice = question?.first == "The data is corrupted."
    fun answer(choice: Int) = onAnswer(PspDialogAnswer(request.id, choice = choice))
    fun choose(index: Int) {
        val next = pspSaveQuestion(request.mode, saves[index])
        if (next == null) return answer(index)
        asked = index
        yes = !next.second
    }
    fun back() {
        when {
            asked != null && notice -> answer(asked!!)
            asked != null && listed -> asked = null
            else -> answer(-1)
        }
    }
    fun confirm() {
        val index = asked
        when {
            request.nothingThere() -> answer(-1)
            index != null && (notice || yes) -> answer(index)
            index != null -> back()
            saves.isNotEmpty() -> choose(row)
        }
    }
    PhobosAlertDialog(
        onDismissRequest = ::back,
        modifier = Modifier.pspKeys(request, keys, input) { key ->
            when (key) {
                PspKey.UP -> if (asked == null) row = (row - 1).coerceAtLeast(0)
                PspKey.DOWN -> if (asked == null) row = (row + 1).coerceAtMost((saves.size - 1).coerceAtLeast(0))
                PspKey.LEFT -> if (asked != null) yes = true
                PspKey.RIGHT -> if (asked != null) yes = false
                PspKey.CONFIRM -> confirm()
                PspKey.BACK -> back()
                PspKey.START -> {}
            }
        },
        properties = DialogProperties(dismissOnClickOutside = false),
        title = {
            DialogSystemBars(fullScreen, inGame = true)
            Text(pspSaveHeading(request.mode))
        },
        text = {
            Column {
                PspTakeFocus(request, input)
                if (request.deleted) {
                    Text("The save data has been deleted.", style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(bottom = 8.dp))
                }
                val index = asked
                when {
                    request.nothingThere() -> Text("There is no save data.")
                    index != null -> {
                        PspSaveCard(request, saves[index], selected = false, onClick = null)
                        Spacer(Modifier.height(12.dp))
                        Text(question?.first ?: "", style = MaterialTheme.typography.bodyLarge)
                    }
                    else -> PspSaveList(request, row) { choose(it) }
                }
                if (request.nothingThere() || notice) PspHints(request, "OK", null) else PspHints(request, "Enter", "Back")
            }
        },
        confirmButton = {
            when {
                request.nothingThere() || notice -> TextButton(onClick = ::confirm) { Text("OK") }
                asked != null -> Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PspButton("Yes", yes) { answer(asked!!) }
                    PspButton("No", !yes) { back() }
                }
                else -> {}
            }
        },
        dismissButton = if (asked == null && !request.nothingThere()) {
            { TextButton(onClick = ::back) { Text("Back") } }
        } else null,
    )
}

/**
 * A game data install's progress, where the PSP shows its own (the game's mode 1; in mode 0 the game draws its own and
 * nothing comes here): the install's icon, its titles and detail, the bytes it copies, and how far it has got. No
 * button and no back: the install can't be stopped from here, and the core takes the dialog down when it's done. The
 * hotkeys still run, the pause hotkey opening the pause menu.
 */
@Composable
private fun PspInstallDialog(request: PspDialogRequest, keys: PspDialogKeys, fullScreen: Boolean) {
    val input = remember { PspDialogInput() }
    val progress = request.installProgress()
    PhobosAlertDialog(
        onDismissRequest = {},
        modifier = Modifier.pspKeys(request, keys, input) {},
        properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
        title = {
            DialogSystemBars(fullScreen, inGame = true)
            Text("Install")
        },
        text = {
            Column {
                PspTakeFocus(request, input)
                request.saves.firstOrNull()?.let { install ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        PspSaveIcon(install)
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text(install.saveTitle.ifEmpty { install.title.ifEmpty { install.name } },
                                style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold,
                                maxLines = 1, overflow = TextOverflow.Ellipsis)
                            if (install.detail.isNotEmpty()) {
                                Text(install.detail, style = MaterialTheme.typography.bodySmall, maxLines = 2,
                                    overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                            Text(pspSaveSize(install.size), style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
                Text("Installing\u2026 $progress%", Modifier.padding(top = 12.dp))
                ProgressBar({ progress / 100f }, Modifier.fillMaxWidth().padding(top = 8.dp))
            }
        },
        confirmButton = {},
    )
}

@Composable
private fun PspSaveList(request: PspDialogRequest, row: Int, onChoose: (Int) -> Unit) {
    val state = rememberLazyListState(initialFirstVisibleItemIndex = row)
    LaunchedEffect(row) {  //(the chosen save kept in view)
        val shown = state.layoutInfo.visibleItemsInfo
        val first = shown.firstOrNull()?.index ?: return@LaunchedEffect
        val last = shown.last().index
        if (row <= first) state.animateScrollToItem(row)
        else if (row >= last) state.animateScrollToItem((row - (last - first) + 1).coerceAtLeast(0))
    }
    LazyColumn(state = state, modifier = Modifier.heightIn(max = 340.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        itemsIndexed(request.saves) { index, save ->
            PspSaveCard(request, save, selected = index == row) { onChoose(index) }
        }
    }
}

/** A save as the PSP's lists show it: its icon, its titles and detail, when it was saved and its size. */
@Composable
private fun PspSaveCard(request: PspDialogRequest, save: PspSave, selected: Boolean, onClick: (() -> Unit)?) {
    val shape = RoundedCornerShape(12.dp)
    val allGames = request.mode == PspDialogRequest.SAVE_LIST_ALL_DELETE
    val heading = when {
        !save.there -> "New Data"
        allGames -> save.title.ifEmpty { save.name }
        else -> save.saveTitle.ifEmpty { save.title.ifEmpty { save.name } }
    }
    val second = when {
        !save.there -> save.saveTitle
        allGames -> save.saveTitle
        else -> save.detail
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(shape)
            .then(if (selected) Modifier.border(3.dp, MaterialTheme.colorScheme.primary, shape) else Modifier)
            .then(if (onClick != null) Modifier.clickable(role = Role.Button, onClick = onClick) else Modifier)
            .semantics { this.selected = selected }
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PspSaveIcon(save)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(heading, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold,
                maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (second.isNotEmpty()) {
                Text(second, style = MaterialTheme.typography.bodySmall, maxLines = 2, overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (save.there) {
                Text(
                    "${pspSaveDate(save.time)}   ${pspSaveSize(save.size)}" + if (save.broken) "   (corrupted)" else "",
                    style = MaterialTheme.typography.labelSmall,
                    color = if (save.broken) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

/** A save's ICON0.PNG at the PSP's 144 by 80, or a plain one where it has none. */
@Composable
private fun PspSaveIcon(save: PspSave) {
    val image = remember(save) {
        if (save.icon.isEmpty()) null
        else BitmapFactory.decodeByteArray(save.icon, 0, save.icon.size)?.asImageBitmap()
    }
    val modifier = Modifier.size(width = 108.dp, height = 60.dp).clip(RoundedCornerShape(6.dp))
    if (image != null) {
        Image(image, contentDescription = null, modifier = modifier, contentScale = ContentScale.Fit)
    } else {
        Box(modifier.background(MaterialTheme.colorScheme.surfaceVariant), contentAlignment = Alignment.Center) {
            Icon(Icons.Outlined.Save, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** When a save was saved, in the device's own way of writing dates. */
private fun pspSaveDate(seconds: Long): String =
    DateTimeFormatter.ofLocalizedDateTime(FormatStyle.MEDIUM, FormatStyle.SHORT)
        .withZone(ZoneId.systemDefault())
        .format(Instant.ofEpochSecond(seconds))
