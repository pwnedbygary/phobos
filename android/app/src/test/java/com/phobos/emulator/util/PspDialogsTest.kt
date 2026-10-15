package com.phobos.emulator.util

import com.phobos.emulator.PhobosCore
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The PSP's system dialogs as the app reads and answers them. The byte files are the core's own: tests/psp/dialogs.cpp
 * writes the request with ares/psp/kernel/dialogs.cpp's dialogBytes() and reads the answer back with its
 * dialogAnswerFrom(), so both sides agree on the form.
 */
class PspDialogsTest {
    private fun resource(name: String): ByteArray =
        requireNotNull(javaClass.classLoader?.getResourceAsStream(name)) { "missing $name" }.use { it.readBytes() }

    private fun save(name: String = "SLOT0", there: Boolean = true, broken: Boolean = false) =
        PspSave(name, "A game", "Chapter 1", "", there, broken, 0, 0, ByteArray(0))

    private fun request(mode: Int, saves: List<PspSave>, circle: Boolean = false) = PspDialogRequest(
        1, PspDialogRequest.KIND_SAVEDATA, 1, circle, "", 0, 0, false, false, "", "", emptyList(), mode, 0, false, saves,
    )

    @Test
    fun readsTheCoresRequest() {
        val request = assertNotNullRequest(parsePspDialog(resource("psp-dialog-request.bin")))
        assertEquals(7, request.id)
        assertEquals(PspDialogRequest.KIND_SAVEDATA, request.kind)
        assertEquals(2, request.language)
        assertTrue(request.circleConfirms)
        assertEquals("Café?", request.text)
        assertEquals(0x8002_0323L, request.errorCode)
        assertEquals(PspDialogRequest.MESSAGE_YES_NO, request.buttons)
        assertTrue(request.noFirst)
        assertTrue(request.back)
        assertEquals("Hai", request.okLabel)
        assertEquals("Iya", request.backLabel)
        assertEquals(listOf(PspKeyboardField("Name", "Snake", 8, 0x0f, 1, 2), PspKeyboardField("PIN", "", 4, 1, 1, 0)),
            request.fields)
        assertEquals(PspDialogRequest.SAVE_LIST_SAVE, request.mode)
        assertEquals(1, request.focus)
        assertTrue(request.deleted)
        assertEquals(2, request.saves.size)
        val first = request.saves[0]
        assertEquals("SLOT0", first.name)
        assertEquals("A game", first.title)
        assertEquals("Chapter 3", first.saveTitle)
        assertEquals("Line one\nline two", first.detail)
        assertTrue(first.there)
        assertTrue(first.broken)
        assertEquals(0x1_0000_0001L, first.size)
        assertEquals(1_760_000_000L, first.time)
        assertArrayEquals(byteArrayOf(0x89.toByte(), 'P'.code.toByte(), 'N'.code.toByte(), 'G'.code.toByte()), first.icon)
        val second = request.saves[1]
        assertEquals("SLOT1", second.name)
        assertFalse(second.there)
        assertFalse(second.broken)
        assertEquals(0, second.icon.size)
    }

    // A game data install's progress as the core shows it (tests/psp/install.cpp writes the file): kind 7, mode 1, its
    // progress as the focus, the install as the one save, with INSDIR's icon (4096 bytes in the test).
    @Test
    fun readsTheCoresInstallProgress() {
        val request = assertNotNullRequest(parsePspDialog(resource("psp-dialog-install.bin")))
        assertEquals(1, request.id)
        assertEquals(PspDialogRequest.KIND_INSTALL, request.kind)
        assertEquals(1, request.language)
        assertTrue(request.circleConfirms)
        assertEquals(1, request.mode)
        assertEquals(39, request.installProgress())
        assertEquals(1, request.saves.size)
        val install = request.saves[0]
        assertEquals("DATA", install.name)
        assertEquals("A Game -Final Mix-", install.title)
        assertEquals("Install Data", install.saveTitle)
        assertEquals("Deleting this will cause\nload times to increase.", install.detail)
        assertFalse(install.there)
        assertEquals(4096L + 10240L + 2_621_440L, install.size)
        assertEquals(4096, install.icon.size)
        assertEquals(100, request.copy(focus = 250).installProgress())
        assertEquals(0, request.copy(focus = -1).installProgress())
    }

    private fun assertNotNullRequest(request: PspDialogRequest?): PspDialogRequest {
        assertTrue("the request wasn't read", request != null)
        return request!!
    }

