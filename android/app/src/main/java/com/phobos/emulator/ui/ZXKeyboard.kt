package com.phobos.emulator.ui

import android.content.res.Configuration
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.KeyboardHide
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.util.ZX_SCHEMES
import com.phobos.emulator.util.ZxTape
import com.phobos.emulator.util.zxScheme
import kotlinx.coroutines.delay

// On-screen ZX Spectrum 48K keyboard, laid out and labelled like the real one
// (torinak.com/qaop/keyboard): four rows of ten keys, each with its letter, its red
// SYMBOL SHIFT symbol and its BASIC keyword (on the number keys, what CAPS SHIFT does).
// Keys map to the core's Keyboard matrix labels via setKeyboardKey(). Each press HOLDS
// ~60ms so the core's per-frame poll (50 Hz) sees it; visual highlight + haptic on
// every press.
//
// SHIFT behavior:
//  - SYMBOL SHIFT is LATCHING: tap to hold (keys show their symbols), tap
//    again to release (keys revert to letters). While latched, the pressed
//    key's value goes through the matrix with SYMBOL SHIFT held.
//  - CAPS SHIFT is also latching (held until tapped again); the number keys
//    then show what they do with it.

private val ROW1 = listOf("1","2","3","4","5","6","7","8","9","0")
private val ROW2 = listOf("Q","W","E","R","T","Y","U","I","O","P")
private val ROW3 = listOf("A","S","D","F","G","H","J","K","L")
private val ROW4 = listOf("Z","X","C","V","B","N","M")

private val SPACE = "SPACE BREAK"
private val ENTER = "ENTER"
private val SHIFT = "SYMBOL SHIFT"
private val CAPS = "CAPS SHIFT"

// Authentic ZX palette
private val Chassis = Color(0xFF1F1B1B)
private val KeyTop = Color(0xFF6E7176)
private val KeyBottom = Color(0xFF4B4E52)
private val KeyBorder = Color(0xFF000000)
private val KeyPressed = Color(0xFF9CD2FF)   // bright feedback on press
private val SymbolRed = Color(0xFFFF6B5E)    // the keys' red SYMBOL SHIFT legends
private val KeywordGray = Color(0xFFD0D3D7)
private val StripeUnloaded = Color(0xFF8A8C8F) // the stripe still to fill while a tape loads

// ZX Spectrum rainbow: red -> yellow -> green -> blue
private val Rainbow = Brush.horizontalGradient(
    listOf(
        Color(0xFFC3463A), // red
        Color(0xFFE2C332), // yellow
        Color(0xFF8BB060), // green
        Color(0xFF64ADD0), // blue
    )
)

// A plain face for the legends: the themes' display fonts can make C, O and 0 or B and 8 look alike.
private val LegendFont = FontFamily.SansSerif

// SYMBOL SHIFT + key, as the 48K types it.
private val ZXSymbols = mapOf(
    "1" to "!", "2" to "@", "3" to "#", "4" to "$", "5" to "%",
    "6" to "&", "7" to "'", "8" to "(", "9" to ")", "0" to "_",
    "Q" to "<=", "W" to "<>", "E" to ">=", "R" to "<", "T" to ">",
    "Y" to "AND", "U" to "OR", "I" to "AT", "O" to ";", "P" to "\"",
    "A" to "STOP", "S" to "NOT", "D" to "STEP", "F" to "TO", "G" to "THEN",
    "H" to "↑", "J" to "-", "K" to "+", "L" to "=",
    "Z" to ":", "X" to "£", "C" to "?", "V" to "/", "B" to "*",
    "N" to ",", "M" to ".",
)

