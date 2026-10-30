package com.phobos.emulator.art

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Compares [LabelCrop] with Cartridge's own crate/labelcrop.js answers on the synthetic bitmaps. */
class LabelCropTest {

    private fun resource(name: String) = javaClass.getResource("/carts/$name")!!.readText()

    private val fixture = JSONObject(resource("labelcrop-fixtures.json"))

    private fun bitmap(w: Int, h: Int, bg: IntArray, rect: IntArray, fg: IntArray): ByteArray {
        val bmp = ByteArray(w * h * 4)
        for (y in 0 until h) {
            for (x in 0 until w) {
                val i = (y * w + x) * 4
                val inside = x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3]
                val c = if (inside) fg else bg
                bmp[i] = c[0].toByte()
                bmp[i + 1] = c[1].toByte()
                bmp[i + 2] = c[2].toByte()
                bmp[i + 3] = c[3].toByte()
            }
        }
        return bmp
    }

    private fun ints(a: org.json.JSONArray): IntArray = IntArray(a.length()) { a.getInt(it) }

    @Test
    fun cropRectsMatchCartridge() {
        val cases = fixture.getJSONArray("cases")
        for (i in 0 until cases.length()) {
            val c = cases.getJSONObject(i)
            val w = c.getInt("w")
            val h = c.getInt("h")
            val bmp = bitmap(w, h, ints(c.getJSONArray("bg")), ints(c.getJSONArray("rect")), ints(c.getJSONArray("fg")))
            val shape = c.getString("shape")
            val actual = LabelCrop.labelRect(bmp, w, h, shape)
            if (c.isNull("expected")) {
                assertNull("case $i ($shape ${w}x$h)", actual)
            } else {
                val e = c.getJSONObject("expected")
                assertEquals(
                    "case $i ($shape ${w}x$h)",
                    intArrayOf(e.getInt("x"), e.getInt("y"), e.getInt("w"), e.getInt("h")).toList(),
                    actual?.let { listOf(it.x, it.y, it.w, it.h) },
                )
            }
        }
    }
}
