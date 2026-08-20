package com.phobos.emulator.util

import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * GitHub-backed GPU driver downloader (Turnip / AdrenoTools / Mesa builds).
 *
 * Mirrors the source list and flow used by Mupen64PlusAE-Turnip, adapted to
 * Phobos: drivers are plain .so packages (or .zip/.apk containing a .so) that
 * the core loads via the custom-driver path. Network calls use the built-in
 * HttpURLConnection + org.json so no extra dependency is required.
 */
data class DriverSource(
    val name: String,
    val description: String,
    val owner: String,
    val repo: String
)

data class DriverAsset(
    val tag: String,
    val name: String,
    val url: String,
    val size: Long,
    val publishedAt: String
)

/** GitHub repos that publish Adreno/Mesa Turnip driver builds. */
val DRIVER_SOURCES = listOf(
    DriverSource("K11MCH1", "Classic AdrenoTools releases (Turnip v26.x, Qualcomm)", "K11MCH1", "AdrenoToolsDrivers"),
    DriverSource("StevenMXZ", "Scheduled Turnip builds (v26.2.x, Gen8)", "StevenMXZ", "Adreno-Tools-Drivers"),
    DriverSource("Banners-Turnip", "Daily Mesa main builds (v26.3.0)", "The412Banner", "Banners-Turnip"),
    DriverSource("Mr. Purple", "purple-turnip builds", "MrPurple666", "purple-turnip"),
    DriverSource("Whitebelyash", "freedreno CI builds", "whitebelyash", "AdrenoToolsDrivers"),
    DriverSource("nihui", "mesa-turnip-android-driver builds", "nihui", "mesa-turnip-android-driver"),
)

object DriverDownloader {
    private const val TAG = "PhobosDriver"
    private const val API = "https://api.github.com"

    /**
     * Fetches driver assets (zip/apk/adpkg) from the most recent releases of a
     * repo. Throws on HTTP/parse failure so the caller can surface a toast.
     */
    fun fetchAssets(source: DriverSource): List<DriverAsset> {
        val url = URL("$API/repos/${source.owner}/${source.repo}/releases?per_page=10")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            setRequestProperty("Accept", "application/vnd.github+json")
            setRequestProperty("User-Agent", "Phobos")
            connectTimeout = 15000
            readTimeout = 15000
        }
        try {
            val code = conn.responseCode
            if (code != HttpURLConnection.HTTP_OK) {
                val body = runCatching { conn.inputStream.bufferedReader().readText() }.getOrDefault("")
                throw RuntimeException("GitHub API $code${if (body.isNotBlank()) ": $body" else ""}")
            }
            val releases = JSONArray(conn.inputStream.bufferedReader().readText())
            val assets = mutableListOf<DriverAsset>()
            for (i in 0 until releases.length()) {
                val rel = releases.optJSONObject(i) ?: continue
                val tag = rel.optString("tag_name", "unknown")
                val published = rel.optString("published_at", "")
                val arr = rel.optJSONArray("assets") ?: continue
                for (j in 0 until arr.length()) {
                    val a = arr.optJSONObject(j) ?: continue
                    val an = a.optString("name", "")
                    if (an.endsWith(".zip", true) || an.endsWith(".apk", true) || an.endsWith(".adpkg", true)) {
                        assets.add(
                            DriverAsset(
                                tag = tag,
                                name = an,
                                url = a.optString("browser_download_url", ""),
                                size = a.optLong("size", 0),
                                publishedAt = published
                            )
                        )
                    }
                }
            }
            return assets
        } finally {
            conn.disconnect()
        }
    }

    /**
     * Streams a driver package to [dest], reporting [downloaded]/[total] bytes
     * (total is -1 when the server omits Content-Length).
     */
    fun download(url: String, dest: File, onProgress: (downloaded: Long, total: Long) -> Unit) {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            setRequestProperty("User-Agent", "Phobos")
            connectTimeout = 15000
            readTimeout = 60000
        }
        try {
            val code = conn.responseCode
            if (code != HttpURLConnection.HTTP_OK) {
                throw RuntimeException("Download HTTP $code")
            }
            val total = conn.contentLengthLong.coerceAtLeast(-1)
            var downloaded = 0L
            conn.inputStream.use { input ->
                FileOutputStream(dest).use { out ->
                    val buf = ByteArray(8192)
                    var read: Int
                    while (input.read(buf).also { read = it } != -1) {
                        out.write(buf, 0, read)
                        downloaded += read
                        onProgress(downloaded, total)
                    }
                }
            }
            Log.i(TAG, "Downloaded ${dest.name} ($downloaded bytes)")
        } finally {
            conn.disconnect()
        }
    }
}
