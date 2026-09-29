package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AppUpdatesTest {
    private fun asset(name: String) = """{"name": "$name", "browser_download_url": "https://example.com/$name"}"""

    // The API's release list, newest first: a nightly, a draft, a release, and v1.1.0 from before
    // releases carried update.json.
    private val releasesJson = """
        [
          {"tag_name": "nightly-104430", "prerelease": true, "draft": false, "assets": [
            ${asset("Phobos-1.2.0-5-gabc1234-Legacy.apk")}, ${asset("Phobos-1.2.0-5-gabc1234-Modern.apk")}, ${asset("update.json")}]},
          {"tag_name": "v9.9.9", "prerelease": false, "draft": true, "assets": [${asset("update.json")}]},
          {"tag_name": "v1.2.0", "prerelease": false, "draft": false, "assets": [
            ${asset("Phobos-1.2.0-Legacy.apk")}, ${asset("Phobos-1.2.0-Modern.apk")}, ${asset("update.json")}]},
          {"tag_name": "v1.1.0", "prerelease": false, "draft": false, "assets": [
            ${asset("Phobos-1.1.0-Legacy.apk")}, ${asset("Phobos-1.1.0-Modern.apk")}]}
        ]
    """.trimIndent()

    // As .github/scripts/stage-apks.sh writes it.
    private fun manifestJson(code: Long, name: String) = """
        {
          "versionCode": $code,
          "versionName": "$name",
          "channel": "nightly",
          "commit": "abc1234",
          "apks": {
            "legacy": {"name": "Phobos-$name-Legacy.apk", "size": 24107427, "sha256": "D456CFDA5EE71A078D25733F5453D93B43825E3FB75C3D0834FDF00D5C9F648E"},
            "modern": {"name": "Phobos-$name-Modern.apk", "size": 24067395, "sha256": "6da440e39dc3ec450ba282b3a6db311280a3d2dfbfa5b99d487f1ae058ebeb4e"}
          }
        }
    """.trimIndent()

    private val releases = parseReleases(releasesJson)
    private val nightly = releases.first { it.tag == "nightly-104430" }
    private val stable = releases.first { it.tag == "v1.2.0" }

    @Test fun releasesAreParsedWithoutDrafts() {
        assertEquals(listOf("nightly-104430", "v1.2.0", "v1.1.0"), releases.map { it.tag })
        assertEquals(listOf(true, false, false), releases.map { it.prerelease })
        assertEquals("https://example.com/update.json", stable.assets["update.json"])
        assertEquals(3, stable.assets.size)
    }

    @Test fun manifestsAreParsed() {
        val manifest = parseManifest(manifestJson(104430, "1.2.0-5-gabc1234"))!!
        assertEquals(104430L, manifest.versionCode)
        assertEquals("1.2.0-5-gabc1234", manifest.versionName)
        assertEquals(UpdateApk("Phobos-1.2.0-5-gabc1234-Modern.apk", 24067395, "6da440e39dc3ec450ba282b3a6db311280a3d2dfbfa5b99d487f1ae058ebeb4e"), manifest.apks["modern"])
        assertEquals("d456cfda5ee71a078d25733f5453d93b43825e3fb75c3d0834fdf00d5c9f648e", manifest.apks["legacy"]?.sha256)
        assertNull(parseManifest("not json"))
        assertNull(parseManifest("""{"versionCode": 1, "versionName": "x"}"""))
    }

    @Test fun releasesOnlyCountWhenTheyHaveAManifest() {
        assertEquals(listOf("v1.2.0"), candidateReleases(releases, nightly = false).map { it.tag })
        assertEquals(listOf("nightly-104430", "v1.2.0"), candidateReleases(releases, nightly = true).map { it.tag })
    }

    @Test fun theNewestBuildAboveTheInstalledOneIsTheUpdate() {
        val candidates = listOf(
            nightly to parseManifest(manifestJson(104430, "1.2.0-5-gabc1234"))!!,
            stable to parseManifest(manifestJson(104425, "1.2.0"))!!,
        )
        val update = pickUpdate(candidates, installedCode = 104421, flavor = "modern")!!
        assertEquals("nightly-104430", update.release.tag)
        assertEquals("https://example.com/Phobos-1.2.0-5-gabc1234-Modern.apk", update.apkUrl)
        assertEquals("1.2.0", pickUpdate(candidates.drop(1), installedCode = 104421, flavor = "legacy")?.manifest?.versionName)
        // Not newer than what is installed.
        assertNull(pickUpdate(candidates, installedCode = 104430, flavor = "modern"))
    }

    @Test fun anUpdateNeedsTheInstalledFlavorsApk() {
        val manifest = parseManifest(manifestJson(104430, "1.2.0-5-gabc1234"))!!
        assertNull(pickUpdate(listOf(nightly to manifest), installedCode = 1, flavor = "future"))
        // The manifest names an APK the release doesn't carry (an upload that didn't finish).
        val unfinished = nightly.copy(assets = nightly.assets - "Phobos-1.2.0-5-gabc1234-Modern.apk")
        assertNull(pickUpdate(listOf(unfinished to manifest), installedCode = 1, flavor = "modern"))
    }
}