    @Test
    fun refusesWhatIsntARequest() {
        val bytes = resource("psp-dialog-request.bin")
        assertNull(parsePspDialog(bytes.copyOf(bytes.size - 1)))
        assertNull(parsePspDialog(bytes + byteArrayOf(0)))
        assertNull(parsePspDialog(bytes.copyOf().also { it[0] = 2 }))
        assertNull(parsePspDialog(bytes.copyOf().also { it[20] = 0xff.toByte(); it[23] = 0x7f }))  // the text's length
        assertNull(parsePspDialog(ByteArray(0)))
    }

    @Test
    fun writesTheAnswerTheCoreReads() {
        val answer = PspDialogAnswer(7, button = PspDialogAnswer.BUTTON_BACK, choice = -1, cancelled = true,
            texts = listOf("Boss", "1☆2"))
        assertArrayEquals(resource("psp-dialog-answer.bin"), answer.encode())
    }

    @Test
    fun keyboardFieldsTakeWhatThePspTakes() {
        val name = PspKeyboardField("Name", "", 8, 0, 1, 0)
        assertEquals("Big Boss", name.accept("Big Boss!"))
        assertEquals("Snake☆", name.accept("Sna\uD83C\uDFAEke☆"))  // (a game pad: two UTF-16 units, no PSP character)
        assertEquals("ab", name.accept("a\nb"))
        assertEquals("a\nb", PspKeyboardField("Note", "", 20, 0, 3, 0).accept("a\n\tb"))  // (lines kept in several)
        val pin = PspKeyboardField("PIN", "", 4, PSP_INPUT_LATIN_DIGIT, 1, 0)
        assertTrue(pin.digitsOnly())
        assertEquals("1234", pin.accept("1a2b3c45"))
        assertFalse(PspKeyboardField("", "", 4, PSP_INPUT_LATIN_DIGIT or 0x8, 1, 0).digitsOnly())
        assertFalse(PspKeyboardField("", "", 4, 0, 1, 0).digitsOnly())
        assertEquals("", PspKeyboardField("", "", 0, 0, 1, 0).accept("anything"))
    }

    @Test
    fun keyboardFieldsTakeOnlyTheirInputTypes() {
        // 7th Dragon 2020's name: full-width letters alone, at most 8 (ASCII typed full-width, the space too, the digit
        // left out: no full-width digits)
        val name = PspKeyboardField("Input name.", "", 8, 0x0c00, 1, 1)
        assertEquals("\uff22\uff4f\uff42\u3000\uff21", name.accept("Bob 7A"))
        assertEquals("\uff30\uff33\uff30", name.accept("\uff30\uff33\uff30"))  // (already full-width)
        assertEquals("\uff21\uff22\uff23\uff24\uff25\uff26\uff27\uff28", name.accept("ABCDEFGHIJ"))
        assertEquals("\uff50\uff53\uff50", PspKeyboardField("", "", 8, PSP_INPUT_JAPANESE_LOWER, 1, 1).accept("PSP"))
        assertEquals("ps p", PspKeyboardField("", "", 8, PSP_INPUT_LATIN_LOWER, 1, 0).accept("\uff30S P"))
        assertEquals("PSP 1", PspKeyboardField("", "", 8, 0x0f, 1, 0).accept("\uff30SP\u30001"))
        assertEquals("\uff11\uff12", PspKeyboardField("", "", 8, PSP_INPUT_JAPANESE_DIGIT, 1, 1).accept("1 2"))
        assertEquals("\u30ab\u30ca", PspKeyboardField("", "", 8, PSP_INPUT_KATAKANA, 1, 1).accept("\u304b\u306a"))
        assertEquals("\u0434\u0430", PspKeyboardField("", "", 8, PSP_INPUT_RUSSIAN_LOWER, 1, 0).accept("\u0414a\u0410"))
        assertEquals("a/b?9", PspKeyboardField("", "", 8, PSP_INPUT_URL, 1, 0).accept("a/b?9\u3042"))
        assertEquals("a\nb", PspKeyboardField("", "", 8, PSP_INPUT_LATIN_LOWER, 2, 0).accept("a\nb"))
        assertEquals("\u2606a", PspKeyboardField("", "", 8, 0x40_0000, 1, 0).accept("\u2606a"))  // (no type known: all)
    }

