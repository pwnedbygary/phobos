package com.phobos.emulator.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class MsxKeyboardTest {
    /** The MSX core's keyboard matrix labels, read from its source (tests run from android/app). */
    private fun coreLabels(): Set<String> {
        val source = File("../../ares/msx/keyboard/keyboard.cpp").readText()
        val start = source.indexOf("string labels[12][8]")
        val end = source.indexOf("for(u32 column", start)
        return Regex("\"((?:[^\"\\\\]|\\\\.)*)\"").findAll(source.substring(start, end))
            .map { it.groupValues[1].replace("\\\"", "\"").replace("\\\\", "\\") }
            .filter { it.isNotEmpty() }
            .toSet()
    }

    @Test fun everyKeyPressesALabelTheCoreHas() {
        val labels = coreLabels()
        assertEquals(90, labels.size)
        val unknown = MSX_KEYBOARD_NODES - labels
        assertTrue("Labels the core doesn't have: $unknown", unknown.isEmpty())
    }

    @Test fun theKeyboardCoversTheTypingKeys() {
        // The main block, the function and editing keys and the cursor keys: everything but the
        // numeric keypad, the two Japanese-only keys and the accent key C-BIOS ignores.
        assertEquals(71, MSX_KEYBOARD_NODES.size)
    }
}
