package com.phobos.emulator.ui

import android.content.res.Configuration
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardHide
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.Icon
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
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.util.ZxTape
import kotlinx.coroutines.delay

// On-screen MSX keyboard in the international layout, the one the bundled C-BIOS reads the keyboard
// with (its key table: 2 shifts to @, = sits after -, [ ] after P, ; ' ` after L). The core names its
// matrix keys after the Japanese layout, so each key presses the core's label for its matrix position
// via setKeyboardKey(), held long enough for the BIOS's keyboard scan to see it. The accent key left of
// the right SHIFT is left out: C-BIOS types nothing with it. SHIFT, CTRL and GRAPH latch: tap to hold,
// tap again to let go; SHIFT shows what each key types with it. Latched and held keys are let go when
// the keyboard closes.

private enum class MsxKeyKind { CHAR, FUNCTION, CONTROL, MODIFIER }

/** A key: the legend on its face, the core's matrix label it presses, and what SHIFT makes it type. */
private data class MsxKey(
    val face: String,
    val node: String,
    val shifted: String? = null,
    val weight: Float = 1f,
    val kind: MsxKeyKind = MsxKeyKind.CHAR,
)

private fun char(face: String, node: String, shifted: String? = null) = MsxKey(face, node, shifted)
private fun control(face: String, node: String, weight: Float, shifted: String? = null) =
    MsxKey(face, node, shifted, weight, MsxKeyKind.CONTROL)
private fun modifier(face: String, node: String, weight: Float) = MsxKey(face, node, null, weight, MsxKeyKind.MODIFIER)

private val MSX_FUNCTION_ROW = listOf(
    MsxKey("F1", "F1 F6", "F6", 1.4f, MsxKeyKind.FUNCTION),
    MsxKey("F2", "F2 F7", "F7", 1.4f, MsxKeyKind.FUNCTION),
    MsxKey("F3", "F3 F8", "F8", 1.4f, MsxKeyKind.FUNCTION),
    MsxKey("F4", "F4 F9", "F9", 1.4f, MsxKeyKind.FUNCTION),
    MsxKey("F5", "F5 F10", "F10", 1.4f, MsxKeyKind.FUNCTION),
    control("SELECT", "SELECT", 1.7f),
    control("STOP", "STOP", 1.7f),
    control("HOME", "CLS/HOME", 1.7f, shifted = "CLS"),
    control("INS", "INS", 1.7f),
    control("DEL", "DEL", 1.7f),
)

private val MSX_ROW1 = listOf(
    control("ESC", "ESC", 1f),
    char("1", "1 ! ぬ", "!"),
    char("2", "2 \" ふ", "@"),
    char("3", "3 # あ ぁ", "#"),
    char("4", "4 $ う ぅ", "$"),
    char("5", "5 % え ぇ", "%"),
    char("6", "6 & お ぉ", "^"),
    char("7", "7 ’ や ゃ", "&"),
    char("8", "8 ( ゆ ゅ", "*"),
    char("9", "9 ) よ ょ", "("),
    char("0", "0 わ を", ")"),
    char("-", "- = ほ", "_"),
    char("=", "^ ~ へ", "+"),
    char("\\", "¥ | ー", "|"),
    control("BS", "BS", 1.5f),
)

private val MSX_ROW2 = listOf(
    control("TAB", "TAB", 1.5f),
    char("Q", "Q た"), char("W", "W て"), char("E", "E い ぃ"), char("R", "R す"), char("T", "T か"),
    char("Y", "Y ん"), char("U", "U な"), char("I", "I に"), char("O", "O ら"), char("P", "P せ"),
    char("[", "@ ‘ \"", "{"),
    char("]", "[ { 。", "}"),
)

private val MSX_ROW3 = listOf(
    modifier("CTRL", "CTRL", 1.75f),
    char("A", "A ち"), char("S", "S と"), char("D", "D し"), char("F", "F は"), char("G", "G き"),
    char("H", "H く"), char("J", "J ま"), char("K", "K の"), char("L", "L り"),
    char(";", "; + れ", ":"),
    char("'", ": * け", "\""),
    char("`", "] } む", "~"),
    control("RETURN", "RETURN", 1.75f),
)

private val MSX_ROW4 = listOf(
    modifier("SHIFT", "SHIFT", 2.25f),
    char("Z", "Z つ っ"), char("X", "X さ"), char("C", "C そ"), char("V", "V ひ"), char("B", "B こ"),
    char("N", "N み"), char("M", "M も"),
    char(",", ", < ね `", "<"),
    char(".", ". > る 。", ">"),
    char("/", "/ ? め .", "?"),
    modifier("SHIFT", "SHIFT", 3.25f),
)

