package com.phobos.emulator.ui.carts

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** Every shape the UI can ask Filament for must exist in the baked assets: the build cannot catch a missing file. */
class CartShapeAssetsTest {

    @Test
    fun everyMappedShapeHasItsBakedAsset() {
        val root = File("src/main/assets/carts")
        assertTrue("assets root $root", root.isDirectory)
        val entries = CartShapes.allShapeEntries()
        assertTrue("the map is not empty", entries.isNotEmpty())
        for ((system, shape) in entries) {
            val file = if (CartShapes.isDisc(system)) File(root, "discs/$shape.glb") else File(root, "carts/$shape.glb")
            assertTrue("$system maps to $shape, but $file is missing", file.isFile)
        }
    }
}
