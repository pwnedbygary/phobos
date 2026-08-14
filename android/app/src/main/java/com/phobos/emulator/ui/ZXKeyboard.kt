package com.phobos.emulator.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.PhobosCore
import kotlinx.coroutines.delay

// On-screen ZX Spectrum 48K keyboard, styled after the classic layout
// (torinak.com/qaop/keyboard). Keys map to the core's Keyboard matrix labels
// via setKeyboardKey(). Each press HOLDS ~60ms so the core's per-frame poll
// (50 Hz) sees it; visual highlight + haptic on every press.
//
// SHIFT behavior:
//  - SYMBOL SHIFT is LATCHING: tap to hold (keys show their symbols), tap
//    again to release (keys revert to letters). While latched, the pressed
//    key's value goes through the matrix with SYMBOL SHIFT held.
//  - CAPS SHIFT is also latching (held until tapped again).

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
private val KeyTop = Color(0xFF7A7D80)
private val KeyBottom = Color(0xFF575A5C)
private val KeyBorder = Color(0xFF000000)
private val KeyPressed = Color(0xFF9CD2FF)   // bright feedback on press
private val SymColor = Color(0xFF9CD2FF)     // symbol text when SYM latched

// ZX Spectrum rainbow: red -> yellow -> green -> blue
private val Rainbow = Brush.horizontalGradient(
    listOf(
        Color(0xFFC3463A), // red
        Color(0xFFE2C332), // yellow
        Color(0xFF8BB060), // green
        Color(0xFF64ADD0), // blue
    )
)

// SYMBOL SHIFT + key -> symbol (authentic ZX Spectrum 48K layout).
private val ZXSymbols = mapOf(
    "1" to "!", "2" to "\"", "3" to "#", "4" to "$", "5" to "%",
    "6" to "&", "7" to "'", "8" to "(", "9" to ")", "0" to "_",
    "Q" to "!", "W" to "?", "E" to "£", "R" to "<", "T" to ">",
    "Y" to "←", "U" to "↑", "I" to "↓", "O" to "→", "P" to "\"",
    "A" to "[", "S" to "]", "D" to "$", "F" to ";", "G" to ":",
    "H" to "=", "J" to "+", "K" to "-", "L" to "*",
    "Z" to "\\", "X" to "\"", "C" to "?", "V" to "/", "B" to "^",
    "N" to ",", "M" to ".",
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
    turboTape: Boolean,
    onTurboTape: (Boolean) -> Unit,
    controlScheme: Int,
    onControlScheme: (Int) -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(Chassis)
            .padding(bottom = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        // Rainbow stripe + ZX SPECTRUM wordmark
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(34.dp)
                .background(Rainbow),
            contentAlignment = Alignment.CenterStart
        ) {
            Text(
                text = "ZX SPECTRUM",
                color = Color.Black,
                fontSize = 17.sp,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                letterSpacing = 2.sp,
                modifier = Modifier.padding(start = 14.dp)
            )
        }

        // Macro row: one-tap LOAD "" + CLS + CLEAR
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            MacroKey(
                label = "LOAD \"\"",
                weight = 3f,
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
                // After ENTER, the ROM waits for the tape signal — start the
                // tape playback. Turbo toggle: 2x when enabled (faster loads),
                // 1x real-time when off (always safe).
                onAfterSteps = {
                    PhobosCore.setTapeSpeed(if (turboTape) 2 else 1)
                    PhobosCore.playTape()
                },
                onClearShifts = {
                    PhobosCore.setKeyboardKey(SHIFT, false)
                    PhobosCore.setKeyboardKey(CAPS, false)
                    onSymLatched(false)
                    onCapsLatched(false)
                }
            )
            MacroKey(
                label = "CLS",
                weight = 1.5f,
                // CAPS SHIFT + 1 = CLS (clear screen).
                steps = listOf(listOf(CAPS, "1")),
                onClearShifts = {
                    PhobosCore.setKeyboardKey(SHIFT, false)
                    PhobosCore.setKeyboardKey(CAPS, false)
                    onSymLatched(false)
                    onCapsLatched(false)
                }
            )
            // TURBO tape toggle: 1x (real-time) <-> 2x (faster loads).
            ToggleKey(
                label = "TURBO",
                weight = 1.5f,
                active = turboTape,
                onToggle = { onTurboTape(it) }
            )
            // SCHEME cycler: Kempston -> QAOP -> ZXZX (tap to cycle).
            SchemeKey(
                scheme = controlScheme,
                weight = 2f,
                onCycle = {
                    val next = (controlScheme + 1) % 3
                    onControlScheme(next)
                    PhobosCore.setZxControlScheme(next)
                }
            )
        }

        KeyboardRow(ROW1, labelSize = 15.sp, padStart = 0.65f, padEnd = 0.65f, symLatched = symLatched)
        KeyboardRow(ROW2, labelSize = 15.sp, padStart = 0.65f, padEnd = 0.65f, symLatched = symLatched)
        // Row 3: CAPS SHIFT + A-L + ENTER
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(5.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Key(
                label = CAPS,
                weight = 1.6f, labelSize = 11.sp,
                symLatched = symLatched,
                onSymToggle = {},
                latched = capsLatched,
                onLatchChange = { onCapsLatched(it) }
            )
            ROW3.forEach {
                Key(it, weight = 1f, labelSize = 15.sp, symLatched = symLatched, onSymToggle = {})
            }
            Key(ENTER, weight = 1.6f, labelSize = 11.sp, symLatched = symLatched, onSymToggle = {})
        }
        // Row 4: SYMBOL SHIFT + Z-M + SPACE
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(5.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Key(
                label = SHIFT,
                weight = 1.6f, labelSize = 11.sp,
                symLatched = symLatched,
                onSymToggle = { onSymLatched(it) },
                latched = symLatched,
                onLatchChange = { onSymLatched(it) }
            )
            ROW4.forEach {
                Key(it, weight = 1f, labelSize = 15.sp, symLatched = symLatched, onSymToggle = {})
            }
            Key(SPACE, weight = 2.6f, labelSize = 11.sp, symLatched = symLatched, onSymToggle = {})
            // BACK = CAPS SHIFT + 0 (authentic ZX DELETE). Chord both keys.
            ChordKey(listOf(CAPS, "0"), weight = 1.4f, label = "BACK", labelSize = 11.sp)
        }
    }
}

