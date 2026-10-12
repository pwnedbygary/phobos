package com.phobos.emulator.util

import com.phobos.emulator.PhobosCore
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * A PSP system dialog waiting for the player: a message, the on-screen keyboard, or saving and loading. The PSP core
 * asks for it in the byte form ares/psp/kernel/dialogs.cpp's dialogBytes() writes (docs/psp-core.md, part 73), and
 * the game runs on until [PspDialogAnswer] answers it.
 */
data class PspDialogRequest(
    val id: Int,
    val kind: Int,
    val language: Int,
    /** Circle confirms and cross backs out, as in Japan (the game's own choice for its dialogs). */
    val circleConfirms: Boolean,
    // A message: its text, or the number of an error shown by its number (text empty); its buttons
    // (MESSAGE_NO_BUTTONS, MESSAGE_OK, MESSAGE_YES_NO), No chosen first, whether Back answers it, and the buttons'
    // own names where the game gives them.
    val text: String,
    val errorCode: Long,
    val buttons: Int,
    val noFirst: Boolean,
    val back: Boolean,
    val okLabel: String,
    val backLabel: String,
    val fields: List<PspKeyboardField>,
    // Saving and loading: the mode (SAVE_* below), the save chosen first, whether a save was just deleted from this
    // list, and the saves to choose from.
    val mode: Int,
    val focus: Int,
    val deleted: Boolean,
    val saves: List<PspSave>,
) {
    companion object {
        const val KIND_SAVEDATA = 1
        const val KIND_MESSAGE = 2
        const val KIND_KEYBOARD = 3

        const val MESSAGE_NO_BUTTONS = 0
        const val MESSAGE_OK = 1
        const val MESSAGE_YES_NO = 2

        const val SAVE_LOAD = 2
        const val SAVE_SAVE = 3
        const val SAVE_LIST_LOAD = 4
        const val SAVE_LIST_SAVE = 5
        const val SAVE_LIST_DELETE = 6
        const val SAVE_LIST_ALL_DELETE = 7
        const val SAVE_DELETE = 10
    }
}

/** A keyboard field: what it's for, its text at first, the most UTF-16 units it takes, the PSP's input types for it. */
data class PspKeyboardField(
    val description: String,
    val text: String,
    val maxLength: Int,
    val inputType: Int,
    val lines: Int,
    val language: Int,
)

/**
 * A save to choose: [there] on the memory stick (else new data a save mode makes), [broken] if loading it would fail,
 * its PARAM.SFO's titles, its files' [size] in bytes, when it was saved ([time], seconds since 1970) and its ICON0.PNG.
 */
class PspSave(
    val name: String,
    val title: String,
    val saveTitle: String,
    val detail: String,
    val there: Boolean,
    val broken: Boolean,
    val size: Long,
    val time: Long,
    val icon: ByteArray,
)

/**
 * The answer: for a message the button (BUTTON_*), for the keyboard each field's text or [cancelled], for a save
 * dialog the save chosen (its place in [PspDialogRequest.saves]) or -1 for none (backed out of, or the notice that
 * there's nothing to load or delete closed).
 */
data class PspDialogAnswer(
    val id: Int,
    val button: Int = 0,
    val choice: Int = -1,
    val cancelled: Boolean = false,
    val texts: List<String> = emptyList(),
) {
    /** In the byte form ares/psp/kernel/dialogs.cpp's dialogAnswerFrom() reads. */
    fun encode(): ByteArray {
        val encoded = texts.map { it.encodeToByteArray() }
        val buffer = ByteBuffer.allocate(24 + encoded.sumOf { 4 + it.size }).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(1).putInt(id).putInt(button).putInt(choice).putInt(if (cancelled) 1 else 0).putInt(encoded.size)
        for (text in encoded) buffer.putInt(text.size).put(text)
        return buffer.array()
    }

    companion object {
        const val BUTTON_YES = 1
        const val BUTTON_NO = 2
        const val BUTTON_BACK = 3
    }
}

/** A request in the core's byte form; null if it isn't one (cut short, another version, a length past its end). */
fun parsePspDialog(bytes: ByteArray): PspDialogRequest? {
    val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    fun word(): Int = if (buffer.remaining() >= 4) buffer.int else throw IndexOutOfBoundsException()
    fun wide(): Long = if (buffer.remaining() >= 8) buffer.long else throw IndexOutOfBoundsException()
    fun blob(): ByteArray {
        val size = word()
        if (size < 0 || size > buffer.remaining()) throw IndexOutOfBoundsException()
        return ByteArray(size).also { buffer.get(it) }
    }
    fun text(): String = blob().toString(Charsets.UTF_8)
    fun count(): Int = word().also { if (it < 0 || it > buffer.remaining()) throw IndexOutOfBoundsException() }
    return try {
        if (word() != 1) return null
        val id = word()
        val kind = word()
        val language = word()
        val circle = word() and 1 != 0
        val text = text()
        val error = word().toLong() and 0xffff_ffffL
        val buttons = word()
        val messageFlags = word()
        val ok = text()
        val back = text()
        val fields = List(count()) {
            val description = text()
            val text = text()
            val most = (word().toLong() and 0xffff_ffffL).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            PspKeyboardField(description, text, most, word(), word(), word())
        }
        val mode = word()
        val focus = word()
        val deleted = word() and 1 != 0
        val saves = List(count()) {
            val name = text()
            val title = text()
            val saveTitle = text()
            val detail = text()
            val flags = word()
            PspSave(name, title, saveTitle, detail, flags and 1 != 0, flags and 2 != 0, wide(), wide(), blob())
        }
        if (buffer.hasRemaining()) return null
        PspDialogRequest(
            id, kind, language, circle, text, error, buttons, messageFlags and 1 != 0, messageFlags and 2 != 0,
            ok, back, fields, mode, focus, deleted, saves,
        )
    } catch (_: IndexOutOfBoundsException) {
        null
    }
}

