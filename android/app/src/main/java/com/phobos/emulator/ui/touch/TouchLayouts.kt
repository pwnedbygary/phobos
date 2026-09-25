package com.phobos.emulator.ui.touch

import com.phobos.emulator.PhobosCore.Input
import com.phobos.emulator.ui.touch.Anchor.BOTTOM_CENTER
import com.phobos.emulator.ui.touch.Anchor.BOTTOM_LEFT
import com.phobos.emulator.ui.touch.Anchor.BOTTOM_RIGHT
import com.phobos.emulator.ui.touch.Anchor.TOP_CENTER
import com.phobos.emulator.ui.touch.Anchor.TOP_LEFT
import com.phobos.emulator.ui.touch.Anchor.TOP_RIGHT
import com.phobos.emulator.ui.touch.ButtonShape.PILL
import com.phobos.emulator.ui.touch.ButtonShape.SHOULDER_LEFT
import com.phobos.emulator.ui.touch.ButtonShape.SHOULDER_RIGHT

/**
 * Controller families that share one touch layout (and one saved customization), e.g. Game Boy
 * and Game Boy Color, or Mega Drive and Mega CD.
 */
enum class TouchFamily(val key: String, val displayName: String) {
    N64("n64", "Nintendo 64"),
    PS1("ps1", "PlayStation"),
    SNES("snes", "Super Famicom / SNES"),
    NES("nes", "Famicom / NES"),
    GB("gb", "Game Boy / Game Boy Color"),
    GBA("gba", "Game Boy Advance"),
    MEGA_DRIVE("md", "Mega Drive / Mega CD"),
    MASTER_SYSTEM("sms", "Master System"),
    SG1000("sg1000", "SG-1000"),
    GAME_GEAR("gg", "Game Gear"),
    PCE("pce", "PC Engine / CD / SuperGrafx"),
    NEO_GEO("neogeo", "Neo Geo / Neo Geo CD"),
    NGP("ngp", "Neo Geo Pocket / Color"),
    WONDERSWAN("ws", "WonderSwan / Color"),
    ATARI_2600("a26", "Atari 2600"),
    COLECOVISION("cv", "ColecoVision"),
    MSX("msx", "MSX / MSX2"),
    ZX("zx", "ZX Spectrum"),
    GENERIC("generic", "Other systems");

    companion object {
        /** Family for a library system name (as passed to the emulator screen). */
        fun of(system: String): TouchFamily = when (system) {
            "Nintendo 64" -> N64
            "PlayStation" -> PS1
            "Super Famicom" -> SNES
            "Famicom" -> NES
            "Game Boy", "Game Boy Color" -> GB
            "Game Boy Advance" -> GBA
            "Mega Drive", "Mega CD" -> MEGA_DRIVE
            "Master System" -> MASTER_SYSTEM
            "SG-1000" -> SG1000
            "Game Gear" -> GAME_GEAR
            "PC Engine", "PC Engine CD", "SuperGrafx" -> PCE
            "Neo Geo", "Neo Geo CD" -> NEO_GEO
            "Neo Geo Pocket", "Neo Geo Pocket Color" -> NGP
            "WonderSwan", "WonderSwan Color" -> WONDERSWAN
            "Atari 2600" -> ATARI_2600
            "ColecoVision" -> COLECOVISION
            "MSX", "MSX2" -> MSX
            "ZX Spectrum", "ZX Spectrum 128" -> ZX
            else -> GENERIC
        }
    }
}

/** Settings key suffix for one family/orientation layout customization. */
fun touchLayoutKey(family: TouchFamily, landscape: Boolean): String =
    "${family.key}_${if (landscape) "land" else "port"}"

/**
 * Default on-screen layouts. Positions are dp offsets from a screen anchor (see [Placement]);
 * every element can be moved, resized or hidden per family and orientation in the layout editor.
 *
 * Button bits follow the native mapping in PhobosRunner.cpp `resolveButtonBit()`, for example:
 * PlayStation Cross = A, Circle = B, Square = X, Triangle = Y; N64 Z = L2 and the C buttons are
 * the right-stick bits; Mega Drive C = R1 and Z = R2; Neo Geo A/B/C/D = X/Y/A/B; Atari 2600
 * Game Reset = Start; Master System Pause and Neo Geo Pocket Option = Start.
 */
