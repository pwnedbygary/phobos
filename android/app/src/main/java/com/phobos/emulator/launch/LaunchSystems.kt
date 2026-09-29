package com.phobos.emulator.launch

/**
 * Finds the system for a game another app (a frontend such as ES-DE, Daijisho or Argosy) asked Phobos to
 * start, and the file-system path behind the URI it sent. Plain Kotlin for host tests; ExternalLaunch.kt reads
 * the intent and the files.
 */
object LaunchSystems {

    /** "ZX Spectrum 128" loads through the Library's single ZX Spectrum entry, which detects 128K tapes. */
    private const val ZX = "ZX Spectrum"
    private const val ZX_128 = "ZX Spectrum 128"

    /**
     * Names frontends use for each system, normalized: Phobos's own names, Argosy's platform slugs, ES-DE's
     * system names and Daijisho's platform short names (Daijisho calls the Master System "master" and has one
     * MSX platform for MSX and MSX2).
     */
    private val names: Map<String, String> = listOf(
        "Atari 2600" to listOf("atari2600", "a2600", "2600"),
        "ColecoVision" to listOf("colecovision", "coleco"),
        "Famicom" to listOf("famicom", "fc", "nes", "fds", "famicomdisksystem", "nintendoentertainmentsystem"),
        "Super Famicom" to listOf(
            "superfamicom", "sfc", "snes", "snesna", "supernes", "supernintendo", "supernintendoentertainmentsystem",
            "satellaview", "sufami",
        ),
        "Nintendo 64" to listOf("nintendo64", "n64", "n64dd", "64dd"),
        "Game Boy" to listOf("gameboy", "gb", "nintendogameboy"),
        "Game Boy Color" to listOf("gameboycolor", "gbc", "nintendogameboycolor"),
        "Game Boy Advance" to listOf("gameboyadvance", "gba", "nintendogameboyadvance"),
        "SG-1000" to listOf("sg1000", "segasg1000"),
        "Master System" to listOf("mastersystem", "sms", "master", "mark3", "segamastersystem"),
        "Mega Drive" to listOf("megadrive", "genesis", "md", "megadrivejp", "segagenesis", "segamegadrive"),
        "Game Gear" to listOf("gamegear", "gg", "segagamegear"),
        "Mega CD" to listOf("megacd", "segacd", "scd", "megacdjp"),
        "PlayStation" to listOf("playstation", "psx", "ps1", "sonyplaystation"),
        "Neo Geo" to listOf("neogeo", "neogeoaes", "neogeomvs"),
        "Neo Geo CD" to listOf("neogeocd", "neocd", "neogeocdjp"),
        "Neo Geo Pocket" to listOf("neogeopocket", "ngp"),
        "Neo Geo Pocket Color" to listOf("neogeopocketcolor", "ngpc"),
        ZX to listOf("zxspectrum", "zx", "zxs", "spectrum", "zxspectrum128"),
        "PC Engine" to listOf("pcengine", "pce", "tg16", "turbografx16"),
        "PC Engine CD" to listOf("pcenginecd", "pcecd", "tgcd", "turbografxcd"),
        "SuperGrafx" to listOf("supergrafx", "sgx"),
        "WonderSwan" to listOf("wonderswan", "ws"),
        "WonderSwan Color" to listOf("wonderswancolor", "wsc"),
        "MSX" to listOf("msx", "msx1"),
        "MSX2" to listOf("msx2"),
    ).flatMap { (system, keys) -> keys.map { it to system } }.toMap()

    /** Extensions two systems list where one is the usual meaning: a .gb file is a Game Boy game. */
    private val preferred = mapOf("gb" to "Game Boy", "ngc" to "Neo Geo Pocket Color")

    /** Archives every system's folder can hold (the Library lists .zip for all of them). */
    private val archives = setOf("zip", "7z")

    /** Files of a Neo Geo MVS/AES set (for example 242-p1.p1 and 242-c1.c1). */
    private val neoGeoSetParts = setOf("p1", "p2", "m1", "s1", "c1", "c2", "v1", "v2")

    private fun normalize(name: String) = name.lowercase().filter { it.isLetterOrDigit() }

