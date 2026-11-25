package com.phobos.emulator.ui.carts

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

class CartGlbLabelPatchTest {

    private val json = """
        {"asset":{"version":"2.0"},"buffers":[{"byteLength":0}],"bufferViews":[],
         "materials":[{"name":"shell","pbrMetallicRoughness":{}},
                      {"name":"label","pbrMetallicRoughness":{}}]}
    """.trimIndent()

    @Test
    fun labelMaterialGetsAWhiteMapAndUnlit() {
        val original = glb(json)
        val patched = CartGlbLabelPatch.patch(original)
        assertTrue(patched.contentEquals(original).not())

        val obj = jsonOf(patched)
        val materials = obj.getJSONArray("materials")
        val label = materials.getJSONObject(1)
        val pbr = label.getJSONObject("pbrMetallicRoughness")
        assertTrue("baseColorTexture was not added", pbr.has("baseColorTexture"))
        assertTrue("KHR_materials_unlit was not added", label.getJSONObject("extensions").has("KHR_materials_unlit"))
        assertTrue(extensionsUsed(obj).contains("KHR_materials_unlit"))

        // The shell material is untouched.
        assertEquals("shell", materials.getJSONObject(0).getString("name"))
        assertTrue(!materials.getJSONObject(0).getJSONObject("pbrMetallicRoughness").has("baseColorTexture"))

        // The white image is in the BIN and the buffer length covers it.
        assertTrue(obj.getJSONArray("buffers").getJSONObject(0).getInt("byteLength") > 0)
        assertNotNull(obj.getJSONArray("images").getJSONObject(0).getInt("bufferView"))
    }

    @Test
    fun patchIsIdempotent() {
        val once = CartGlbLabelPatch.patch(glb(json))
        val twice = CartGlbLabelPatch.patch(once)
        assertTrue(twice.contentEquals(once))
    }

    @Test
    fun aShellWithoutALabelIsLeftAlone() {
        val onlyShell = """{"asset":{"version":"2.0"},"buffers":[{"byteLength":0}],"bufferViews":[],"materials":[{"name":"shell","pbrMetallicRoughness":{}}]}"""
        val original = glb(onlyShell)
        assertTrue(CartGlbLabelPatch.patch(original).contentEquals(original))
    }

    @Test
    fun garbageIsReturnedUnchanged() {
        val bytes = ByteArray(32) { it.toByte() }
        assertTrue(CartGlbLabelPatch.patch(bytes).contentEquals(bytes))
    }

    private fun extensionsUsed(obj: JSONObject): Set<String> {
        val array = obj.optJSONArray("extensionsUsed") ?: return emptySet()
        return buildSet { for (i in 0 until array.length()) add(array.getString(i)) }
    }

    private fun jsonOf(glb: ByteArray): JSONObject {
        val buffer = ByteBuffer.wrap(glb).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(0x46546C67, buffer.int)
        buffer.int
        buffer.int
        val jsonLength = buffer.int
        assertEquals(0x4E4F534A, buffer.int)
        val bytes = ByteArray(jsonLength).also { buffer.get(it) }
        return JSONObject(String(bytes, StandardCharsets.UTF_8).trimEnd('\u0000', ' '))
    }

    private fun glb(jsonText: String, bin: ByteArray = ByteArray(0)): ByteArray {
        val jsonBytes = jsonText.toByteArray(StandardCharsets.UTF_8)
        val jsonPadded = jsonBytes.copyOf((jsonBytes.size + 3) and 3.inv())
        for (i in jsonBytes.size until jsonPadded.size) jsonPadded[i] = ' '.code.toByte()
        val binPadded = bin.copyOf((bin.size + 3) and 3.inv())
        val total = 12 + 8 + jsonPadded.size + 8 + binPadded.size
        val out = ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN)
        out.putInt(0x46546C67).putInt(2).putInt(total)
        out.putInt(jsonPadded.size).putInt(0x4E4F534A).put(jsonPadded)
        out.putInt(binPadded.size).putInt(0x004E4942).put(binPadded)
        return out.array()
    }
}