    /**
     * The conversion's cases, which tests/psp/utility.cpp runs through the core's own (keyboardConform()), so the two
     * agree: each text as its field's input types and lines take it. A case is the types (hexadecimal), the lines, the
     * text and what's entered, apart by tabs, with \uXXXX, \n, \t and \\ escaped (psp-keyboard-cases.txt says more).
     */
    @Test
    fun keyboardFieldsConvertAsTheCoreDoes() {
        val cases = String(resource("psp-keyboard-cases.txt"), Charsets.UTF_8).lines().withIndex()
            .filter { (_, line) -> line.isNotEmpty() && !line.startsWith("#") }
        assertTrue(cases.size >= 40)
        for ((index, line) in cases) {
            val parts = line.split('\t')
            assertEquals("psp-keyboard-cases.txt line ${index + 1}", 4, parts.size)
            val (types, lines, text, entered) = parts
            val field = PspKeyboardField("", "", Int.MAX_VALUE, types.toInt(16), lines.toInt(), 0)
            assertEquals("psp-keyboard-cases.txt line ${index + 1}", unescape(entered), field.accept(unescape(text)))
        }
    }

    private fun unescape(text: String): String {
        val unescaped = StringBuilder()
        var at = 0
        while (at < text.length) {
            if (text[at] == '\\' && at + 1 < text.length) {
                when (val escaped = text[at + 1]) {
                    'u' -> {
                        unescaped.append(text.substring(at + 2, at + 6).toInt(16).toChar())
                        at += 6
                        continue
                    }
                    'n' -> unescaped.append('\n')
                    't' -> unescaped.append('\t')
                    else -> unescaped.append(escaped)
                }
                at += 2
            } else {
                unescaped.append(text[at++])
            }
        }
        return unescaped.toString()
    }

    @Test
    fun anUneditedDefaultIsEnteredAsTheGameGaveIt() {
        // As the keyboard dialog answers (keyboardTexts() as it opens, keyboardAnswer() as OK enters it): the game's
        // own text, left as it was, goes back as it is (a PSP's keyboard edits the text it starts with), where
        // converting it would have changed it (no full-width é or star) or emptied it (digits alone); typed over, each
        // is converted.
        val fields = listOf(
            PspKeyboardField("Name", "Renée ☆", 16, 0x0c00, 1, 1),
            PspKeyboardField("PIN", "Ünal", 4, PSP_INPUT_LATIN_DIGIT, 1, 0),
        )
        val keyboard = PspDialogRequest(
            3, PspDialogRequest.KIND_KEYBOARD, 1, false, "", 0, 0, false, false, "", "", fields, 0, 0, false,
            emptyList(),
        )
        val shown = keyboard.keyboardTexts()
        assertEquals(listOf("Renée ☆", "Ünal"), shown)
        val answer = keyboard.keyboardAnswer(shown)
        assertEquals(3, answer.id)
        assertEquals(listOf("Renée ☆", "Ünal"), answer.texts)
        assertEquals(
            listOf("\uff32\uff45\uff4e\uff45\uff45\u3000\uff58", "12"),
            keyboard.keyboardAnswer(listOf("Renee ☆x", "Ünal12")).texts,
        )
        assertEquals("Renée ☆", fields[0].entered("Renée ☆"))
        assertEquals("\uff32\uff45\uff4e\uff45", fields[0].entered("Renée"))
        // A default the field can't show whole, longer than its limit or holding a control character: what it shows
        // left as it was still sends the game's own (which the core cuts to the limit), typed over it's converted.
        val long = PspKeyboardField("Name", "PLAYER ONE", 8, PSP_INPUT_LATIN_LOWER, 1, 0)
        val tab = PspKeyboardField("Note", "A\tB", 8, 0, 1, 0)
        val awkward = PspDialogRequest(
            4, PspDialogRequest.KIND_KEYBOARD, 1, false, "", 0, 0, false, false, "", "", listOf(long, tab), 0, 0, false,
            emptyList(),
        )
        assertEquals(listOf("PLAYER O", "AB"), awkward.keyboardTexts())
        assertEquals(listOf("PLAYER ONE", "A\tB"), awkward.keyboardAnswer(awkward.keyboardTexts()).texts)
        assertEquals(listOf("player o", "AB!"), awkward.keyboardAnswer(listOf("PLAYER O!", "AB!")).texts)
    }

