package com.phobos.emulator.art

import android.content.Context
import org.json.JSONObject
import java.util.zip.GZIPInputStream

/**
 * Matches a game to a real LaunchBox label scan through the bundled index (`labels.json.gz`).
 *
 * Port of Cartridge's `electron/scraper.js` (`labels()`, `labelShape`, `matchLabel`): exact title key first, then a
 * bigram fuzzy match (Dice >= 0.88, lengths within max(5, 30%)), entries filtered to the regions whose scans fit
 * the shell and to the right media type, preferring the game's own region and real scans over fan-made ones.
 *
 * The index is held as flat arrays (keys, file names, per-key ranges, region strings, one-byte types) rather than
 * a tree of per-entry objects, and the parsed JSON is dropped once [parse] returns - the RP6 loads this once per
 * process ([BundledLabelIndex]) and never re-parses it. Fuzzy bigrams are computed on the fly: only exact-key
 * misses scan, and only keys within the length window, so no per-key bigram cache is retained.
 *
 * Fixtures: `tools/cart-fixtures/make-label-fixtures.mjs` -> `labelmatch-fixtures.json`.
 */
object LabelMatch {

    data class Match(val urls: List<String>, val shape: String)

    /** One system's entries, all in flat arrays: entry i of key k lives at start[k] + 0..count[k]-1. */
    class System(
        val keys: Array<String>,
        val files: Array<String>,
        val start: IntArray,
        val count: IntArray,
        val regions: Array<String>,
        val types: ByteArray,
    ) {
        val exact: HashMap<String, Int> = HashMap<String, Int>(keys.size * 2).apply {
            for (i in this@System.keys.indices) put(this@System.keys[i], i)
        }
    }

    class Index(val base: String, private val systems: Map<String, System>) {
        val entries: Int = systems.values.sumOf { it.files.size }
        fun system(id: String): System? = systems[id]
    }

    private const val TYPE_C = 0.toByte()
    private const val TYPE_D = 1.toByte()
    private const val TYPE_FC = 2.toByte()
    private const val TYPE_FD = 3.toByte()

    private val regionOrder = mapOf(
        "US" to listOf("US", "W", "AU", "EU", "", "JP"),
        "EU" to listOf("EU", "AU", "W", "US", "", "JP"),
        "JP" to listOf("JP", "AS", "W", "US", "", "EU"),
        "W" to listOf("W", "US", "EU", "AU", "", "JP"),
    )

    /** Parses the index JSON: `{"base": "...", "built": "...", "index": {"<system>": {"<key>": [[file, region, type], ...]}}}`. */
    /** The one approved base for label scans; the index must name exactly this or it is not used. */
    const val LABEL_BASE = "https://images.launchbox-app.com/"

    fun parse(json: String): Index {
        val root = JSONObject(json)
        val base = root.optString("base")
        require(base == LABEL_BASE) { "label index base '$base' is not the approved host $LABEL_BASE" }
        val systems = HashMap<String, System>()
        val index = root.optJSONObject("index") ?: return Index(base, systems)
        val intern = HashMap<String, String>()
        for (systemName in index.keys()) {
            val byKey = index.getJSONObject(systemName)
            val keys = ArrayList<String>(byKey.length())
            val start = IntArray(byKey.length())
            val count = IntArray(byKey.length())
            val files = ArrayList<String>()
            val regions = ArrayList<String>()
            val types = ArrayList<Byte>()
            var k = 0
            for (key in byKey.keys()) {
                keys.add(key)
                start[k] = files.size
                val arr = byKey.getJSONArray(key)
                for (i in 0 until arr.length()) {
                    val e = arr.optJSONArray(i) ?: continue
                    files.add(e.optString(0))
                    val r = e.optString(1)
                    regions.add(intern.getOrPut(r) { r })
                    types.add(
                        when (e.optString(2)) {
                            "d" -> TYPE_D
                            "fc" -> TYPE_FC
                            "fd" -> TYPE_FD
                            else -> TYPE_C
                        }
                    )
                }
                count[k] = files.size - start[k]
                k++
            }
            systems[systemName] = System(
                keys.toTypedArray(),
                files.toTypedArray(),
                start,
                count,
                regions.toTypedArray(),
                types.toByteArray(),
            )
        }
        return Index(base, systems)
    }

    /**
     * Cartridge's `labelShape`: a disc system uses the whole printed face; GBC always uses the GBC cut; otherwise the
     * system's media shape when one has a label spec; null when the shell has none.
     */
    fun labelShape(systemId: String, mediaType: String?, mediaShape: String?): String? {
        if (mediaType == "disc") return "disc"
        val k = if (systemId == "gbc") "gbc" else mediaShape
        return if (k != null && LabelCrop.SPECS.containsKey(k)) k else null
    }

    private fun bigrams(s: String): Set<String> {
        val out = HashSet<String>()
        for (i in 0 until s.length - 1) out.add(s.substring(i, i + 2))
        return out
    }

    private fun dice(a: Set<String>, b: Set<String>): Double {
        if (a.isEmpty() && b.isEmpty()) return 0.0
        var shared = 0
        for (x in a) if (x in b) shared++
        return 2.0 * shared / (a.size + b.size)
    }

    /** Cartridge's `matchLabel`: null when the system has no index entries, no shape, or nothing fits. */
    fun match(
        index: Index,
        systemId: String,
        mediaType: String?,
        mediaShape: String?,
        file: String,
        region: String,
    ): Match? {
        val system = index.system(systemId) ?: return null
        val shape = labelShape(systemId, mediaType, mediaShape) ?: return null
        val key = artMatchKey(file)
        if (key.isEmpty()) return null
        var keyIndex = system.exact[key]
        if (keyIndex == null) {
            // Fuzzy: only keys of similar length, best Dice >= 0.88. Bigrams are built on the fly: misses are rare
            // and a per-key bigram cache would cost more memory than the whole flat index.
            val grams = bigrams(key)
            val window = Math.max(5, (key.length * 0.3).toInt())
            var best = -1
            var bestScore = 0.0
            for (i in system.keys.indices) {
                val k = system.keys[i]
                if (Math.abs(k.length - key.length) > window) continue
                val score = dice(grams, bigrams(k))
                if (score >= 0.88 && score > bestScore) {
                    bestScore = score
                    best = i
                }
            }
            if (best < 0) return null
            keyIndex = best
        }
        val allowed = LabelCrop.REGIONS[shape]
        val order = regionOrder[region] ?: regionOrder.getValue("US")
        val wantType = if (shape == "disc" || shape == "umd") TYPE_D else TYPE_C
        val fanType = (wantType + 2).toByte()
        val from = system.start[keyIndex]
        val to = from + system.count[keyIndex]
        val scored = ArrayList<Pair<String, Int>>(to - from)
        for (i in from until to) {
            val regionCode = system.regions[i]
            if (allowed != null && regionCode !in allowed) continue
            val type = system.types[i]
            if (type != wantType && type != fanType) continue
            val ri = order.indexOf(regionCode)
            val score = (if (type == wantType) 0 else 100) + (if (ri < 0) 50 else ri * 10)
            scored.add(system.files[i] to score)
        }
        if (scored.isEmpty()) return null
        // A key holds only a handful of scans; sorting those is cheaper than tracking slots through every branch.
        val urls = scored.sortedBy { it.second }.take(3).map { index.base + it.first }
        return Match(urls, shape)
    }
}
