package com.phobos.emulator.ui.carts

import android.graphics.Bitmap
import com.google.android.filament.Engine
import com.google.android.filament.MaterialInstance
import com.google.android.filament.Texture
import com.google.android.filament.TextureSampler
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The per-game look on a loaded shell: the shell's colour and finish (with play-time wear as extra roughness), and
 * the game's label picture on the label primitive. The baked GLBs name their materials per part - `shell`, `dark`,
 * `metal`, `screws`, `pcb`, `glass`, `window`, `label` for carts and `disc`, `hub`, `stack`, `label` for discs - so
 * the parts are found by material name, in the live focused view and on the offscreen cards alike.
 *
 * What this does not do yet, stated for the note: translucency is approximated per instance through the base
 * colour's alpha (Filament's transparency mode is a material property, not a per-instance one), and wear is extra
 * roughness rather than a speck texture; both are reviewed on the RP6 pass.
 */
object CartMaterials {

    /** Applies [look] and [wear] to the shell parts of [entities]; the label bitmap, when given, to `label`. */
    fun apply(
        engine: Engine,
        entities: IntArray,
        look: LookResolver.Look?,
        wear: Int,
        label: Bitmap?,
    ): List<Texture> {
        val created = mutableListOf<Texture>()
        val rm = engine.renderableManager
        // One texture per call, shared by every label primitive of the same shell.
        var labelTexture: Texture? = null
        for (e in entities) {
            if (!rm.hasComponent(e)) continue
            val instance = rm.getInstance(e)
            for (primitive in 0 until rm.getPrimitiveCount(instance)) {
                val mi = rm.getMaterialInstanceAt(instance, primitive)
                when (mi.material.name) {
                    "shell" -> {
                        applyColour(mi, look)
                        applyFinish(mi, look?.finish ?: "solid")
                        applyWear(mi, wear)
                    }
                    "label" -> {
                        // Never leave the material without a map: a label texture is destroyed after the draw by the
                        // caller, and a later draw with no label must not sample freed memory.
                        val texture = label?.let { bmp ->
                            labelTexture ?: texture(engine, bmp).also {
                                labelTexture = it
                                created += it
                            }
                        } ?: placeholder(engine)
                        mi.setParameter("baseColorMap", texture, sampler())
                    }
                }
            }
        }
        return created
    }

    /** A 1x1 white map so a label material always has a live texture; one per engine, freed with the engine. */
    private val placeholders = java.util.WeakHashMap<Engine, Texture>()

    @Synchronized
    private fun placeholder(engine: Engine): Texture {
        placeholders[engine]?.let { return it }
        val t = Texture.Builder()
            .width(1)
            .height(1)
            .levels(1)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.RGBA8)
            .usage(Texture.Usage.SAMPLEABLE or Texture.Usage.UPLOADABLE)
            .build(engine)
        val buffer = java.nio.ByteBuffer.allocateDirect(4).order(java.nio.ByteOrder.nativeOrder())
        buffer.put(0, 255.toByte())
        buffer.put(1, 255.toByte())
        buffer.put(2, 255.toByte())
        buffer.put(3, 255.toByte())
        t.setImage(engine, 0, 0, 0, 1, 1, Texture.PixelBufferDescriptor(buffer, Texture.Format.RGBA, Texture.Type.UBYTE))
        placeholders[engine] = t
        return t
    }

    private fun sampler() = TextureSampler().apply {
        minFilter = TextureSampler.MinFilter.LINEAR
        magFilter = TextureSampler.MagFilter.LINEAR
        wrapModeS = TextureSampler.WrapMode.CLAMP_TO_EDGE
        wrapModeT = TextureSampler.WrapMode.CLAMP_TO_EDGE
    }

    private fun parse(hex: String): FloatArray {
        val clean = hex.removePrefix("#")
        val v = clean.toIntOrNull(16) ?: 0x6F4FB0
        return floatArrayOf(
            ((v shr 16) and 0xFF) / 255f,
            ((v shr 8) and 0xFF) / 255f,
            (v and 0xFF) / 255f,
        )
    }

    private fun applyColour(mi: MaterialInstance, look: LookResolver.Look?) {
        val c = parse(look?.color ?: LookResolver.defaultShellColors.getValue(look?.finish ?: "solid"))
        val alpha = when (look?.finish) {
            "clear" -> 0.15f
            "translucent" -> 0.45f
            else -> 1f
        }
        mi.setParameter("baseColorFactor", floatArrayOf(c[0], c[1], c[2], alpha))
    }

    private fun applyFinish(mi: MaterialInstance, finish: String) {
        when (finish) {
            "metallic" -> {
                mi.setParameter("metallicFactor", 0.95f)
                mi.setParameter("roughnessFactor", 0.3f)
            }
            "translucent" -> {
                mi.setParameter("metallicFactor", 0.0f)
                mi.setParameter("roughnessFactor", 0.2f)
            }
            "clear" -> {
                mi.setParameter("metallicFactor", 0.0f)
                mi.setParameter("roughnessFactor", 0.08f)
            }
            "glitter" -> {
                mi.setParameter("metallicFactor", 0.35f)
                mi.setParameter("roughnessFactor", 0.55f)
            }
            "glow" -> {
                mi.setParameter("metallicFactor", 0.0f)
                mi.setParameter("roughnessFactor", 0.6f)
                // A soft self-lit edge: the lit material's emissive term.
                runCatching { mi.setParameter("emissiveFactor", floatArrayOf(0.25f, 0.45f, 0.2f)) }
            }
            else -> {
                mi.setParameter("metallicFactor", 0.0f)
                mi.setParameter("roughnessFactor", 0.5f)
            }
        }
    }

    private fun applyWear(mi: MaterialInstance, wear: Int) {
        if (wear <= 0) return
        // 0..3 from play time; more wear, duller and rougher plastic.
        val base = 0.5f
        mi.setParameter("roughnessFactor", (base + 0.08f * wear).coerceAtMost(0.95f))
    }

    private fun texture(engine: Engine, bitmap: Bitmap): Texture {
        val texture = Texture.Builder()
            .width(bitmap.width)
            .height(bitmap.height)
            .levels(1)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.RGBA8)
            .usage(Texture.Usage.SAMPLEABLE or Texture.Usage.UPLOADABLE)
            .build(engine)
        val buffer = ByteBuffer.allocateDirect(bitmap.width * bitmap.height * 4).order(ByteOrder.nativeOrder())
        val row = IntArray(bitmap.width)
        for (y in 0 until bitmap.height) {
            bitmap.getPixels(row, 0, bitmap.width, 0, y, bitmap.width, 1)
            for (x in row.indices) {
                val p = row[x]
                buffer.put(((p ushr 16) and 0xFF).toByte())
                buffer.put(((p ushr 8) and 0xFF).toByte())
                buffer.put((p and 0xFF).toByte())
                buffer.put(((p ushr 24) and 0xFF).toByte())
            }
        }
        buffer.rewind()
        texture.setImage(engine, 0, Texture.PixelBufferDescriptor(
            buffer,
            Texture.Format.RGBA,
            Texture.Type.UBYTE,
        ))
        return texture
    }
}