/** The PSP's input types (psputility_osk.h) the keyboard narrows to: digits alone, or a web address. */
const val PSP_INPUT_LATIN_DIGIT = 0x1
const val PSP_INPUT_JAPANESE_DIGIT = 0x100
const val PSP_INPUT_URL = 0x80000

/** Whether a field takes digits alone (its input types are only the digits'). */
fun PspKeyboardField.digitsOnly(): Boolean =
    inputType != 0 && inputType and (PSP_INPUT_LATIN_DIGIT or PSP_INPUT_JAPANESE_DIGIT).inv() == 0

/**
 * [text] as the field takes it: characters the PSP doesn't have (past U+FFFF, which take two UTF-16 units, and
 * control characters but a field of several lines' line breaks) left out, as are all but digits for a field of
 * digits; no more than its [PspKeyboardField.maxLength].
 */
fun PspKeyboardField.accept(text: String): String {
    val kept = StringBuilder()
    for (c in text) {
        if (Character.isSurrogate(c) || (Character.isISOControl(c) && !(c == '\n' && lines > 1))) continue
        if (digitsOnly() && c !in '0'..'9') continue
        if (kept.length >= maxLength) break
        kept.append(c)
    }
    return kept.toString()
}

/** Which of the user's PSP buttons (PhobosCore.Input bits) confirm and back out, by the game's convention. */
fun PspDialogRequest.confirmBit(): Int = if (circleConfirms) PhobosCore.Input.B else PhobosCore.Input.A
fun PspDialogRequest.backBit(): Int = if (circleConfirms) PhobosCore.Input.A else PhobosCore.Input.B

/** The PSP's glyphs for the confirm and back buttons, as its dialogs show them. */
fun PspDialogRequest.confirmGlyph(): String = if (circleConfirms) "\u25CB" else "\u2715"
fun PspDialogRequest.backGlyph(): String = if (circleConfirms) "\u2715" else "\u25CB"

/** A save's size as the PSP shows it: whole KB, rounded up. */
fun pspSaveSize(bytes: Long): String = "${(bytes + 1023) / 1024} KB"

/** The heading of a save dialog by its mode. */
fun pspSaveHeading(mode: Int): String = when (mode) {
    PspDialogRequest.SAVE_LOAD, PspDialogRequest.SAVE_LIST_LOAD -> "Load"
    PspDialogRequest.SAVE_SAVE, PspDialogRequest.SAVE_LIST_SAVE -> "Save"
    else -> "Delete"
}

/**
 * What a save dialog asks about [save] before acting, as a PSP does, and whether No is chosen first; null where it
 * acts at once (choosing from a list to load, or new data to save). A broken save to load has only its notice.
 */
fun pspSaveQuestion(mode: Int, save: PspSave): Pair<String, Boolean>? = when {
    (mode == PspDialogRequest.SAVE_LOAD || mode == PspDialogRequest.SAVE_LIST_LOAD) && save.broken ->
        "The data is corrupted." to false
    mode == PspDialogRequest.SAVE_LOAD -> "Do you want to load this data?" to false
    mode == PspDialogRequest.SAVE_SAVE && !save.there -> "Do you want to save this data?" to false
    (mode == PspDialogRequest.SAVE_SAVE || mode == PspDialogRequest.SAVE_LIST_SAVE) && save.there ->
        "Do you want to overwrite this data?" to true
    mode == PspDialogRequest.SAVE_DELETE || mode == PspDialogRequest.SAVE_LIST_DELETE ||
        mode == PspDialogRequest.SAVE_LIST_ALL_DELETE -> "This save data will be deleted. Are you sure?" to true
    else -> null
}

/** Whether a save dialog has nothing to show but "There is no save data." (loading or deleting, nothing there). */
fun PspDialogRequest.nothingThere(): Boolean = when (mode) {
    PspDialogRequest.SAVE_LOAD, PspDialogRequest.SAVE_DELETE -> saves.firstOrNull()?.there != true
    PspDialogRequest.SAVE_LIST_LOAD, PspDialogRequest.SAVE_LIST_DELETE, PspDialogRequest.SAVE_LIST_ALL_DELETE ->
        saves.isEmpty()
    else -> false
}

/** Whether a save dialog shows a list to choose from (else one save and its question). */
fun PspDialogRequest.listed(): Boolean = mode in PspDialogRequest.SAVE_LIST_LOAD..PspDialogRequest.SAVE_LIST_ALL_DELETE
