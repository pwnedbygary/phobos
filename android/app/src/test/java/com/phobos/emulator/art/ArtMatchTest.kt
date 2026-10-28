package com.phobos.emulator.art

import com.phobos.emulator.data.RegionPreference
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The port against Cartridge's own matching: `cartridge-matches.json` and `listing.html` are what Cartridge 0.4.9.3's
 * scraper.js and titles.js made of a hand-written listing (tools/art-fixtures/make-fixtures.mjs).
 */
class ArtMatchTest {
    private fun resource(name: String) = javaClass.getResource("/art/$name")!!.readText()
    private val fixture = JSONObject(resource("cartridge-matches.json"))
    private fun strings(key: String) = fixture.getJSONArray(key).let { a -> List(a.length()) { a.getString(it) } }

    @Test fun theListingParsesAsCartridgeParsesIt() {
        val names = parseArtListing(resource("listing.html"))
        assertEquals(strings("listing"), names)
        assertEquals("a non-ASCII name is decoded", true, "Pokémon Stadium (USA)" in names)
    }

    @Test fun aPageWithNoPicturesParsesToNothing() {
        assertEquals(emptyList<String>(), parseArtListing("<html><body>Bad gateway</body></html>"))
    }

    @Test fun matchKeysAreCartridges() {
        val keys = fixture.getJSONArray("keys")
        for (i in 0 until keys.length()) {
            val case = keys.getJSONObject(i)
            assertEquals(case.getString("name"), case.getString("key"), artMatchKey(case.getString("name")))
        }
    }

    @Test fun sanitizedNamesAreCartridges() {
        val cases = fixture.getJSONArray("sanitized")
        for (i in 0 until cases.length()) {
            val case = cases.getJSONObject(i)
            assertEquals(case.getString("sanitized"), sanitizeArtName(case.getString("name")))
        }
    }

    @Test fun eachLookupFindsWhatCartridgeFindsButWhereTheNumbersDiffer() {
        val listing = ArtListing(strings("listing"))
        val matches = fixture.getJSONArray("matches")
        for (i in 0 until matches.length()) {
            val case = matches.getJSONObject(i)
            val base = case.getString("base")
            val order = case.getJSONArray("order").let { a -> List(a.length()) { a.getString(it) } }
            val cartridge = if (case.isNull("cartridge")) null else case.getString("cartridge")
            val found = matchArt(listing, base, order)
            if (case.optBoolean("guard")) {
                assertNotNull("Cartridge took another game for $base", cartridge)
                assertNull("Phobos takes no game with other numbers for $base", found)
            } else {
                assertEquals(base, cartridge, found)
            }
        }
    }

    @Test fun withoutTheLooseStepOnlyANameOrItsKeyMatches() {
        val listing = ArtListing(listOf("Contra Force (USA)"))
        val us = listOf("US", "W", "JP", "EU")
        assertEquals("Contra Force (USA)", matchArt(listing, "Contra Forcee", us))
        assertNull(matchArt(listing, "Contra Forcee", us, fuzzy = false))
        assertEquals("Contra Force (USA)", matchArt(listing, "Contra Force (Japan)", us, fuzzy = false))
    }

    @Test fun anEmptyListingMatchesNothing() {
        assertNull(matchArt(ArtListing(emptyList()), "Contra", listOf("US")))
    }

    @Test fun theRegionOrderFollowsTheConsoleRegionsWithWorldSecond() {
        assertEquals(listOf("US", "W", "JP", "EU"), artRegionOrder(RegionPreference.NTSC_U_NTSC_J_PAL))
        assertEquals(listOf("US", "W", "EU", "JP"), artRegionOrder(RegionPreference.NTSC_U_PAL_NTSC_J))
        assertEquals(listOf("JP", "W", "US", "EU"), artRegionOrder(RegionPreference.NTSC_J_NTSC_U_PAL))
        assertEquals(listOf("JP", "W", "EU", "US"), artRegionOrder(RegionPreference.NTSC_J_PAL_NTSC_U))
        assertEquals(listOf("EU", "W", "US", "JP"), artRegionOrder(RegionPreference.PAL_NTSC_U_NTSC_J))
        assertEquals(listOf("EU", "W", "JP", "US"), artRegionOrder(RegionPreference.PAL_NTSC_J_NTSC_U))
    }

    @Test fun urlsAreEncodedAsJavaScriptEncodesThem() {
        // Expected strings from Node's encodeURIComponent, as Cartridge builds them.
        val folder = "https://thumbnails.libretro.com/Sega%20-%20SG-1000"
        assertEquals(
            "$folder/Named_Boxarts/Black%20Onyx%2C%20The%20(Japan)%20%5B!%5D.png",
            pictureUrl("Sega - SG-1000", ArtKind.BOX, "Black Onyx, The (Japan) [!]"),
        )
        assertEquals("Pok%C3%A9mon%20Stadium%20(USA)", encodeUriComponent("Pokémon Stadium (USA)"))
        assertEquals("$folder/Named_Logos/", listingUrl("Sega - SG-1000", ArtKind.LOGO))
    }

    @Test fun aBadEscapeDecodesToNothing() {
        assertNull(decodeUriComponent("Broken%E0%A4%A"))
        assertNull(decodeUriComponent("Bad%ZZ"))
        assertEquals("a b", decodeUriComponent("a%20b"))
    }
}
