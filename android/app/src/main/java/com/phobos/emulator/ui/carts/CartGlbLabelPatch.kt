package com.phobos.emulator.ui.carts

import android.util.Base64
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

/**
 * gltfio's ubershader only exposes `baseColorMap` when the glTF material declares a base-color texture. The baked
 * Cartridge shells have flat `label` materials (no texture), so at runtime `setParameter("baseColorMap", ...)` has no
 * parameter to write and the label face rendered flat white. This patches a GLB in memory: it embeds a 1x1 white PNG
 * and points every `label` material at it, which makes the loader compile the mapped variant. `CartMaterials` then
 * replaces the map with the real label bitmap. Geometry and all other materials are untouched.
 */
internal object CartGlbLabelPatch {

    private const val MAGIC = 0x46546C67 // "glTF"
    private const val CHUNK_JSON = 0x4E4F534A
    private const val CHUNK_BIN = 0x004E4942

    /** 1x1 opaque white PNG. */
    private val WHITE_PNG: ByteArray = Base64.decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==",
        Base64.DEFAULT,
    )

    fun patch(glb: ByteArray): ByteArray {
        if (glb.size < 20) return glb
        val src = ByteBuffer.wrap(glb).order(ByteOrder.LITTLE_ENDIAN)
        if (src.int != MAGIC) return glb
        src.int // version
        src.int // total length
        var jsonBytes: ByteArray? = null
        var binBytes: ByteArray? = null
        while (src.remaining() >= 8) {
            val len = src.int
            val type = src.int
            if (len < 0 || len > src.remaining()) break
            val chunk = ByteArray(len).also { src.get(it) }
            when (type) {
                CHUNK_JSON -> jsonBytes = chunk
                CHUNK_BIN -> binBytes = chunk
            }
        }
        val json = jsonBytes?.let {
            runCatching { JSONObject(String(it, StandardCharsets.UTF_8).trimEnd('\u0000', ' ')) }.getOrNull()
        } ?: return glb
        val materials = json.optJSONArray("materials") ?: return glb

        var changed = false
        for (i in 0 until materials.length()) {
            val mat = materials.optJSONObject(i) ?: continue
            if (mat.optString("name") != "label") continue
            val pbr = mat.optJSONObject("pbrMetallicRoughness") ?: JSONObject().also {
                mat.put("pbrMetallicRoughness", it)
            }
            if (pbr.has("baseColorTexture")) continue
            val imageIndex = addImage(json, binBytes ?: return glb)
            val textureIndex = addTexture(json, imageIndex)
            pbr.put("baseColorTexture", JSONObject().put("index", textureIndex))
            changed = true
        }
        if (!changed) return glb

        // The PNG lives at the end of the BIN chunk; the writer below copies the old BIN plus the padding + PNG.
        val oldBin = binBytes ?: ByteArray(0)
        val alignedOld = (oldBin.size + 3) and 3.inv()
        val padded = ByteArray(alignedOld) ; oldBin.copyInto(padded) // old bin already padded by the GLB spec
        val pngOffset = padded.size
        val newBin = padded + WHITE_PNG
        val newBinPadded = newBin.copyOf((newBin.size + 3) and 3.inv())
        json.getJSONArray("buffers").getJSONObject(0).put("byteLength", newBinPadded.size)
        val bv = json.getJSONArray("bufferViews").getJSONObject(json.getJSONArray("bufferViews").length() - 1)
        bv.put("buffer", 0).put("byteOffset", pngOffset).put("byteLength", WHITE_PNG.size)

        val jsonOut = json.toString().toByteArray(StandardCharsets.UTF_8)
        val jsonPadded = jsonOut.copyOf((jsonOut.size + 3) and 3.inv())
        for (i in jsonOut.size until jsonPadded.size) jsonPadded[i] = ' '.code.toByte()
        val total = 12 + 8 + jsonPadded.size + 8 + newBinPadded.size
        val out = ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN)
        out.putInt(MAGIC).putInt(2).putInt(total)
        out.putInt(jsonPadded.size).putInt(CHUNK_JSON).put(jsonPadded)
        out.putInt(newBinPadded.size).putInt(CHUNK_BIN).put(newBinPadded)
        return out.array()
    }

    private fun addImage(json: JSONObject, bin: ByteArray): Int {
        val views = json.getJSONArray("bufferViews")
        val viewIndex = views.length()
        val oldSize = bin.size
        val aligned = (oldSize + 3) and 3.inv()
        views.put(
            JSONObject().put("buffer", 0).put("byteOffset", aligned).put("byteLength", WHITE_PNG.size),
        )
        val images = json.optJSONArray("images") ?: org.json.JSONArray().also { json.put("images", it) }
        val imageIndex = images.length()
        images.put(JSONObject().put("bufferView", viewIndex).put("mimeType", "image/png"))
        return imageIndex
    }

    private fun addTexture(json: JSONObject, imageIndex: Int): Int {
        val samplers = json.optJSONArray("samplers") ?: org.json.JSONArray().also { json.put("samplers", it) }
        val samplerIndex = samplers.length()
        samplers.put(
            JSONObject().put("magFilter", 9729).put("minFilter", 9729).put("wrapS", 33071).put("wrapT", 33071),
        )
        val textures = json.optJSONArray("textures") ?: org.json.JSONArray().also { json.put("textures", it) }
        val textureIndex = textures.length()
        textures.put(JSONObject().put("sampler", samplerIndex).put("source", imageIndex))
        return textureIndex
    }
}