object TouchLayouts {
    data class Options(
        val showMenu: Boolean = true,
        val showFastForward: Boolean = false,
        /** PlayStation: DualShock analog mode is on, so the sticks are offered. */
        val ps1Analog: Boolean = true,
        /** WonderSwan: the game is played vertically (Y pad steers, X pad acts as buttons). */
        val wonderSwanVertical: Boolean = false,
    )

    fun forSystem(system: String, options: Options = Options()): TouchLayout =
        forFamily(TouchFamily.of(system), options)

    fun forFamily(family: TouchFamily, options: Options = Options()): TouchLayout =
        TouchLayout(family, controls(family, options) + systemButtons(family, options))

    private fun controls(family: TouchFamily, options: Options): List<TouchElement> = when (family) {
        TouchFamily.N64 -> n64()
        TouchFamily.PS1 -> playStation(options.ps1Analog)
        TouchFamily.SNES -> listOf(
            dpad(),
            face(
                diamond(
                    top = TouchButton("X", Input.X, accent = TouchPalette.BLUE),
                    right = TouchButton("A", Input.A, accent = TouchPalette.RED),
                    bottom = TouchButton("B", Input.B, accent = TouchPalette.YELLOW),
                    left = TouchButton("Y", Input.Y, accent = TouchPalette.GREEN),
                )
            ),
            shoulder("l", "L", Input.L1, L1_LAND, L1_PORT, left = true),
            shoulder("r", "R", Input.R1, R1_LAND, R1_PORT, left = false),
            systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        )
        TouchFamily.NES -> listOf(
            dpad(),
            face(pair(TouchButton("B", Input.B, accent = TouchPalette.RED), TouchButton("A", Input.A, accent = TouchPalette.RED))),
            systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        )
        TouchFamily.GB -> listOf(
            dpad(),
            face(diagonalPair(TouchButton("B", Input.B, accent = TouchPalette.GB_AB), TouchButton("A", Input.A, accent = TouchPalette.GB_AB))),
            systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        )
        TouchFamily.GBA -> listOf(
            dpad(),
            face(diagonalPair(TouchButton("B", Input.B, accent = TouchPalette.GBA_AB), TouchButton("A", Input.A, accent = TouchPalette.GBA_AB))),
            shoulder("l", "L", Input.L1, L1_LAND, L1_PORT, left = true),
            shoulder("r", "R", Input.R1, R1_LAND, R1_PORT, left = false),
            systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        )
        TouchFamily.MEGA_DRIVE -> megaDrive()
        TouchFamily.MASTER_SYSTEM -> listOf(
            dpad(),
            face(pair(TouchButton("1", Input.A), TouchButton("2", Input.B))),
            systemPills("PAUSE" to Input.START),
        )
        TouchFamily.SG1000 -> listOf(
            dpad(),
            face(pair(TouchButton("1", Input.A), TouchButton("2", Input.B))),
        )
        TouchFamily.GAME_GEAR -> listOf(
            dpad(),
            face(pair(TouchButton("1", Input.A), TouchButton("2", Input.B))),
            systemPills("START" to Input.START),
        )
        TouchFamily.PCE -> listOf(
            dpad(),
            face(pair(TouchButton("II", Input.B), TouchButton("I", Input.A))),
            systemPills("SELECT" to Input.SELECT, "RUN" to Input.START),
        )
        TouchFamily.NEO_GEO -> neoGeo()
        TouchFamily.NGP -> listOf(
            dpad(),
            face(diagonalPair(TouchButton("A", Input.A), TouchButton("B", Input.B))),
            systemPills("OPTION" to Input.START),
        )
        TouchFamily.WONDERSWAN -> wonderSwan(options.wonderSwanVertical)
        TouchFamily.ATARI_2600 -> atari2600()
        TouchFamily.COLECOVISION -> colecoVision()
        TouchFamily.MSX -> msx()
        TouchFamily.ZX -> listOf(
            dpad(),
            face(listOf(TouchButton("FIRE", Input.A, w = 74f, accent = TouchPalette.RED, labelScale = 0.26f)), id = "fire", title = "Fire"),
            keyPills("keys", "Keys", "ENTER" to "ENTER", "SPACE" to "SPACE BREAK"),
        )
        TouchFamily.GENERIC -> listOf(
            dpad(),
            face(
                diamond(
                    top = TouchButton("X", Input.X),
                    right = TouchButton("A", Input.A),
                    bottom = TouchButton("B", Input.B),
                    left = TouchButton("Y", Input.Y),
                )
            ),
            shoulder("l", "L", Input.L1, L1_LAND, L1_PORT, left = true),
            shoulder("r", "R", Input.R1, R1_LAND, R1_PORT, left = false),
            systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        )
    }