@Composable
private fun RowScope.ChordKey(
    keys: List<String>,
    weight: Float,
    label: String,
    labelSize: TextUnit
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    val active = pressed || flash

    val shape = RoundedCornerShape(6.dp)
    val bgModifier = if (active) {
        Modifier.background(KeyPressed, shape)
    } else {
        Modifier.background(Brush.verticalGradient(listOf(KeyTop, KeyBottom), 0f, 50f), shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(44.dp)
            .graphicsLayer { scaleX = if (pressed) 0.92f else 1f; scaleY = if (pressed) 0.92f else 1f }
            .then(bgModifier)
            .border(1.5.dp, KeyBorder, shape)
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
        Text(label, color = Color.White, fontSize = labelSize, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
private fun RowScope.ToggleKey(
    label: String,
    weight: Float,
    active: Boolean,
    onToggle: (Boolean) -> Unit
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    // rememberUpdatedState keeps the handler reading the CURRENT active value
    // (pointerInput keys on `label` only, so without this the closure captures
    // the initial value and the toggle never turns OFF).
    val currentActive by rememberUpdatedState(active)
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
            .height(40.dp)
            .graphicsLayer { scaleX = if (pressed) 0.94f else 1f; scaleY = if (pressed) 0.94f else 1f }
            .then(bgMod)
            .border(1.5.dp, if (active) Color.White else Color(0xFF1E3A5F), shape)
            .pointerInput(label) {
                detectTapGestures(
                    onPress = {
                        pressed = true
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onToggle(!currentActive)
                        tryAwaitRelease()
                        pressed = false
                    }
                )
            }
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
private fun RowScope.SchemeKey(
    scheme: Int,
    weight: Float,
    onCycle: () -> Unit
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(6.dp)
    val label = when (scheme) {
        1 -> "QAOP"
        2 -> "ZXZX"
        else -> "KEMP"
    }
    val active = scheme != 0
    val bgMod = if (active) {
        Modifier.background(Color(0xFF3D7EDB), shape)
    } else {
        Modifier.background(Brush.verticalGradient(listOf(Color(0xFF4A6FA5), Color(0xFF2E4E7A)), 0f, 50f), shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(40.dp)
            .graphicsLayer { scaleX = if (pressed) 0.94f else 1f; scaleY = if (pressed) 0.94f else 1f }
            .then(bgMod)
            .border(1.5.dp, if (active) Color.White else Color(0xFF1E3A5F), shape)
            .pointerInput(scheme) {
                detectTapGestures(
                    onPress = {
                        pressed = true
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onCycle()
                        tryAwaitRelease()
                        pressed = false
                    }
                )
            }
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
private fun RowScope.MacroKey(
    label: String,
    weight: Float,
    steps: List<List<String>>,
    onAfterSteps: () -> Unit = {},
    onClearShifts: () -> Unit = {}
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    val active = pressed || flash
    // rememberUpdatedState so the pointerInput closure always calls the LATEST
    // onAfterSteps/onClearShifts (they close over hoisted state like turboTape;
    // pointerInput only restarts on `label`, so without this the macro would
    // use stale turboTape/scheme values forever).
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
            .height(40.dp)
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
private fun KeyboardRow(
    keys: List<String>,
    labelSize: TextUnit,
    padStart: Float,
    padEnd: Float,
    symLatched: Boolean
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(5.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Edge pads replicate the real keyboard's offset key columns.
        Spacer(modifier = Modifier.weight(padStart))
        keys.forEach { Key(it, weight = 1f, labelSize = labelSize, symLatched = symLatched, onSymToggle = {}) }
        Spacer(modifier = Modifier.weight(padEnd))
    }
}

@Composable
private fun RowScope.Key(
    label: String,
    weight: Float,
    labelSize: TextUnit,
    symLatched: Boolean,
    onSymToggle: (Boolean) -> Unit,
    latched: Boolean = false,
    onLatchChange: (Boolean) -> Unit = {}
) {
    val haptic = LocalHapticFeedback.current

    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }

    // rememberUpdatedState keeps the gesture handler reading the CURRENT
    // latched/symLatched values on every tap (pointerInput keys on `label`
    // only, so without this the closure captures the initial value and shift
    // keys never toggle OFF).
    val currentLatched by rememberUpdatedState(latched)

    val isShift = label == CAPS || label == SHIFT
    val isSymbolKey = label in ZXSymbols
    // A latched shift is "active" and shown highlighted.
    val active = pressed || flash || latched

    // Show the symbol when SYM is latched; letters/caps otherwise.
    val display = when {
        label == SPACE -> "SPACE"
        label == ENTER -> "ENTER"
        label == CAPS -> "CAPS"
        label == SHIFT -> "SYM"
        isSymbolKey && symLatched -> ZXSymbols[label]!!
        else -> label
    }

    val keyBrush = Brush.verticalGradient(
        listOf(KeyTop, KeyBottom),
        startY = 0f,
        endY = 50f
    )

    val shape = RoundedCornerShape(6.dp)
    val bgModifier = if (active) {
        Modifier.background(KeyPressed, shape)
    } else {
        Modifier.background(keyBrush, shape)
    }

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .weight(weight)
            .height(44.dp)
            .graphicsLayer {
                scaleX = if (pressed) 0.92f else 1f
                scaleY = if (pressed) 0.92f else 1f
            }
            .then(bgModifier)
            .border(1.5.dp, if (latched) Color.White else KeyBorder, RoundedCornerShape(6.dp))
            .pointerInput(label) {
                detectTapGestures(
                    onPress = {
                        if (isShift) {
                            // Latching shift: toggle held state.
                            val newLatched = !currentLatched
                            if (label == SHIFT) onSymToggle(newLatched)
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
            text = display,
            color = if (isSymbolKey && symLatched) SymColor else Color.White,
            fontSize = labelSize,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
            maxLines = 1
        )
    }
}