    /** The Phobos system a frontend's name for it means (see [names]), or null. */
    fun systemForName(name: String?): String? = name?.let { names[normalize(it)] }

    fun extensionOf(fileName: String): String = fileName.substringAfterLast('.', "").lowercase()

    /** Library systems ("ZX Spectrum 128" folded into "ZX Spectrum"), in the order given. */
    fun librarySystems(systems: List<String>): List<String> = systems.map { if (it == ZX_128) ZX else it }.distinct()

    /** Systems by extension from the native table (system -> extensions), archives left out. */
    fun extensionIndex(extensions: Map<String, List<String>>): Map<String, Set<String>> {
        val index = mutableMapOf<String, MutableSet<String>>()
        for ((system, exts) in extensions) {
            val librarySystem = if (system == ZX_128) ZX else system
            for (ext in exts) {
                val key = ext.lowercase()
                if (key !in archives) index.getOrPut(key) { sortedSetOf() } += librarySystem
            }
        }
        return index
    }

    sealed interface Match {
        data class Found(val system: String) : Match
        /** No clear answer: the user picks from [candidates]. */
        data class Ask(val candidates: List<String>) : Match
    }

    /**
     * The system for [romName], trying in order: the frontend's [hint]; the Library folder the file is in
     * ([libraryFolders], system -> folder paths), so a game gets the same system, and so the same saves, as when
     * it is started from the Library; an extension only one system uses; the names of the folders above the file
     * (ES-DE and most setups keep each system in a folder named after it); the usual meaning of an extension two
     * systems share; the files inside a .zip ([archiveEntries], read only when needed). Otherwise the user is
     * asked, choosing between the systems that take the extension, or all of them.
     */
    fun resolve(
        romName: String,
        path: String?,
        hint: String?,
        systems: List<String>,
        extensions: Map<String, List<String>>,
        libraryFolders: Map<String, List<String>> = emptyMap(),
        archiveEntries: () -> List<String> = { emptyList() },
    ): Match {
        val available = librarySystems(systems).toSet()
        fun known(system: String?) = system?.takeIf { it in available }

        known(systemForName(hint))?.let { return Match.Found(it) }

        val ext = extensionOf(romName)
        val index = extensionIndex(extensions)
        if (path != null) {
            known(libraryFolderSystem(path, ext, libraryFolders, index))?.let { return Match.Found(it) }
        }
        fun unique(e: String) = index[e]?.singleOrNull()
        known(unique(ext))?.let { return Match.Found(it) }
        if (path != null) {
            for (folder in folderNames(path)) known(systemForName(folder))?.let { return Match.Found(it) }
        }
        known(preferred[ext])?.let { return Match.Found(it) }
        if (ext == "zip") {
            val entries = archiveEntries()
            val entryExts = entries.map { extensionOf(it.substringAfterLast('/')) }
            for (e in entryExts) known(unique(e) ?: preferred[e])?.let { return Match.Found(it) }
            if (entryExts.any { it in neoGeoSetParts }) known("Neo Geo")?.let { return Match.Found(it) }
        }
        val candidates = index[ext]?.filter { it in available }.orEmpty()
        return Match.Ask(candidates.ifEmpty { available.sorted() })
    }

    /**
     * The system whose Library folder holds [path] (the deepest folder when several do), if that system takes
     * [ext] as the Library's scan would: archives count for every system.
     */
    private fun libraryFolderSystem(
        path: String,
        ext: String,
        libraryFolders: Map<String, List<String>>,
        index: Map<String, Set<String>>,
    ): String? {
        val holders = libraryFolders.mapNotNull { (system, folders) ->
            val depth = folders.filter { isInside(path, it) }.maxOfOrNull { it.trimEnd('/').length } ?: return@mapNotNull null
            (if (system == ZX_128) ZX else system) to depth
        }
        val deepest = holders.maxOfOrNull { it.second } ?: return null
        val takesExt = holders.filter { it.second == deepest }.map { it.first }.distinct()
            .filter { ext in archives || it in index[ext].orEmpty() }
        return takesExt.singleOrNull()
    }

    private fun isInside(path: String, folder: String): Boolean {
        val base = folder.trimEnd('/')
        return base.isNotEmpty() && path.startsWith("$base/")
    }