    // ── Systems with bespoke arrangements ──────────────────────────────────

    private fun n64(): List<TouchElement> = listOf(
        AnalogElement(
            "stick", "Analog stick",
            Placement(BOTTOM_LEFT, 108f, -100f), Placement(BOTTOM_LEFT, 98f, -106f),
            Stick.LEFT, size = 144f, gate = StickGate.OCTAGON,
        ),
        DpadElement(land = Placement(TOP_LEFT, 64f, 128f), port = Placement(BOTTOM_LEFT, 72f, -240f), size = 96f),
        single("l", "L", Placement(TOP_LEFT, 62f, 32f), Placement(BOTTOM_LEFT, 146f, -336f),
            TouchButton("L", Input.L1, w = 100f, h = 40f, shape = SHOULDER_LEFT, labelScale = 0.55f)),
        single("z", "Z", Placement(TOP_LEFT, 160f, 32f), Placement(BOTTOM_LEFT, 50f, -336f),
            TouchButton("Z", Input.L2, w = 72f, h = 36f, shape = PILL, labelScale = 0.55f)),
        single("r", "R", Placement(TOP_RIGHT, -62f, 32f), Placement(BOTTOM_RIGHT, -60f, -336f),
            TouchButton("R", Input.R1, w = 100f, h = 40f, shape = SHOULDER_RIGHT, labelScale = 0.55f)),
        // Second Z mirrors the left pair in landscape. Four shoulder buttons don't fit
        // one row on narrow portrait screens, so it starts hidden there (left Z remains).
        ButtonCluster(
            "z_right", "Z (right)", Placement(TOP_RIGHT, -160f, 32f), Placement(BOTTOM_RIGHT, -156f, -336f),
            listOf(TouchButton("Z", Input.L2, w = 72f, h = 36f, shape = PILL, labelScale = 0.55f)),
            hiddenInPortrait = true,
        ),
        ButtonCluster(
            "c_buttons", "C buttons",
            Placement(TOP_RIGHT, -96f, 128f), Placement(BOTTOM_RIGHT, -96f, -246f),
            diamond(
                top = TouchButton("C-Up", Input.RS_UP, glyph = Glyph.ARROW_UP, accent = TouchPalette.N64_C),
                right = TouchButton("C-Right", Input.RS_RIGHT, glyph = Glyph.ARROW_RIGHT, accent = TouchPalette.N64_C),
                bottom = TouchButton("C-Down", Input.RS_DOWN, glyph = Glyph.ARROW_DOWN, accent = TouchPalette.N64_C),
                left = TouchButton("C-Left", Input.RS_LEFT, glyph = Glyph.ARROW_LEFT, accent = TouchPalette.N64_C),
                offset = 40f, size = 44f,
            ),
            multiHit = true,
        ),
        ButtonCluster(
            "ab", "A / B",
            Placement(BOTTOM_RIGHT, -108f, -86f), Placement(BOTTOM_RIGHT, -104f, -104f),
            listOf(
                TouchButton("A", Input.A, dx = 34f, dy = 14f, w = 66f, accent = TouchPalette.N64_A),
                TouchButton("B", Input.B, dx = -40f, dy = -28f, w = 56f, accent = TouchPalette.N64_B),
            ),
            multiHit = true,
        ),
        single("start", "Start", Placement(BOTTOM_CENTER, 0f, -28f), Placement(BOTTOM_CENTER, 0f, -30f),
            TouchButton("START", Input.START, w = 42f, accent = TouchPalette.N64_START, labelScale = 0.26f)),
    )

