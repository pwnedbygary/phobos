package com.phobos.emulator.art

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Compares [LabelMatch] with Cartridge's own scraper.js answers on the trimmed bundled index. */
class LabelMatchTest {

    private val text = javaClass.getResource("/carts/labelmatch-fixtures.json")!!.readText()
    private val fixture = JSONObject(text)
    private val index = LabelMatch.parse(text)

    @Test
    fun baseMatchesTheBundledIndex() {
        assertEquals(fixture.getString("base"), index.base)
        assertEquals("https://images.launchbox-app.com/", index.base)
    }

    @Test
    fun labelCasesMatchCartridge() {
        val cases = fixture.getJSONArray("cases")
        for (i in 0 until cases.length()) {
            val c = cases.getJSONObject(i)
            val sys = c.getJSONObject("system")
            val media = sys.optJSONObject("media") ?: JSONObject()
            val actual = LabelMatch.match(
                index,
                sys.getString("id"),
                media.optString("type").ifEmpty { null },
                media.optString("shape").ifEmpty { null },
                c.getString("file"),
                c.getString("region"),
            )
            if (c.isNull("expected")) {
                assertNull("case $i (${c.optString("label")})", actual)
            } else {
                val e = c.getJSONObject("expected")
                val urls = e.getJSONArray("urls")
                val expectedUrls = (0 until urls.length()).map { urls.getString(it) }
                assertEquals("case $i urls (${c.optString("label")})", expectedUrls, actual?.urls)
                assertEquals("case $i shape (${c.optString("label")})", e.getString("shape"), actual?.shape)
            }
        }
    }

    @Test
    fun everyExpectedUrlStaysOnTheNamedHost() {
        val cases = fixture.getJSONArray("cases")
        for (i in 0 until cases.length()) {
            val c = cases.getJSONObject(i)
            if (c.isNull("expected")) continue
            val urls = c.getJSONObject("expected").getJSONArray("urls")
            for (j in 0 until urls.length()) {
                val url = urls.getString(j)
                assertEquals("case $i url $j", true, url.startsWith("https://images.launchbox-app.com/"))
            }
        }
    }
}
