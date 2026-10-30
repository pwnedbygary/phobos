package com.phobos.emulator.ui.carts

import com.phobos.emulator.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/** The label's face mapping is pure; the resources themselves are checked to exist (ids non-zero) and bundled. */
class CartFaceResolverTest {

    @Test
    fun stylesMapToBundledFaces() {
        assertEquals(CartFaceResolver.Face.VT323, CartFaceResolver.face("crt"))
        assertEquals(CartFaceResolver.Face.MANGA, CartFaceResolver.face("manga"))
        assertEquals(CartFaceResolver.Face.GALMURI, CartFaceResolver.face("pixel"))
        assertEquals(CartFaceResolver.Face.DOTGOTHIC, CartFaceResolver.face("rpg"))
        assertEquals(CartFaceResolver.Face.MPLUS1, CartFaceResolver.face("xmb"))
    }

    @Test
    fun theDefaultBorrowsMplusOneAndUnknownNamesToo() {
        assertEquals(CartFaceResolver.Face.MPLUS1, CartFaceResolver.face(null))
        assertEquals(CartFaceResolver.Face.MPLUS1, CartFaceResolver.face(""))
        assertEquals(CartFaceResolver.Face.MPLUS1, CartFaceResolver.face("whatever"))
    }

    @Test
    fun everyFaceIsABundledResource() {
        for (face in CartFaceResolver.Face.entries) {
            assertNotEquals("${face.name} must be a bundled res/font", 0, face.fontRes)
        }
        assertNotEquals(R.font.mplus1, 0)
        assertNotEquals(R.font.vt323, 0)
        assertNotEquals(R.font.comic_neue_regular, 0)
        assertNotEquals(R.font.galmuri11, 0)
        assertNotEquals(R.font.dotgothic16, 0)
    }
}
