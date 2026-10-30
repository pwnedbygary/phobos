package com.phobos.emulator.art

import java.io.File
import java.util.zip.GZIPInputStream
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Host timing for the fuzzy label match, printed (never asserted) for the design note: median and p95 per card and
 * the time for a screenful, on the real bundled index. Runs only when the index can be read (the booked build run
 * from the app module directory); skipped otherwise.
 */
class LabelMatchPerfTest {

    private fun indexFile(): File? = listOf(
        File("src/main/assets/carts/labels.json.gz"),
        File("android/app/src/main/assets/carts/labels.json.gz"),
        File(System.getProperty("carts.labelIndex", "")),
    ).firstOrNull { it.isFile }

    @Test
    fun fuzzyMatchCostOnAHostGrid() {
        val file = indexFile()
        assumeTrue("label index not present for the timing run", file != null)
        val index = LabelMatch.parse(
            GZIPInputStream(file!!.inputStream()).use { it.readBytes() }.toString(Charsets.UTF_8),
        )
        println("perf: index ${index.entries} entries, base ${index.base}")

        // 200 realistic cards: real keys mutated so the exact lookup misses and the fuzzy path runs (the worst case).
        val names = ArrayList<Pair<String, String>>()
        for (system in listOf("gb", "gbc", "gba", "nes", "snes", "n64", "psx")) {
            val s = index.system(system) ?: continue
            val mediaType = if (system == "psx") "disc" else "cart"
            val mediaShape = if (system == "psx") "cd" else system
            for (i in 0 until 25) {
                if (s.files.isEmpty()) break
                val k = s.keys[(i * 977) % s.keys.size]
                names += system to mutate(k)
            }
        }
        val warm = names.take(16)
        warm.forEach { (sys, name) ->
            LabelMatch.match(index, sys, if (sys == "psx") "disc" else "cart", if (sys == "psx") "cd" else sys, "$name.bin", "US")
        }
        val times = ArrayList<Long>(names.size)
        for ((sys, name) in names) {
            val t0 = System.nanoTime()
            LabelMatch.match(index, sys, if (sys == "psx") "disc" else "cart", if (sys == "psx") "cd" else sys, "$name.bin", "US")
            times += (System.nanoTime() - t0) / 1000 // microseconds
        }
        times.sort()
        fun p(q: Double) = times[((times.size - 1) * q).toInt()]
        val total = times.sumOf { it }
        println("perf: ${times.size} cards, median ${p(0.5)} us, p95 ${p(0.95)} us, max ${times.last()} us, " +
            "screenful(24) ${total * 24 / times.size} us total")
        assertTrue("the fuzzy path ran", times.size >= 100)
    }

    private fun mutate(key: String): String {
        if (key.length < 4) return key + "x"
        val cut = key.length / 2
        return key.substring(0, cut) + key.substring(cut + 1) // drop one character: forces the fuzzy path
    }
}
