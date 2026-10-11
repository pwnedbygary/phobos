package com.phobos.emulator.ui

import com.phobos.emulator.launch.LaunchSystems
import org.junit.Assert.assertEquals
import org.junit.Test

class ExperimentalSystemsTest {
    /** The mark looks systems up by the exact name the Library lists: a near miss ("Playstation Portable") marks nothing. */
    @Test fun everyExperimentalSystemIsALibrarySystem() {
        for (system in experimentalSystems) assertEquals(system, LaunchSystems.systemForName(system))
    }
}