// The keyword each letter types at the start of a line, and what CAPS SHIFT does with each number.
private val ZXKeywords = mapOf(
    "1" to "EDIT", "2" to "CAPS LOCK", "3" to "TRUE VID", "4" to "INV VID", "5" to "←",
    "6" to "↓", "7" to "↑", "8" to "→", "9" to "GRAPH", "0" to "DELETE",
    "Q" to "PLOT", "W" to "DRAW", "E" to "REM", "R" to "RUN", "T" to "RAND",
    "Y" to "RETURN", "U" to "IF", "I" to "INPUT", "O" to "POKE", "P" to "PRINT",
    "A" to "NEW", "S" to "SAVE", "D" to "DIM", "F" to "FOR", "G" to "GO TO",
    "H" to "GO SUB", "J" to "LOAD", "K" to "LIST", "L" to "LET",
    "Z" to "COPY", "X" to "CLEAR", "C" to "CONT", "V" to "CLS", "B" to "BORDER",
    "N" to "NEXT", "M" to "PAUSE", SPACE to "BREAK",
)

// What every key on the keyboard shares: its size, the shift latches and the CUSTOM rebinding.
private class KeyboardState(
    val keyHeight: Dp,
    val symLatched: Boolean,
    val capsLatched: Boolean,
    val rebindTarget: String?,
    val boundKeys: Set<String>,
    val onLongPressRebind: ((String) -> Unit)?,
)

