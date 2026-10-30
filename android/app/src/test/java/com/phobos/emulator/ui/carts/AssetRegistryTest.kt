package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/** The engine's asset registry: every created asset is tracked, snapshotted for release and cleared. */
class AssetRegistryTest {

    @Test
    fun everyAssetIsReleasedWithTheEngine() {
        val registry = AssetRegistry<String>()
        registry.add("cart:gb")
        registry.add("disc:cd")
        registry.add("cart:gb") // identity set: adding the same asset twice does not duplicate it
        assertEquals(2, registry.size())
        assertEquals(setOf("cart:gb", "disc:cd"), registry.snapshot().toSet())
        registry.clear()
        assertEquals(0, registry.size())
        assertFalse(registry.snapshot().isNotEmpty())
    }
}