    private fun playStation(analog: Boolean): List<TouchElement> = buildList {
        add(dpad())
        add(
            face(
                diamond(
                    top = TouchButton("Triangle", Input.Y, glyph = Glyph.PS_TRIANGLE, accent = TouchPalette.PS_TRIANGLE),
                    right = TouchButton("Circle", Input.B, glyph = Glyph.PS_CIRCLE, accent = TouchPalette.PS_CIRCLE),
                    bottom = TouchButton("Cross", Input.A, glyph = Glyph.PS_CROSS, accent = TouchPalette.PS_CROSS),
                    left = TouchButton("Square", Input.X, glyph = Glyph.PS_SQUARE, accent = TouchPalette.PS_SQUARE),
                )
            )
        )
        add(shoulder("l1", "L1", Input.L1, L1_LAND, L1_PORT, left = true))
        add(shoulder("l2", "L2", Input.L2, L2_LAND, L2_PORT, left = true))
        add(shoulder("r1", "R1", Input.R1, R1_LAND, R1_PORT, left = false))
        add(shoulder("r2", "R2", Input.R2, R2_LAND, R2_PORT, left = false))
        add(systemPills("SELECT" to Input.SELECT, "START" to Input.START))
        if (analog) {
            add(
                AnalogElement(
                    "left_stick", "Left stick",
                    Placement(BOTTOM_LEFT, 262f, -86f), Placement(BOTTOM_LEFT, 110f, -64f),
                    Stick.LEFT, size = 116f, hiddenInPortrait = true,
                )
            )
            add(
                AnalogElement(
                    "right_stick", "Right stick",
                    Placement(BOTTOM_RIGHT, -262f, -86f), Placement(BOTTOM_RIGHT, -110f, -64f),
                    Stick.RIGHT, size = 116f, hiddenInPortrait = true,
                )
            )
            add(
                single("l3", "L3", Placement(BOTTOM_LEFT, 262f, -178f), Placement(BOTTOM_LEFT, 110f, -150f),
                    TouchButton("L3", Input.L3, w = 40f, labelScale = 0.36f), hidden = true)
            )
            add(
                single("r3", "R3", Placement(BOTTOM_RIGHT, -262f, -178f), Placement(BOTTOM_RIGHT, -110f, -150f),
                    TouchButton("R3", Input.R3, w = 40f, labelScale = 0.36f), hidden = true)
            )
        }
    }

    /** Six-button Fighting Pad (3-button games ignore X/Y/Z). Bits: C = R1, Z = R2, Mode = Select. */
    private fun megaDrive(): List<TouchElement> = listOf(
        dpad(),
        ButtonCluster(
            "face", "Face buttons",
            Placement(BOTTOM_RIGHT, -112f, -106f), Placement(BOTTOM_RIGHT, -102f, -176f),
            listOf(
                TouchButton("A", Input.A, dx = -60f, dy = 34f, w = 50f),
                TouchButton("B", Input.B, dx = 0f, dy = 22f, w = 50f),
                TouchButton("C", Input.R1, dx = 60f, dy = 10f, w = 50f),
                TouchButton("X", Input.X, dx = -60f, dy = -30f, w = 50f),
                TouchButton("Y", Input.Y, dx = 0f, dy = -42f, w = 50f),
                TouchButton("Z", Input.R2, dx = 60f, dy = -54f, w = 50f),
            ),
            multiHit = true,
        ),
        systemPills("START" to Input.START),
        single("mode", "Mode", Placement(BOTTOM_CENTER, -84f, -22f), Placement(BOTTOM_CENTER, -84f, -40f),
            TouchButton("MODE", Input.SELECT, w = 64f, h = 26f, shape = PILL, labelScale = 0.5f), hidden = true),
    )

