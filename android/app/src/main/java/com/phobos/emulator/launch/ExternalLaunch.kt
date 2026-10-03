package com.phobos.emulator.launch

import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.util.Log
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.util.m3uEntries
import com.phobos.emulator.util.m3uEntryPath
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream

/** A game another app asked Phobos to start: the ROM, and the system it named, if any. */
data class LaunchRequest(val uri: Uri, val systemHint: String?)

/** Extras that carry the ROM when it doesn't come as the intent's data (RetroArch's, Argosy's path keys). */
private val ROM_EXTRAS = listOf("rom", "ROM", "romPath", "path", "file", "filePath", "uri")

/** Extras that name the system: Phobos's name for it or a frontend's (see [LaunchSystems.systemForName]). */
private val SYSTEM_EXTRAS = listOf("system", "platform")

private const val ENTRIES_TO_READ = 16

/**
 * The game [intent] asks for, or null when it asks for none (the launcher, or a task restored from recents,
 * whose old grant is gone). The ROM is the intent's data, else one of [ROM_EXTRAS]; a plain path counts as a
 * file.
 */
fun launchRequestOf(intent: Intent): LaunchRequest? {
    if (intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY != 0) return null
    val uri = intent.data?.let(::romUri) ?: ROM_EXTRAS.firstNotNullOfOrNull { romExtra(intent, it) } ?: return null
    val hint = SYSTEM_EXTRAS.firstNotNullOfOrNull { key -> intent.getStringExtra(key)?.takeIf { it.isNotBlank() } }
    return LaunchRequest(uri, hint)
}

private fun romExtra(intent: Intent, key: String): Uri? {
    @Suppress("DEPRECATION")
    return when (val value = intent.extras?.get(key)) {
        is Uri -> romUri(value)
        is String -> value.takeIf { it.isNotBlank() }?.let { romUri(Uri.parse(it)) }
        else -> null
    }
}

private fun romUri(uri: Uri): Uri? = when (uri.scheme) {
    "content", "file" -> uri
    null -> uri.path?.takeIf { it.startsWith("/") }?.let { Uri.fromFile(File(it)) }
    else -> null
}

/** What a [LaunchRequest] came to. */
sealed interface LaunchTarget {
    data class Ready(val system: String, val rom: RomFile) : LaunchTarget
    /** The system isn't clear: the user picks from [candidates]. */
    data class ChooseSystem(val rom: RomFile, val candidates: List<String>) : LaunchTarget
    /** Phobos can't read the file: no grant came with it and no folder Phobos holds contains it. */
    data class Unreadable(val name: String) : LaunchTarget
}

/**
 * Resolves [request] against the Library: [systems] and [extensions] as the core lists them, [libraryRoots] the
 * ROM folders (tree URIs) the user gave each system. The ROM keeps its file name, as the Library names it, so
 * its saves and states are the ones the Library's copy uses.
 */
suspend fun resolveLaunch(
    context: Context,
    request: LaunchRequest,
    systems: List<String>,
    extensions: Map<String, List<String>>,
    libraryRoots: Map<String, Set<String>>,
): LaunchTarget = withContext(Dispatchers.IO) {
    val resolver = context.contentResolver
    val primaryRoot = Environment.getExternalStorageDirectory().absolutePath
    val uri = request.uri
    val path = LaunchSystems.filesystemPath(uri.scheme, uri.authority, uri.pathSegments, uri.path, primaryRoot)
    val fallbackName = path?.substringAfterLast('/') ?: uri.lastPathSegment?.substringAfterLast('/') ?: "game"

    val readable = readableUri(resolver, uri, path, primaryRoot)
        ?: return@withContext LaunchTarget.Unreadable(fallbackName)
    val name = displayName(resolver, readable) ?: fallbackName
    val folders = libraryRoots.mapValues { (_, trees) ->
        trees.mapNotNull { treePath(Uri.parse(it), primaryRoot) }
    }
    val match = LaunchSystems.resolve(name, path, request.systemHint, systems, extensions, folders) {
        archiveEntries(resolver, readable)
    }
    val file = RomFile(name, readable, parentFolder(resolver, readable, path, primaryRoot))
    // A playlist starts the game it gathers, as the Library lists it, so the saves, states and disc changes match.
    val rom = if (path != null && name.endsWith(".m3u", ignoreCase = true)) {
        val entries = playlistDiscs(resolver, readable, path, primaryRoot)
        val discs = entries.mapNotNull { it.second }
        if (entries.isEmpty() || discs.size < entries.size) {
            return@withContext LaunchTarget.Unreadable(entries.firstOrNull { it.second == null }?.first ?: name)
        }
        RomFile(name, discs.first().uri, file.parentUri, discs)
    } else file
    when (match) {
        is LaunchSystems.Match.Found -> LaunchTarget.Ready(match.system, rom)
        is LaunchSystems.Match.Ask -> LaunchTarget.ChooseSystem(rom, match.candidates)
    }
}