@Composable
fun ZXKeyboardOverlay(
    modifier: Modifier = Modifier,
    // State is HOISTED to EmulatorScreen so it persists when the keyboard is
    // hidden/shown (remember{} inside this composable resets on hide).
    symLatched: Boolean,
    onSymLatched: (Boolean) -> Unit,
    capsLatched: Boolean,
    onCapsLatched: (Boolean) -> Unit,
    // How fast the game runs while a tape plays (the tape keeps its real speed).
    loadSpeed: Int,
    onLoadSpeed: (Int) -> Unit,
    controlScheme: Int,
    onControlScheme: (Int) -> Unit,
    rebindTarget: String?,
    onRebindTarget: (String?) -> Unit,
    onClose: () -> Unit,
    boundKeys: Set<String> = emptySet(),
    keyboardOpacity: Float = 1.0f,
    // The tape, shown on the stripe with its controls.
    tape: ZxTape = ZxTape(),
    onTapePlaying: (Boolean) -> Unit = {},
    onTapeRewind: () -> Unit = {},
    onTapeLoad: () -> Unit = {},
) {
    // CUSTOM rebinding: the next controller button is captured by EmulatorScreen's key handler,
    // which sees keys before this overlay could.

    // Rebinding is only available in CUSTOM mode (scheme 4). Long-press a key
    // to start binding it to a gamepad control.
    val rebindEnabled = controlScheme == 4
    val longPressRebind: ((String) -> Unit)? = if (rebindEnabled) { label ->
        onRebindTarget(label)
    } else null
    // Landscape leaves the game little height above the keyboard, so the keys are shorter there.
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val state = KeyboardState(
        keyHeight = if (landscape) 38.dp else 48.dp,
        symLatched = symLatched,
        capsLatched = capsLatched,
        rebindTarget = rebindTarget,
        boundKeys = boundKeys,
        onLongPressRebind = longPressRebind,
    )
    val macroHeight = if (landscape) 32.dp else 40.dp
    val clearShifts = {
        PhobosCore.setKeyboardKey(SHIFT, false)
        PhobosCore.setKeyboardKey(CAPS, false)
        onSymLatched(false)
        onCapsLatched(false)
    }

    CompositionLocalProvider(LocalTextStyle provides TextStyle(fontFamily = LegendFont)) {
        Column(
            modifier = modifier
                .fillMaxWidth()
                .graphicsLayer { alpha = keyboardOpacity }
                .background(Chassis)
                .padding(bottom = 6.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            // Rainbow stripe, wordmark, the tape's controls and the button that puts the keyboard away.
            // While a tape plays or stands part way through, the stripe is its progress bar: the rainbow
            // fills in from the left as the tape loads.
            val tapeShown = tape.playing || tape.paused
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(26.dp)
                    .drawBehind {
                        if (!tapeShown) {
                            drawRect(Rainbow)
                        } else {
                            val loaded = size.width * tape.progress
                            drawRect(StripeUnloaded)
                            clipRect(right = loaded) { drawRect(Rainbow) }
                            // The tape's read position.
                            val edge = 2.dp.toPx()
                            drawRect(Color.White.copy(alpha = 0.85f), topLeft = Offset(loaded - edge, 0f), size = Size(edge, size.height))
                        }
                    },
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "ZX Spectrum",
                    color = Color.Black,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Black,
                    fontStyle = FontStyle.Italic,
                    letterSpacing = 1.sp,
                    modifier = Modifier.padding(start = 14.dp).weight(1f)
                )
                if (tapeShown) {
                    Text(
                        text = "${if (tape.playing) "Loading" else "Stopped"} ${(tape.progress * 100).toInt()}%",
                        color = Color.Black,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                }
                if (tape.inserted) {
                    StripeButton(Icons.Default.SkipPrevious, "Rewind the tape", onClick = onTapeRewind)
                    StripeButton(
                        icon = if (tape.playing) Icons.Default.Pause else Icons.Default.PlayArrow,
                        description = if (tape.playing) "Stop the tape" else "Play the tape",
                        onClick = { onTapePlaying(!tape.playing) },
                    )
                }
                StripeButton(Icons.Default.KeyboardHide, "Hide the keyboard", width = 56.dp, onClick = onClose)
            }

            // Macro row: one-tap LOAD "", DELETE and BREAK, the tape speed and the control scheme
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(5.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                MacroKey(
                    label = "LOAD \"\"",
                    weight = 2.5f,
                    height = macroHeight,
                    // Full LOAD "" = J (LOAD), then SYM+P (quote), SYM+P (quote),
                    // then ENTER. The quotes are REQUIRED — LOAD + ENTER alone
                    // re-prompts "Program:" and waits.
                    // MUST clear any latched shift first: if SYM/CAPS is stuck on,
                    // J types "-" (SYM+J) instead of LOAD.
                    steps = listOf(
                        listOf("J"),
                        listOf(SHIFT, "P"),
                        listOf(SHIFT, "P"),
                        listOf("ENTER"),
                    ),
                    // After ENTER, the ROM waits for the tape signal until it comes
                    // (or BREAK), so the tape may start a moment later.
                    onAfterSteps = onTapeLoad,
                    onClearShifts = clearShifts
                )
                // DELETE = CAPS SHIFT + 0 and BREAK = CAPS SHIFT + SPACE, as on the real keyboard.
                ChordKey(listOf(CAPS, "0"), weight = 1.5f, height = macroHeight, label = "DELETE")
                ChordKey(listOf(CAPS, SPACE), weight = 1.5f, height = macroHeight, label = "BREAK")
                // How fast tapes load: the game runs up to this many times real time while one plays.
                PickerKey(
                    label = "TAPE ${loadSpeed}x",
                    active = loadSpeed > 1,
                    weight = 1.5f,
                    height = macroHeight,
                    options = LOAD_SPEEDS.map { it to loadSpeedLabel(it) },
                    selected = loadSpeed,
                    onSelect = onLoadSpeed,
                )
                // The game's scheme, picked from a list. CUSTOM (4) uses only the per-key rebind map;
                // the presets stay pristine.
                PickerKey(
                    label = zxScheme(controlScheme).short,
                    active = controlScheme != 0,
                    weight = 2f,
                    height = macroHeight,
                    options = ZX_SCHEMES.map { it.id to it.label },
                    selected = controlScheme,
                    onSelect = onControlScheme,
                )
            }

            // The 48K's four rows of ten, offset row by row as on the real keyboard.
            KeyRow(padEnd = 0.5f) {
                ROW1.forEach { Key(it, state) }
            }
            KeyRow(padStart = 0.5f) {
                ROW2.forEach { Key(it, state) }
            }
            KeyRow(padStart = 0.25f) {
                ROW3.forEach { Key(it, state) }
                Key(ENTER, state, weight = 1.25f)
            }
            KeyRow {
                Key(CAPS, state, weight = 1.25f, latched = capsLatched, onLatchChange = { onCapsLatched(it) })
                ROW4.forEach { Key(it, state) }
                Key(SHIFT, state, latched = symLatched, onLatchChange = { onSymLatched(it) })
                Key(SPACE, state, weight = 1.25f)
            }
        }
    }
}