    /** A/B/C/D in a slanted 2x2 grid; combination buttons are available from the editor. */
    private fun neoGeo(): List<TouchElement> = listOf(
        dpad(),
        ButtonCluster(
            "face", "A B C D",
            FACE_LAND, FACE_PORT,
            listOf(
                TouchButton("A", Input.X, dx = -32f, dy = 30f, w = 56f, accent = TouchPalette.RED),
                TouchButton("B", Input.Y, dx = 26f, dy = 18f, w = 56f, accent = TouchPalette.YELLOW),
                TouchButton("C", Input.A, dx = -26f, dy = -30f, w = 56f, accent = TouchPalette.GREEN),
                TouchButton("D", Input.B, dx = 32f, dy = -42f, w = 56f, accent = TouchPalette.BLUE),
            ),
            multiHit = true,
        ),
        systemPills("SELECT" to Input.SELECT, "START" to Input.START),
        ButtonCluster(
            "combos", "Combination buttons",
            Placement(BOTTOM_RIGHT, -108f, -212f), Placement(BOTTOM_RIGHT, -108f, -290f),
            listOf("AB" to Input.R1, "CD" to Input.R2, "BC" to Input.L1, "ABC" to Input.L2, "BCD" to Input.R3)
                .mapIndexed { i, (label, bits) ->
                    TouchButton(label, bits, dx = -84f + i * 42f, w = 38f, labelScale = 0.34f)
                },
            hiddenByDefault = true,
        ),
    )

    /**
     * Horizontal play: the X pad is the D-pad and the Y pad a small diamond (Y1..Y4 = L1, R1, X,
     * Y bits). Vertical play: the Y pad steers (D-pad bits) and the X pad acts as face buttons.
     */
    private fun wonderSwan(vertical: Boolean): List<TouchElement> = if (!vertical) listOf(
        dpad(),
        face(diagonalPair(TouchButton("B", Input.B), TouchButton("A", Input.A))),
        ButtonCluster(
            "y_pad", "Y buttons",
            Placement(TOP_LEFT, 74f, 96f), Placement(BOTTOM_LEFT, 74f, -330f),
            diamond(
                top = TouchButton("Y1", Input.L1),
                right = TouchButton("Y2", Input.R1),
                bottom = TouchButton("Y3", Input.X),
                left = TouchButton("Y4", Input.Y),
                offset = 32f, size = 40f,
            ).map { it.copy(labelScale = 0.36f) },
        ),
        systemPills("START" to Input.START),
    ) else listOf(
        dpad(),
        face(
            diamond(
                top = TouchButton("X1", Input.X),
                right = TouchButton("X2", Input.Y),
                bottom = TouchButton("X3", Input.B),
                left = TouchButton("X4", Input.A),
            ).map { it.copy(labelScale = 0.36f) },
            id = "x_pad", title = "X buttons",
        ),
        systemPills("START" to Input.START),
    )

    /** Joystick + fire, Game Reset / Game Select switches; difficulty and TV switches in the editor. */
    private fun atari2600(): List<TouchElement> = listOf(
        dpad(),
        face(listOf(TouchButton("FIRE", Input.A, w = 76f, accent = TouchPalette.RED, labelScale = 0.26f)), id = "fire", title = "Fire"),
        systemPills("RESET" to Input.START, "SELECT" to Input.SELECT),
        ButtonCluster(
            "switches", "Console switches",
            Placement(BOTTOM_CENTER, 0f, -64f), Placement(BOTTOM_CENTER, 0f, -84f),
            listOf("L DIFF" to Input.L1, "R DIFF" to Input.R1, "TV" to Input.L2).mapIndexed { i, (label, bits) ->
                TouchButton(label, bits, dx = -72f + i * 72f, w = 64f, h = 26f, shape = PILL, labelScale = 0.5f)
            },
            hiddenByDefault = true,
        ),
    )

    /** Fire buttons (L1/R1 like the hardware mapping) plus the 12-key keypad (keyboard keys). */
    private fun colecoVision(): List<TouchElement> = listOf(
        dpad(),
        face(
            listOf(
                TouchButton("L", Input.L1, dx = -38f, dy = 12f, w = 62f, accent = TouchPalette.ORANGE),
                TouchButton("R", Input.R1, dx = 38f, dy = -12f, w = 62f, accent = TouchPalette.ORANGE),
            ),
            id = "fire", title = "Fire buttons",
        ),
        ButtonCluster(
            "keypad", "Keypad",
            Placement(TOP_RIGHT, -60f, 88f), Placement(BOTTOM_CENTER, 0f, -396f),
            listOf("1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#").mapIndexed { i, key ->
                TouchButton(key, key = key, dx = (i % 3 - 1) * 36f, dy = (i / 3 - 1.5f) * 36f, w = 32f, labelScale = 0.44f)
            },
        ),
    )