/**
 * [uri] if Phobos can open it, else the same file through a folder Phobos holds a grant for (the Library's ROM
 * folders): a path or a storage document that came without a grant opens the way the Library opens it.
 */
private fun readableUri(resolver: ContentResolver, uri: Uri, path: String?, primaryRoot: String): Uri? {
    if (canRead(resolver, uri)) return uri
    val docId = storageDocumentId(uri) ?: path?.let { LaunchSystems.pathToDocumentId(it, primaryRoot) } ?: return null
    return throughHeldFolders(resolver, docId).firstOrNull { canRead(resolver, it) }
}

/** The storage document [docId] as reached through each folder Phobos holds a read grant for that contains it. */
private fun throughHeldFolders(resolver: ContentResolver, docId: String): List<Uri> =
    resolver.persistedUriPermissions
        .filter { it.isReadPermission && DocumentsContract.isTreeUri(it.uri) }
        .map { it.uri }
        .filter { tree -> runCatching { LaunchSystems.treeContains(DocumentsContract.getTreeDocumentId(tree), docId) }.getOrDefault(false) }
        .map { tree -> DocumentsContract.buildDocumentUriUsingTree(tree, docId) }

private fun canRead(resolver: ContentResolver, uri: Uri): Boolean =
    try {
        resolver.openFileDescriptor(uri, "r")?.use { true } ?: false
    } catch (e: Exception) {
        Log.i("Phobos", "Launch: can't open $uri directly (${e.javaClass.simpleName})")
        false
    }

private fun storageDocumentId(uri: Uri): String? =
    if (uri.authority == "com.android.externalstorage.documents") runCatching { DocumentsContract.getDocumentId(uri) }.getOrNull() else null

private fun displayName(resolver: ContentResolver, uri: Uri): String? {
    if (uri.scheme == "file") return uri.path?.substringAfterLast('/')?.takeIf { it.isNotEmpty() }
    return runCatching {
        resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0)?.takeIf { it.isNotBlank() } else null
        }
    }.getOrNull()
}

private fun treePath(tree: Uri, primaryRoot: String): String? =
    runCatching { LaunchSystems.documentIdToPath(DocumentsContract.getTreeDocumentId(tree), primaryRoot) }.getOrNull()

/**
 * The discs the .m3u playlist at [path] lists, each paired with its file name: where its entry leads from the
 * playlist's folder, or null when Phobos can't read it there.
 */
private fun playlistDiscs(resolver: ContentResolver, playlist: Uri, path: String, primaryRoot: String): List<Pair<String, RomFile?>> {
    val text = runCatching { resolver.openInputStream(playlist)?.bufferedReader()?.use { it.readText() } }.getOrNull()
        ?: return emptyList()
    return m3uEntries(text).map { entry ->
        val discPath = m3uEntryPath(path, entry)
        val discName = discPath.substringAfterLast('/')
        val uri = readableUri(resolver, Uri.fromFile(File(discPath)), discPath, primaryRoot)
        discName to uri?.let { RomFile(discName, it, parentFolder(resolver, it, discPath, primaryRoot)) }
    }
}

/**
 * The folder holding the game, through a folder Phobos holds, for the Neo Geo BIOS lookup next to it (see
 * MainViewModel.loadRom); null when Phobos holds no folder containing it.
 */
private fun parentFolder(resolver: ContentResolver, uri: Uri, path: String?, primaryRoot: String): Uri? {
    val docId = (if (DocumentsContract.isTreeUri(uri)) runCatching { DocumentsContract.getDocumentId(uri) }.getOrNull() else null)
        ?: path?.let { LaunchSystems.pathToDocumentId(it, primaryRoot) }
        ?: return null
    val slash = docId.lastIndexOf('/')
    val parent = if (slash >= 0) docId.substring(0, slash) else docId.substringBefore(':') + ":"
    return if (DocumentsContract.isTreeUri(uri)) DocumentsContract.buildDocumentUriUsingTree(uri, parent)
    else throughHeldFolders(resolver, parent).firstOrNull()
}

/** Names of the first entries of the zip at [uri]: from its central directory when the file can seek. */
private fun archiveEntries(resolver: ContentResolver, uri: Uri): List<String> =
    runCatching {
        resolver.openFileDescriptor(uri, "r")?.use { pfd ->
            ZipFile("/proc/self/fd/${pfd.fd}").use { zip ->
                zip.entries().asSequence().filterNot { it.isDirectory }.take(ENTRIES_TO_READ).map { it.name }.toList()
            }
        }
    }.getOrNull() ?: runCatching {
        resolver.openInputStream(uri)?.use { input ->
            ZipInputStream(input.buffered()).use { zip ->
                generateSequence { zip.nextEntry }.filterNot { it.isDirectory }.take(ENTRIES_TO_READ).map { it.name }.toList()
            }
        }
    }.getOrNull().orEmpty()
