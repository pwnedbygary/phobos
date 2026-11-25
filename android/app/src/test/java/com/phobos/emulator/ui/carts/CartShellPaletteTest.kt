package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class CartShellPaletteTest {

    @Test
    fun everyBakedShapeHasAPlasticColour() {
        val assets = File("src/main/assets/carts/carts").listFiles { f -> f.isFile && f.extension == "glb" }
            ?.map { it.nameWithoutExtension }?.toSet().orEmpty()
        val shapes = CartShapes.allShapeEntries().values.toSet() + assets + setOf("tape")
        val missing = shapes - CartShellPalette.shapes()
        assertTrue("shapes with no colour: $missing", missing.isEmpty())
    }

    @Test
    fun coloursAreValidAndNotTheGenericPurple() {
        for (shape in CartShellPalette.shapes()) {
            val colour = CartShellPalette.forShape(shape)
            assertTrue("$shape: $colour", Regex("#[0-9a-fA-F]{6}").matches(colour))
            assertFalse("$shape kept Cartridge's generic purple", colour == "#6f4fb0")
        }
    }

    @Test
    fun unknownShapeFallsBackToCartridgesPlastic() {
        assertEquals("#6f4fb0", CartShellPalette.forShape("no-such-shell"))
        assertEquals("#6f4fb0", CartShellPalette.forShape(null))
    }
}