    /** Joystick triggers A/B plus the keys most games need; function keys live in the editor. */
    private fun msx(): List<TouchElement> = listOf(
        dpad(),
        face(diagonalPair(TouchButton("B", Input.B), TouchButton("A", Input.A))),
        keyPills("keys", "Keys", "SPACE" to "SPACE", "RETURN" to "RETURN"),
        ButtonCluster(
            "function_keys", "Function keys",
            Placement(TOP_CENTER, 0f, 64f), Placement(BOTTOM_CENTER, 0f, -420f),
            listOf("F1" to "F1 F6", "F2" to "F2 F7", "F3" to "F3 F8", "F4" to "F4 F9", "F5" to "F5 F10", "ESC" to "ESC")
                .mapIndexed { i, (label, key) ->
                    TouchButton(label, key = key, dx = -120f + i * 48f, w = 44f, h = 26f, shape = PILL, labelScale = 0.48f)
                },
            hiddenByDefault = true,
        ),
    )

    // ── Menu, fast-forward and keyboard toggles ────────────────────────────

    private class SystemSpots(
        val menuLand: Placement, val menuPort: Placement,
        val ffLand: Placement, val ffPort: Placement,
        val keyboardLand: Placement, val keyboardPort: Placement,
    )

    private val DEFAULT_SPOTS = SystemSpots(
        Placement(TOP_CENTER, 0f, 22f), Placement(BOTTOM_CENTER, 0f, -306f),
        Placement(TOP_CENTER, 46f, 22f), Placement(BOTTOM_CENTER, 0f, -352f),
        Placement(TOP_CENTER, -46f, 22f), Placement(BOTTOM_CENTER, 0f, -398f),
    )

    // N64 portrait uses the center column between the D-pad and the C buttons.
    private val N64_SPOTS = SystemSpots(
        Placement(TOP_CENTER, 0f, 22f), Placement(BOTTOM_CENTER, 0f, -252f),
        Placement(TOP_CENTER, 46f, 22f), Placement(BOTTOM_CENTER, 0f, -206f),
        Placement(TOP_CENTER, -46f, 22f), Placement(BOTTOM_CENTER, 0f, -160f),
    )

    // ColecoVision portrait keeps the center column for the keypad.
    private val COLECO_SPOTS = SystemSpots(
        Placement(TOP_CENTER, 0f, 22f), Placement(BOTTOM_CENTER, -24f, -40f),
        Placement(TOP_CENTER, 46f, 22f), Placement(BOTTOM_CENTER, 24f, -40f),
        Placement(TOP_CENTER, -46f, 22f), Placement(BOTTOM_CENTER, 0f, -84f),
    )

    private fun systemButtons(family: TouchFamily, options: Options): List<TouchElement> = buildList {
        val spots = when (family) {
            TouchFamily.N64 -> N64_SPOTS
            TouchFamily.COLECOVISION -> COLECO_SPOTS
            else -> DEFAULT_SPOTS
        }
        if (options.showMenu) {
            add(single("menu", "Menu", spots.menuLand, spots.menuPort,
                TouchButton("Menu", glyph = Glyph.MENU, w = 36f, action = TouchAction.MENU)))
        }
        if (options.showFastForward) {
            add(single("fast_forward", "Fast forward", spots.ffLand, spots.ffPort,
                TouchButton("Fast forward", glyph = Glyph.FAST_FORWARD, w = 36f, action = TouchAction.FAST_FORWARD)))
        }
        if (family == TouchFamily.ZX) {
            add(single("keyboard", "Keyboard", spots.keyboardLand, spots.keyboardPort,
                TouchButton("Keyboard", glyph = Glyph.KEYBOARD, w = 36f, action = TouchAction.KEYBOARD)))
        }
    }

    // ── Building blocks ────────────────────────────────────────────────────