    /** Names of the folders above the file at [path], nearest first, at most [levels]. */
    fun folderNames(path: String, levels: Int = 3): List<String> =
        path.trimEnd('/').split('/').dropLast(1).filter { it.isNotEmpty() }.takeLast(levels).reversed()

    // ── Storage paths and document IDs ──────────────────────────────────────────────────────────────

    private const val EXTERNAL_STORAGE_DOCUMENTS = "com.android.externalstorage.documents"
    private const val DOWNLOADS_DOCUMENTS = "com.android.providers.downloads.documents"

    /**
     * The shared-storage path of an external storage document ID: "primary:ROMs/n64" is under [primaryRoot],
     * "EBFF-F6C0:ROMs" under /storage/EBFF-F6C0, "home:x" in Documents; null for other IDs.
     */
    fun documentIdToPath(docId: String, primaryRoot: String): String? {
        val colon = docId.indexOf(':')
        if (colon <= 0) return null
        val volume = docId.substring(0, colon)
        val relative = docId.substring(colon + 1).trim('/')
        val base = when (volume) {
            "primary" -> primaryRoot
            "home" -> "$primaryRoot/Documents"
            else -> "/storage/$volume"
        }
        return if (relative.isEmpty()) base else "$base/$relative"
    }

    /** The external storage document ID of a shared-storage [path] (the reverse of [documentIdToPath]). */
    fun pathToDocumentId(path: String, primaryRoot: String): String? {
        val root = primaryRoot.trimEnd('/')
        val normalized = listOf("/sdcard", "/storage/self/primary", "/mnt/sdcard").fold(path) { p, alias ->
            if (p == alias || p.startsWith("$alias/")) root + p.removePrefix(alias) else p
        }
        if (normalized == root || normalized.startsWith("$root/")) {
            return "primary:" + normalized.removePrefix(root).trimStart('/')
        }
        if (!normalized.startsWith("/storage/")) return null
        val rest = normalized.removePrefix("/storage/")
        val volume = rest.substringBefore('/')
        if (volume.isEmpty() || volume == "emulated" || volume == "self") return null
        return volume + ":" + rest.substringAfter('/', "")
    }

    /** Whether the tree [treeDocId] holds the document [docId] (a volume's root tree ends with ':'). */
    fun treeContains(treeDocId: String, docId: String): Boolean =
        docId == treeDocId || docId.startsWith("$treeDocId/") || (treeDocId.endsWith(':') && docId.startsWith(treeDocId))

    /**
     * Where the file behind a launch URI lives, when that can be told: [pathSegments] and [path] are the URI's
     * decoded segments and path. file:// URIs and bare paths are paths already; an external storage document
     * maps through its ID, and a Downloads document only when its ID is a raw path; for other providers
     * (FileProvider URIs such as Argosy's content://…fileprovider/root/storage/emulated/0/…) the path is taken
     * from where /storage/ starts.
     */
    fun filesystemPath(scheme: String?, authority: String?, pathSegments: List<String>, path: String?, primaryRoot: String): String? {
        when (scheme) {
            "file", null -> return path?.takeIf { it.startsWith("/") }
            "content" -> {}
            else -> return null
        }
        if (authority == EXTERNAL_STORAGE_DOCUMENTS || authority == DOWNLOADS_DOCUMENTS) {
            val docId = when {
                pathSegments.size >= 4 && pathSegments[0] == "tree" && pathSegments[2] == "document" -> pathSegments[3]
                pathSegments.size >= 2 && (pathSegments[0] == "document" || pathSegments[0] == "tree") -> pathSegments[1]
                else -> null
            } ?: return null
            return if (authority == EXTERNAL_STORAGE_DOCUMENTS) documentIdToPath(docId, primaryRoot)
            else docId.takeIf { it.startsWith("raw:/") }?.removePrefix("raw:")
        }
        val p = path ?: return null
        val storage = p.indexOf("/storage/")
        if (storage >= 0) return p.substring(storage)
        val sdcard = p.indexOf("/sdcard/")
        if (sdcard >= 0) return primaryRoot.trimEnd('/') + p.substring(sdcard + "/sdcard".length)
        return null
    }
}