/** An icon on the keyboard's stripe, with a touch target the stripe's full height. */
@Composable
private fun StripeButton(icon: ImageVector, description: String, width: Dp = 44.dp, onClick: () -> Unit) {
    val currentOnClick by rememberUpdatedState(onClick)
    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .fillMaxHeight()
            .width(width)
            .pointerInput(Unit) { detectTapGestures(onTap = { currentOnClick() }) }
    ) {
        Icon(icon, contentDescription = description, tint = Color.Black, modifier = Modifier.size(20.dp))
    }
}

@Composable
private fun KeyRow(padStart: Float = 0f, padEnd: Float = 0f, keys: @Composable RowScope.() -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (padStart > 0f) Spacer(modifier = Modifier.weight(padStart))
        keys()
        if (padEnd > 0f) Spacer(modifier = Modifier.weight(padEnd))
    }
}

@Composable
private fun RowScope.ChordKey(
    keys: List<String>,
    weight: Float,
    height: Dp,
    label: String
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    val active = pressed || flash

    val shape = RoundedCornerShape(6.dp)
    val bgModifier = if (active) {
        Modifier.background(Color(0xFF3D7EDB), shape)
    } else {
        Modifier.background(Brush.verticalGradient(listOf(Color(0xFF4A6FA5), Color(0xFF2E4E7A)), 0f, 50f), shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(height)
            .graphicsLayer { scaleX = if (pressed) 0.94f else 1f; scaleY = if (pressed) 0.94f else 1f }
            .then(bgModifier)
            .border(1.5.dp, if (active) Color.White else Color(0xFF1E3A5F), shape)
            .pointerInput(keys) {
                detectTapGestures(
                    onPress = {
                        pressed = true
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        // Press all chord keys down, hold, then release together.
                        keys.forEach { PhobosCore.setKeyboardKey(it, true) }
                        tryAwaitRelease()
                        delay(60)
                        keys.forEach { PhobosCore.setKeyboardKey(it, false) }
                        pressed = false
                        flash = true
                        delay(130)
                        flash = false
                    }
                )
            }
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

// A macro-row button that opens a list to pick from; [active] lights it when the pick isn't the default.
@Composable
private fun RowScope.PickerKey(
    label: String,
    active: Boolean,
    weight: Float,
    height: Dp,
    options: List<Pair<Int, String>>,
    selected: Int,
    onSelect: (Int) -> Unit
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var choosing by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(6.dp)
    val bgMod = if (active) {
        Modifier.background(Color(0xFF3D7EDB), shape)
    } else {
        Modifier.background(Brush.verticalGradient(listOf(Color(0xFF4A6FA5), Color(0xFF2E4E7A)), 0f, 50f), shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(height)
            .graphicsLayer { scaleX = if (pressed) 0.94f else 1f; scaleY = if (pressed) 0.94f else 1f }
            .then(bgMod)
            .border(1.5.dp, if (active) Color.White else Color(0xFF1E3A5F), shape)
            .pointerInput(Unit) {
                detectTapGestures(
                    onPress = {
                        pressed = true
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        choosing = true
                        tryAwaitRelease()
                        pressed = false
                    }
                )
            }
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
        DropdownMenu(expanded = choosing, onDismissRequest = { choosing = false }) {
            options.forEach { (value, text) ->
                DropdownMenuItem(
                    text = { Text(text) },
                    onClick = { choosing = false; onSelect(value) },
                    trailingIcon = if (value == selected) ({ Icon(Icons.Default.Check, contentDescription = "In use") }) else null,
                )
            }
        }
    }
}

@Composable
private fun RowScope.MacroKey(
    label: String,
    weight: Float,
    height: Dp,
    steps: List<List<String>>,
    onAfterSteps: () -> Unit = {},
    onClearShifts: () -> Unit = {}
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    val active = pressed || flash
    // rememberUpdatedState so the pointerInput closure always calls the LATEST
    // onAfterSteps/onClearShifts (they close over hoisted state; pointerInput
    // only restarts on `label`, so without this the macro would use stale
    // values forever).
    val currentOnAfterSteps by rememberUpdatedState(onAfterSteps)
    val currentOnClearShifts by rememberUpdatedState(onClearShifts)

    val shape = RoundedCornerShape(6.dp)
    val bgModifier = if (active) {
        Modifier.background(Color(0xFF3D7EDB), shape)   // accent blue for macros
    } else {
        Modifier.background(Brush.verticalGradient(listOf(Color(0xFF4A6FA5), Color(0xFF2E4E7A)), 0f, 50f), shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(height)
            .graphicsLayer { scaleX = if (pressed) 0.94f else 1f; scaleY = if (pressed) 0.94f else 1f }
            .then(bgModifier)
            .border(1.5.dp, if (active) Color.White else Color(0xFF1E3A5F), shape)
            .pointerInput(label) {
                detectTapGestures(
                    onPress = {
                        pressed = true
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        // Clear any latched shift so a stuck SYM/CAPS doesn't
                        // turn J into "-" or other shifted chars.
                        currentOnClearShifts()
                        delay(40)
                        // Fire each step as a quick blip (press all keys in the
                        // step, short hold, release) WITHOUT waiting for
                        // finger-up — holding a key on the ZX ROM triggers
                        // key-repeat, which can type a neighbor (J held -> 'L').
                        // A 90ms gap between steps keeps chords distinct.
                        steps.forEach { stepKeys ->
                            stepKeys.forEach { PhobosCore.setKeyboardKey(it, true) }
                            delay(40)
                            stepKeys.forEach { PhobosCore.setKeyboardKey(it, false) }
                            delay(90)
                        }
                        currentOnAfterSteps()
                        tryAwaitRelease()
                        pressed = false
                        flash = true
                        delay(160)
                        flash = false
                    }
                )
            }
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
private fun RowScope.Key(
    label: String,
    state: KeyboardState,
    weight: Float = 1f,
    latched: Boolean = false,
    onLatchChange: (Boolean) -> Unit = {}
) {
    val haptic = LocalHapticFeedback.current
    // Read at gesture time: the callback turns on and off with the CUSTOM scheme.
    val currentOnLongPressRebind by rememberUpdatedState(state.onLongPressRebind)

    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    // Track whether THIS key is the active rebind target.
    val isRebindTarget = label == state.rebindTarget
    val isBound = label in state.boundKeys

    // rememberUpdatedState keeps the gesture handler reading the CURRENT
    // latched value on every tap (pointerInput keys on `label` only, so
    // without this the closure captures the initial value and shift keys
    // never toggle OFF).
    val currentLatched by rememberUpdatedState(latched)
    val currentIsRebindTarget by rememberUpdatedState(isRebindTarget)

    val isShift = label == CAPS || label == SHIFT
    // A latched shift is "active" and shown highlighted. The rebind-target key
    // pulses (active + border glow) while waiting for a gamepad control.
    val active = pressed || flash || latched || currentIsRebindTarget

    // Pulse animation for the key being rebound.
    val transition = rememberInfiniteTransition()
    val pulseAlpha by transition.animateFloat(
        initialValue = 1f, targetValue = 0.4f,
        animationSpec = infiniteRepeatable(tween(400), RepeatMode.Reverse)
    )

    val symbol = ZXSymbols[label]
    val keyword = ZXKeywords[label]
    val digit = label.length == 1 && label[0].isDigit()
    val showingSymbol = state.symLatched && symbol != null
    val showingCapsFunction = state.capsLatched && !showingSymbol && digit && keyword != null
    // The big legend is what the key types now: its symbol while SYMBOL SHIFT is latched,
    // a number's CAPS SHIFT function while CAPS SHIFT is.
    val main = when {
        label == CAPS -> "CAPS\nSHIFT"
        label == SHIFT -> "SYMBOL\nSHIFT"
        label == SPACE -> "SPACE"
        showingSymbol -> symbol!!
        showingCapsFunction -> keyword!!
        else -> label
    }
    // The small legends: the symbol (or the letter, while the symbol is the big one) in the
    // corner, the keyword along the bottom.
    val corner = if (showingSymbol) label else symbol
    val bottom = if (showingCapsFunction) null else keyword
    val mainColor = when {
        showingSymbol || label == SHIFT -> SymbolRed
        else -> Color.White
    }
    val mainSize = when {
        main.contains('\n') -> 9.sp
        main.length == 1 -> 17.sp
        main.length <= 3 -> 14.sp
        else -> 10.sp
    }

    val keyBrush = Brush.verticalGradient(
        listOf(KeyTop, KeyBottom),
        startY = 0f,
        endY = 50f
    )

    val shape = RoundedCornerShape(6.dp)
    // Rebind-target key: pulsing border glow + translucent fill.
    val bgModifier = if (currentIsRebindTarget) {
        Modifier.background(KeyPressed.copy(alpha = pulseAlpha), shape)
    } else if (active) {
        Modifier.background(KeyPressed, shape)
    } else {
        Modifier.background(keyBrush, shape)
    }

    Box(
        modifier = Modifier
            .weight(weight)
            .height(state.keyHeight)
            .graphicsLayer {
                scaleX = if (pressed) 0.92f else 1f
                scaleY = if (pressed) 0.92f else 1f
            }
            .then(bgModifier)
            .border(
                if (currentIsRebindTarget) 2.dp else 1.5.dp,
                when {
                    currentIsRebindTarget -> Color.Yellow
                    latched -> Color.White
                    else -> KeyBorder
                },
                shape
            )
            .pointerInput(label) {
                detectTapGestures(
                    onLongPress = {
                        // In CUSTOM mode, long-press starts rebinding this key.
                        currentOnLongPressRebind?.invoke(label)
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    },
                    onPress = {
                        if (isShift) {
                            // Latching shift: toggle held state.
                            val newLatched = !currentLatched
                            onLatchChange(newLatched)
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            PhobosCore.setKeyboardKey(label, newLatched)
                            pressed = true
                            tryAwaitRelease()
                            pressed = false
                            flash = true
                            delay(130)
                            flash = false
                        } else {
                            pressed = true
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            PhobosCore.setKeyboardKey(label, true)
                            tryAwaitRelease()
                            delay(60)
                            PhobosCore.setKeyboardKey(label, false)
                            pressed = false
                            flash = true
                            delay(130)
                            flash = false
                        }
                    }
                )
            }
    ) {
        Text(
            text = main,
            color = if (active && !currentIsRebindTarget) Color.Black else mainColor,
            fontSize = mainSize,
            lineHeight = mainSize,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
            maxLines = 2,
            modifier = Modifier.align(Alignment.Center).padding(bottom = if (bottom != null) 7.dp else 0.dp)
        )
        if (corner != null) {
            Text(
                text = corner,
                color = if (showingSymbol) Color.White else SymbolRed,
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
                modifier = Modifier.align(Alignment.TopEnd).padding(top = 2.dp, end = 4.dp)
            )
        }
        if (bottom != null) {
            Text(
                text = bottom,
                color = KeywordGray,
                fontSize = 7.5.sp,
                maxLines = 1,
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 2.dp)
            )
        }
        // Bound-key indicator: a small dot in the corner (CUSTOM mode).
        if (isBound) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(3.dp)
                    .size(5.dp)
                    .background(Color(0xFF4CAF50), RoundedCornerShape(3.dp))
            )
        }
    }
}