    @Test
    fun keyboardFieldsHoldWhatsTypedUntilEntered() {
        // While typing, the field holds what Android's keyboard composes, so romaji on its way to kana stays as typed
        // in a field of hiragana; only what the PSP hasn't got at all goes, and nothing past what the limit enters.
        val kana = PspKeyboardField("", "", 4, PSP_INPUT_HIRAGANA, 1, 1)
        assertEquals("\u3042k", kana.typing("\u3042k"))
        assertEquals("", kana.accept("k"))
        val name = PspKeyboardField("Input name.", "", 3, 0x0c00, 1, 1)
        assertEquals("Bob", name.typing("Bob"))  // (entered full-width, as the field takes it)
        assertEquals("\uff22\uff4f\uff42", name.accept(name.typing("Bob")))
        assertEquals("B7ob", name.typing("B7obby"))  // (the digit counts nothing: the field drops it)
        assertEquals("Bob", name.typing("Bob123"))  // (and nothing goes in once the limit's reached)
        assertEquals("ab", name.typing("a\uD83C\uDFAE\nb"))  // (a game pad and a line break: not in one line)
        assertEquals("a\nb", PspKeyboardField("", "", 8, 0, 2, 0).typing("a\nb"))
        assertEquals("", PspKeyboardField("", "", 0, 0, 1, 0).typing("anything"))
    }

    @Test
    fun confirmFollowsTheGamesConvention() {
        val cross = request(PspDialogRequest.SAVE_LOAD, emptyList())
        assertEquals(PhobosCore.Input.A, cross.confirmBit())  // (the app's A is the PSP's cross)
        assertEquals(PhobosCore.Input.B, cross.backBit())
        assertEquals("✕", cross.confirmGlyph())
        val circle = request(PspDialogRequest.SAVE_LOAD, emptyList(), circle = true)
        assertEquals(PhobosCore.Input.B, circle.confirmBit())
        assertEquals(PhobosCore.Input.A, circle.backBit())
        assertEquals("○", circle.confirmGlyph())
        assertEquals("✕", circle.backGlyph())
    }

    @Test
    fun saveDialogsAskWhatAPspAsks() {
        assertEquals("Do you want to load this data?" to false, pspSaveQuestion(PspDialogRequest.SAVE_LOAD, save()))
        assertEquals("The data is corrupted." to false,
            pspSaveQuestion(PspDialogRequest.SAVE_LIST_LOAD, save(broken = true)))
        assertNull(pspSaveQuestion(PspDialogRequest.SAVE_LIST_LOAD, save()))
        assertEquals("Do you want to save this data?" to false,
            pspSaveQuestion(PspDialogRequest.SAVE_SAVE, save(there = false)))
        assertEquals("Do you want to overwrite this data?" to true, pspSaveQuestion(PspDialogRequest.SAVE_SAVE, save()))
        assertEquals("Do you want to overwrite this data?" to true,
            pspSaveQuestion(PspDialogRequest.SAVE_LIST_SAVE, save()))
        assertNull(pspSaveQuestion(PspDialogRequest.SAVE_LIST_SAVE, save(there = false)))
        for (mode in listOf(PspDialogRequest.SAVE_DELETE, PspDialogRequest.SAVE_LIST_DELETE,
                            PspDialogRequest.SAVE_LIST_ALL_DELETE)) {
            assertEquals("This save data will be deleted. Are you sure?" to true, pspSaveQuestion(mode, save()))
        }
    }

    @Test
    fun nothingToLoadOrDeleteIsSaid() {
        assertTrue(request(PspDialogRequest.SAVE_LIST_LOAD, emptyList()).nothingThere())
        assertFalse(request(PspDialogRequest.SAVE_LIST_LOAD, listOf(save())).nothingThere())
        assertTrue(request(PspDialogRequest.SAVE_LOAD, listOf(save(there = false))).nothingThere())
        assertTrue(request(PspDialogRequest.SAVE_DELETE, listOf(save(there = false))).nothingThere())
        assertTrue(request(PspDialogRequest.SAVE_LIST_DELETE, emptyList()).nothingThere())
        assertFalse(request(PspDialogRequest.SAVE_SAVE, listOf(save(there = false))).nothingThere())
        assertFalse(request(PspDialogRequest.SAVE_LIST_SAVE, emptyList()).nothingThere())
        assertTrue(request(PspDialogRequest.SAVE_LIST_ALL_DELETE, listOf(save())).listed())
        assertFalse(request(PspDialogRequest.SAVE_DELETE, listOf(save())).listed())
    }

    @Test
    fun savesShowTheirSizeAsAPspDoes() {
        assertEquals("0 KB", pspSaveSize(0))
        assertEquals("1 KB", pspSaveSize(1))
        assertEquals("1 KB", pspSaveSize(1024))
        assertEquals("97 KB", pspSaveSize(98_305))
        assertEquals("Load", pspSaveHeading(PspDialogRequest.SAVE_LIST_LOAD))
        assertEquals("Save", pspSaveHeading(PspDialogRequest.SAVE_SAVE))
        assertEquals("Delete", pspSaveHeading(PspDialogRequest.SAVE_LIST_ALL_DELETE))
    }
}