    private val DPAD_LAND = Placement(BOTTOM_LEFT, 104f, -104f)
    private val DPAD_PORT = Placement(BOTTOM_LEFT, 100f, -170f)
    private val FACE_LAND = Placement(BOTTOM_RIGHT, -104f, -104f)
    private val FACE_PORT = Placement(BOTTOM_RIGHT, -100f, -170f)
    private val L1_LAND = Placement(TOP_LEFT, 62f, 32f)
    private val L2_LAND = Placement(TOP_LEFT, 62f, 80f)
    private val R1_LAND = Placement(TOP_RIGHT, -62f, 32f)
    private val R2_LAND = Placement(TOP_RIGHT, -62f, 80f)
    private val L1_PORT = Placement(BOTTOM_LEFT, 58f, -306f)
    private val L2_PORT = Placement(BOTTOM_LEFT, 58f, -352f)
    private val R1_PORT = Placement(BOTTOM_RIGHT, -58f, -306f)
    private val R2_PORT = Placement(BOTTOM_RIGHT, -58f, -352f)
    private val SYSTEM_LAND = Placement(BOTTOM_CENTER, 0f, -22f)
    private val SYSTEM_PORT = Placement(BOTTOM_CENTER, 0f, -40f)

    private fun dpad() = DpadElement(land = DPAD_LAND, port = DPAD_PORT)

    private fun face(
        buttons: List<TouchButton>,
        id: String = "face",
        title: String = "Face buttons",
    ) = ButtonCluster(id, title, FACE_LAND, FACE_PORT, buttons, multiHit = true)

    private fun single(
        id: String, title: String, land: Placement, port: Placement, button: TouchButton, hidden: Boolean = false,
    ) = ButtonCluster(id, title, land, port, listOf(button), hiddenByDefault = hidden)

    private fun shoulder(id: String, label: String, bits: Int, land: Placement, port: Placement, left: Boolean) =
        single(id, label, land, port,
            TouchButton(label, bits, w = 100f, h = 40f, shape = if (left) SHOULDER_LEFT else SHOULDER_RIGHT, labelScale = 0.5f))

    /** Four buttons at the compass points around the cluster center. */
    private fun diamond(
        top: TouchButton, right: TouchButton, bottom: TouchButton, left: TouchButton,
        offset: Float = 52f, size: Float = 58f,
    ): List<TouchButton> = listOf(
        top.copy(dx = 0f, dy = -offset, w = size, h = size),
        right.copy(dx = offset, dy = 0f, w = size, h = size),
        bottom.copy(dx = 0f, dy = offset, w = size, h = size),
        left.copy(dx = -offset, dy = 0f, w = size, h = size),
    )

    /** Two buttons side by side (NES B-A, PC Engine II-I, Master System 1-2). */
    private fun pair(left: TouchButton, right: TouchButton, size: Float = 60f) = listOf(
        left.copy(dx = -36f, dy = 0f, w = size, h = size),
        right.copy(dx = 36f, dy = 0f, w = size, h = size),
    )

    /** Two buttons on a diagonal, the right one higher (Game Boy B-A layout). */
    private fun diagonalPair(lower: TouchButton, upper: TouchButton, size: Float = 60f) = listOf(
        lower.copy(dx = -34f, dy = 18f, w = size, h = size),
        upper.copy(dx = 34f, dy = -18f, w = size, h = size),
    )

    /** Start/Select-style pills along the bottom center. */
    private fun systemPills(vararg buttons: Pair<String, Int>): ButtonCluster {
        val spacing = 80f
        val first = -(buttons.size - 1) * spacing / 2f
        return ButtonCluster(
            "system", buttons.joinToString(" / ") { it.first.lowercase().replaceFirstChar(Char::uppercase) },
            SYSTEM_LAND, SYSTEM_PORT,
            buttons.mapIndexed { i, (label, bits) ->
                TouchButton(label, bits, dx = first + i * spacing, w = 64f, h = 26f, shape = PILL, labelScale = 0.5f)
            },
        )
    }

    /** Keyboard keys as pills along the bottom center (label to core key name). */
    private fun keyPills(id: String, title: String, vararg keys: Pair<String, String>): ButtonCluster {
        val spacing = 100f
        val first = -(keys.size - 1) * spacing / 2f
        return ButtonCluster(
            id, title, SYSTEM_LAND, SYSTEM_PORT,
            keys.mapIndexed { i, (label, key) ->
                TouchButton(label, key = key, dx = first + i * spacing, w = 88f, h = 28f, shape = PILL, labelScale = 0.48f)
            },
        )
    }
}