private val MSX_ROW5 = listOf(
    control("CAPS", "CAPS", 1.5f),
    modifier("GRAPH", "GRAPH", 1.5f),
    control("CODE", "かな", 1.5f),
    MsxKey("SPACE", "SPACE", weight = 5f),
    control("←", "←", 1.5f),
    control("↑", "↑", 1.5f),
    control("↓", "↓", 1.5f),
    control("→", "→", 1.5f),
)

/** Every matrix label the keyboard presses, for the host test that checks them against the core's. */
internal val MSX_KEYBOARD_NODES: Set<String> =
    listOf(MSX_FUNCTION_ROW, MSX_ROW1, MSX_ROW2, MSX_ROW3, MSX_ROW4, MSX_ROW5).flatten().map { it.node }.toSet()

// A black MSX2-style body. The function keys and the stripe take the blues of the MSX's video chip
// (TMS9918): its dark blue is MSX BASIC's screen colour, with its light blue and cyan.
private val MsxChassis = Color(0xFF16161A)
private val MsxKeyTop = Color(0xFF3F424A)
private val MsxKeyBottom = Color(0xFF2B2D33)
private val MsxControlTop = Color(0xFF5C606B)
private val MsxControlBottom = Color(0xFF464952)
private val MsxFunctionTop = Color(0xFF5455ED)
private val MsxFunctionBottom = Color(0xFF3D3EC2)
private val MsxKeyBorder = Color(0xFF000000)
private val MsxPressed = Color(0xFF42EBF5)
private val MsxShiftLegend = Color(0xFF9D9BFF)
private val MsxStripe = Brush.horizontalGradient(
    listOf(Color(0xFF5455ED), Color(0xFF7D76FC), Color(0xFF42EBF5))
)
private val MsxStripeUnloaded = Color(0xFF34364A) // the stripe still to fill while a tape loads

// A plain face for the legends: the themes' display fonts can make C, O and 0 or B and 8 look alike.
private val MsxLegendFont = FontFamily.SansSerif

@Composable
fun MSXKeyboardOverlay(
    modifier: Modifier = Modifier,
    msx2: Boolean,
    onClose: () -> Unit,
    keyboardOpacity: Float = 1.0f,
    // The tape, shown on the stripe; the MSX's motor relay plays and stops it, so it only rewinds here.
    tape: ZxTape = ZxTape(),
    // The data tape is recording what the MSX saves.
    recording: Boolean = false,
    onTapeRewind: () -> Unit = {},
) {
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    // Six rows of keys, shorter in landscape so the game keeps some height above them.
    val keyHeight = if (landscape) 32.dp else 44.dp
    val functionHeight = if (landscape) 26.dp else 36.dp
    var latched by remember { mutableStateOf(emptySet<String>()) }
    DisposableEffect(Unit) {
        onDispose { latched.forEach { PhobosCore.setKeyboardKey(it, false) } }
    }
    val shiftLatched = "SHIFT" in latched
    val toggle: (String) -> Unit = { node ->
        val holding = node !in latched
        latched = if (holding) latched + node else latched - node
        PhobosCore.setKeyboardKey(node, holding)
    }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .graphicsLayer { alpha = keyboardOpacity }
            .background(MsxChassis)
            .padding(bottom = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(3.dp)
    ) {
        // While a tape plays or stands part way through, the stripe is its progress bar: the blues fill in
        // from the left as the tape loads, as the ZX Spectrum keyboard's rainbow does.
        val tapeShown = recording || tape.playing || tape.paused
        val currentTapeRewind by rememberUpdatedState(onTapeRewind)
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(24.dp)
                .drawBehind {
                    if (!tapeShown) {
                        drawRect(MsxStripe)
                    } else {
                        val loaded = size.width * tape.progress
                        drawRect(MsxStripeUnloaded)
                        clipRect(right = loaded) { drawRect(MsxStripe) }
                        // The tape's read position.
                        val edge = 2.dp.toPx()
                        drawRect(Color.White.copy(alpha = 0.85f), topLeft = Offset(loaded - edge, 0f), size = Size(edge, size.height))
                    }
                },
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = if (msx2) "MSX2" else "MSX",
                color = Color.White,
                fontSize = 15.sp,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                letterSpacing = 2.sp,
                fontFamily = MsxLegendFont,
                modifier = Modifier.padding(start = 14.dp).weight(1f)
            )
            if (tapeShown) {
                Text(
                    text = if (recording) "Recording" else "${if (tape.playing) "Loading" else "Stopped"} ${(tape.progress * 100).toInt()}%",
                    color = Color.White,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = MsxLegendFont,
                    maxLines = 1
                )
            }
            if (tape.inserted) {
                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier
                        .fillMaxHeight()
                        .width(44.dp)
                        .pointerInput(Unit) { detectTapGestures(onTap = { currentTapeRewind() }) }
                ) {
                    Icon(Icons.Default.SkipPrevious, contentDescription = "Rewind the tape", tint = Color.White, modifier = Modifier.size(20.dp))
                }
            }
            Box(
                contentAlignment = Alignment.Center,
                modifier = Modifier
                    .fillMaxHeight()
                    .width(56.dp)
                    .pointerInput(Unit) { detectTapGestures(onTap = { onClose() }) }
            ) {
                Icon(Icons.Default.KeyboardHide, contentDescription = "Hide the keyboard", tint = Color.White, modifier = Modifier.size(20.dp))
            }
        }

        MsxKeyRow { MSX_FUNCTION_ROW.forEach { MsxKeyCap(it, functionHeight, shiftLatched, latched, toggle) } }
        MsxKeyRow { MSX_ROW1.forEach { MsxKeyCap(it, keyHeight, shiftLatched, latched, toggle) } }
        MsxKeyRow(padEnd = 2f) { MSX_ROW2.forEach { MsxKeyCap(it, keyHeight, shiftLatched, latched, toggle) } }
        MsxKeyRow { MSX_ROW3.forEach { MsxKeyCap(it, keyHeight, shiftLatched, latched, toggle) } }
        MsxKeyRow { MSX_ROW4.forEach { MsxKeyCap(it, keyHeight, shiftLatched, latched, toggle) } }
        MsxKeyRow { MSX_ROW5.forEach { MsxKeyCap(it, keyHeight, shiftLatched, latched, toggle) } }
    }
}

