package com.phobos.emulator.util

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.IntentCompat
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** One APK a release carries, as its update.json lists it. */
data class UpdateApk(val name: String, val size: Long, val sha256: String)

/**
 * A release's update.json, which CI writes beside the APKs (.github/scripts/stage-apks.sh): the build's
 * version code and name, and the APK of each flavor ("legacy", "modern").
 */
data class UpdateManifest(val versionCode: Long, val versionName: String, val apks: Map<String, UpdateApk>)

/** A GitHub release, with its assets' download URLs by file name. */
data class UpdateRelease(val tag: String, val prerelease: Boolean, val assets: Map<String, String>)

/** An update for this install: the release, its manifest, and the APK for the installed flavor. */
data class AppUpdate(val release: UpdateRelease, val manifest: UpdateManifest, val apk: UpdateApk, val apkUrl: String)

internal const val MANIFEST_NAME = "update.json"

/** Releases from the GitHub API's release list, drafts left out. */
internal fun parseReleases(json: String): List<UpdateRelease> {
    val releases = JSONArray(json)
    return (0 until releases.length()).mapNotNull { i ->
        val release = releases.optJSONObject(i)?.takeUnless { it.optBoolean("draft") } ?: return@mapNotNull null
        val assets = release.optJSONArray("assets") ?: JSONArray()
        UpdateRelease(
            tag = release.optString("tag_name"),
            prerelease = release.optBoolean("prerelease"),
            assets = (0 until assets.length()).mapNotNull { assets.optJSONObject(it) }
                .associate { it.optString("name") to it.optString("browser_download_url") },
        )
    }
}

internal fun parseManifest(json: String): UpdateManifest? = runCatching {
    val root = JSONObject(json)
    val apks = root.getJSONObject("apks")
    UpdateManifest(
        versionCode = root.getLong("versionCode"),
        versionName = root.getString("versionName"),
        apks = apks.keys().asSequence().associateWith { flavor ->
            val apk = apks.getJSONObject(flavor)
            UpdateApk(apk.getString("name"), apk.getLong("size"), apk.getString("sha256").lowercase())
        },
    )
}.getOrNull()

/**
 * The releases whose manifests are worth reading, newest first: those that have one, releases only unless
 * [nightly]. A second one covers a newest release whose upload didn't finish.
 */
internal fun candidateReleases(releases: List<UpdateRelease>, nightly: Boolean): List<UpdateRelease> =
    releases.filter { MANIFEST_NAME in it.assets && (nightly || !it.prerelease) }.take(2)

/** The newest of [releases] (each with its manifest) above [installedCode] that has an APK for [flavor]. */
internal fun pickUpdate(releases: List<Pair<UpdateRelease, UpdateManifest>>, installedCode: Long, flavor: String): AppUpdate? =
    releases.mapNotNull { (release, manifest) ->
        val apk = manifest.apks[flavor] ?: return@mapNotNull null
        val url = release.assets[apk.name] ?: return@mapNotNull null
        AppUpdate(release, manifest, apk, url).takeIf { manifest.versionCode > installedCode }
    }.maxByOrNull { it.manifest.versionCode }

/**
 * Phobos's own updates from its GitHub releases: [check], [download], then [install], which hands the APK to
 * Android's installer; the user confirms there, as for any update installed outside a store.
 */
object AppUpdater {
    private const val RELEASES = "https://api.github.com/repos/pwnedbygary/phobos/releases?per_page=20"

    private val _installFailures = MutableSharedFlow<String>(extraBufferCapacity = 1)
    /** Why an install Android was handed didn't happen (a successful update replaces the process instead). */
    val installFailures: SharedFlow<String> = _installFailures.asSharedFlow()

    /** The update for this install, or null when it is the newest build; throws when GitHub can't be read. */
    fun check(installedCode: Long, flavor: String, nightly: Boolean): AppUpdate? {
        val releases = parseReleases(get(RELEASES))
        val withManifests = candidateReleases(releases, nightly).mapNotNull { release ->
            parseManifest(get(release.assets.getValue(MANIFEST_NAME)))?.let { release to it }
        }
        return pickUpdate(withManifests, installedCode, flavor)
    }

    /**
     * Downloads the update's APK into [dir] (replacing earlier downloads) and checks it: its size and SHA-256
     * against the manifest, and that it is this app at the manifest's version code.
     */
    fun download(context: Context, update: AppUpdate, dir: File, onProgress: (Float) -> Unit): File {
        dir.mkdirs()
        dir.listFiles()?.forEach { it.delete() }
        val file = File(dir, update.apk.name)
        DriverDownloader.download(update.apkUrl, file) { done, total -> onProgress(if (total > 0) done.toFloat() / total else -1f) }
        check(file.length() == update.apk.size) { "The download is incomplete. Try again." }
        check(sha256(file) == update.apk.sha256) { "The download doesn't match its checksum. Try again." }
        val pm = context.packageManager
        val archive = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) pm.getPackageArchiveInfo(file.path, PackageManager.PackageInfoFlags.of(0))
            else @Suppress("DEPRECATION") pm.getPackageArchiveInfo(file.path, 0)
        val code = archive?.let { if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) it.longVersionCode else @Suppress("DEPRECATION") it.versionCode.toLong() }
        check(archive?.packageName == context.packageName && code == update.manifest.versionCode) {
            "The download isn't the Phobos build it should be."
        }
        return file
    }

    fun install(context: Context, apk: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
            setSize(apk.length())
        }
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("phobos.apk", 0, apk.length()).use { out ->
                apk.inputStream().use { it.copyTo(out) }
                session.fsync(out)
            }
            val status = Intent(context, UpdateInstallReceiver::class.java)
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
            session.commit(PendingIntent.getBroadcast(context, id, status, flags).intentSender)
        }
    }

    internal fun installFailed(message: String) {
        _installFailures.tryEmit(message)
    }

    private fun get(url: String): String {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            setRequestProperty("Accept", "application/vnd.github+json")
            setRequestProperty("User-Agent", "Phobos")
            connectTimeout = 15_000
            readTimeout = 15_000
        }
        try {
            when (val code = conn.responseCode) {
                HttpURLConnection.HTTP_OK -> return conn.inputStream.bufferedReader().readText()
                HttpURLConnection.HTTP_FORBIDDEN, 429 -> throw IllegalStateException("GitHub's hourly limit for checks was reached. Try again later.")
                else -> throw IllegalStateException("GitHub answered $code. Try again later.")
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                digest.update(buffer, 0, read)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
}

/** Android's installer reports here: it shows its confirmation, or says why the update didn't install. */
class UpdateInstallReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION ->
                IntentCompat.getParcelableExtra(intent, Intent.EXTRA_INTENT, Intent::class.java)
                    ?.let { context.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
            PackageInstaller.STATUS_SUCCESS -> {}
            PackageInstaller.STATUS_FAILURE_ABORTED -> AppUpdater.installFailed("The update was cancelled.")
            else -> AppUpdater.installFailed(
                intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE) ?: "Android didn't install the update.",
            )
        }
    }
}