@Composable
private fun MsxKeyRow(padEnd: Float = 0f, keys: @Composable RowScope.() -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(3.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        keys()
        if (padEnd > 0f) Spacer(modifier = Modifier.weight(padEnd))
    }
}

@Composable
private fun RowScope.MsxKeyCap(
    key: MsxKey,
    height: Dp,
    shiftLatched: Boolean,
    latched: Set<String>,
    onToggle: (String) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    val isLatched = key.kind == MsxKeyKind.MODIFIER && key.node in latched
    val active = pressed || flash || isLatched
    val currentOnToggle by rememberUpdatedState(onToggle)

    // With SHIFT latched, the big legend is what the key types now and its plain legend moves to the corner.
    val showingShifted = shiftLatched && key.shifted != null
    val main = if (showingShifted) key.shifted!! else key.face
    val corner = if (showingShifted) key.face else key.shifted
    val mainSize = when {
        main.length == 1 -> 16.sp
        main.length <= 3 -> 12.sp
        else -> 9.sp
    }
    val (top, bottom) = when (key.kind) {
        MsxKeyKind.CHAR -> MsxKeyTop to MsxKeyBottom
        MsxKeyKind.FUNCTION -> MsxFunctionTop to MsxFunctionBottom
        MsxKeyKind.CONTROL, MsxKeyKind.MODIFIER -> MsxControlTop to MsxControlBottom
    }
    val shape = RoundedCornerShape(5.dp)
    val face = if (active) Modifier.background(MsxPressed, shape)
    else Modifier.background(Brush.verticalGradient(listOf(top, bottom), startY = 0f, endY = 50f), shape)

    Box(
        modifier = Modifier
            .weight(key.weight)
            .height(height)
            .graphicsLayer {
                scaleX = if (pressed) 0.92f else 1f
                scaleY = if (pressed) 0.92f else 1f
            }
            .then(face)
            .border(1.5.dp, if (isLatched) Color.White else MsxKeyBorder, shape)
            .pointerInput(key) {
                detectTapGestures(
                    onPress = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        pressed = true
                        if (key.kind == MsxKeyKind.MODIFIER) {
                            currentOnToggle(key.node)
                            tryAwaitRelease()
                        } else {
                            PhobosCore.setKeyboardKey(key.node, true)
                            try {
                                tryAwaitRelease()
                                delay(60)
                            } finally {
                                PhobosCore.setKeyboardKey(key.node, false)
                            }
                        }
                        pressed = false
                        flash = true
                        delay(130)
                        flash = false
                    }
                )
            }
    ) {
        Text(
            text = main,
            color = when {
                active -> Color.Black
                showingShifted -> MsxShiftLegend
                else -> Color.White
            },
            fontSize = mainSize,
            lineHeight = mainSize,
            fontWeight = FontWeight.Bold,
            fontFamily = MsxLegendFont,
            textAlign = TextAlign.Center,
            maxLines = 1,
            softWrap = false,
            modifier = Modifier.align(Alignment.Center)
        )
        if (corner != null) {
            Text(
                text = corner,
                color = if (active) Color.Black else if (showingShifted) Color.White else MsxShiftLegend,
                fontSize = 8.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = MsxLegendFont,
                maxLines = 1,
                modifier = Modifier.align(Alignment.TopEnd).padding(top = 1.dp, end = 3.dp)
            )
        }
    }
}
