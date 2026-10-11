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

    const val PSP = "PlayStation Portable"

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
        "Super Game Boy" to listOf("supergameboy", "sgb", "sgb1", "sgb2"),
        "Arcade" to listOf("arcade", "aleck64", "mame", "sg1000a"),
        "Mega LD" to listOf("megald", "laseractive", "laseractivesega", "segapac", "pioneerlaseractive"),
        "PC Engine LD" to listOf("pcengineld", "laseractivenec", "necpac", "ldrom2", "pceld"),
        "Nintendo 64" to listOf("nintendo64", "n64", "n64dd", "64dd"),
        "Game Boy" to listOf("gameboy", "gb", "nintendogameboy"),
        "Game Boy Color" to listOf("gameboycolor", "gbc", "nintendogameboycolor"),
        "Game Boy Advance" to listOf("gameboyadvance", "gba", "nintendogameboyadvance"),
        "SG-1000" to listOf("sg1000", "segasg1000"),
        "Master System" to listOf("mastersystem", "sms", "master", "mark3", "segamastersystem"),
        "Mega Drive" to listOf("megadrive", "genesis", "md", "megadrivejp", "segagenesis", "segamegadrive"),
        "Mega 32X" to listOf(
            "mega32x", "32x", "sega32x", "sega32xjp", "sega32xna", "sega32", "segamegadrive32x", "segagenesis32x",
            "segasuper32x",
        ),
        "Game Gear" to listOf("gamegear", "gg", "segagamegear"),
        "Mega CD" to listOf("megacd", "segacd", "scd", "megacdjp"),
        "Mega CD 32X" to listOf("megacd32x", "segacd32x", "sega32xcd", "32xcd", "cd32x"),
        "PlayStation" to listOf("playstation", "psx", "ps1", "sonyplaystation"),
        "PlayStation Portable" to listOf("playstationportable", "psp", "sonypsp", "sonyplaystationportable"),
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
        "Pocket Challenge V2" to listOf("pocketchallengev2", "pocketchallenge", "pcv2", "benessepocketchallengev2"),
        "MSX" to listOf("msx", "msx1"),
        "MSX2" to listOf("msx2"),
    ).flatMap { (system, keys) -> keys.map { it to system } }.toMap()

    /** Extensions two systems list where one is the usual meaning: a .gb file is a Game Boy game. */
    private val preferred = mapOf("gb" to "Game Boy", "ngc" to "Neo Geo Pocket Color")

    /** Cartridge systems whose CD add-on's discs frontends file with them (ES-DE's 32X folders take CD 32X games). */
    private val cdAddOns = mapOf("Mega 32X" to "Mega CD 32X")

    /** Archives every system's folder can hold (the Library lists .zip for all of them). */
    private val archives = setOf("zip", "7z")

    /** Files of a Neo Geo MVS/AES set (for example 242-p1.p1 and 242-c1.c1). */
    private val neoGeoSetParts = setOf("p1", "p2", "m1", "s1", "c1", "c2", "v1", "v2")

    /** Files that mark a MAME Aleck64 set (parent BIOS or game) inside a .zip. */
    private val aleck64SetParts = setOf("pifdata.bin", "normpnt.rom", "normslp.rom")

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
     * asked, choosing between the systems that take the extension, or all of them. A disc image the hint or a
     * folder name puts under the 32X is a CD 32X game.
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
        val ext = extensionOf(romName)
        // A disc filed under a cartridge system is its CD add-on's game.
        fun named(name: String?): String? {
            val system = systemForName(name)
            val cd = system?.let { cdAddOns[it] } ?: return system
            return if (ext !in extensions[system].orEmpty() && ext in extensions[cd].orEmpty()) cd else system
        }

        known(named(hint))?.let { return Match.Found(it) }

        val index = extensionIndex(extensions)
        if (path != null) {
            known(libraryFolderSystem(path, ext, libraryFolders, index))?.let { return Match.Found(it) }
        }
        fun unique(e: String) = index[e]?.singleOrNull()
        known(unique(ext))?.let { return Match.Found(it) }
        if (path != null) {
            for (folder in folderNames(path)) known(named(folder))?.let { return Match.Found(it) }
        }
        known(preferred[ext])?.let { return Match.Found(it) }
        if (ext == "zip") {
            val entries = archiveEntries()
            val entryExts = entries.map { extensionOf(it.substringAfterLast('/')) }
            for (e in entryExts) known(unique(e) ?: preferred[e])?.let { return Match.Found(it) }
            if (entryExts.any { it in neoGeoSetParts }) known("Neo Geo")?.let { return Match.Found(it) }
            val baseNames = entries.map { it.substringAfterLast('/').lowercase() }
            if (baseNames.any { it in aleck64SetParts }) known("Arcade")?.let { return Match.Found(it) }
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

    /**
     * The name of the folder holding the file [fileName] a launch URI points at, when it can be told: from where it
     * is on shared storage ([filesystemPath]), from a storage document's ID ("primary:ROMs/psp/Cube/EBOOT.PBP"), or,
     * for another app's provider, from the URI's own path when that ends in the file's name
     * (content://some.provider/files/Cube/EBOOT.PBP); null when nothing says, as for a document whose ID is only a
     * number, or a provider whose path is IDs (content://media/external/file/123).
     */
    fun parentFolderName(
        scheme: String?, authority: String?, pathSegments: List<String>, path: String?, primaryRoot: String,
        fileName: String,
    ): String? {
        filesystemPath(scheme, authority, pathSegments, path, primaryRoot)?.let {
            return folderNames(it, levels = 1).firstOrNull()
        }
        if (scheme != "content") return null
        val documentId = when {
            pathSegments.size >= 4 && pathSegments[0] == "tree" && pathSegments[2] == "document" -> pathSegments[3]
            pathSegments.size >= 2 && (pathSegments[0] == "document" || pathSegments[0] == "tree") -> pathSegments[1]
            else -> return pathSegments.takeIf { it.lastOrNull().equals(fileName, ignoreCase = true) }
                ?.dropLast(1)?.lastOrNull()
        }
        val relative = documentId.substringAfter(':', "")
        return if ('/' in relative) folderNames(relative, levels = 1).firstOrNull() else null
    }

    /** Names of the folders above the file at [path], nearest first, at most [levels]. */
    fun folderNames(path: String, levels: Int = 3): List<String> =
        path.trimEnd('/').split('/').dropLast(1).filter { it.isNotEmpty() }.takeLast(levels).reversed()

    /**
     * What Phobos calls a PSP program, from its file's [fileName] and the [folderName] it sits in. Homebrew comes
     * as an EBOOT.PBP in a folder named after the program ("Cube/EBOOT.PBP"), and states and other per-game
     * files go by the name, so every program would share them: an EBOOT.PBP takes its folder's name instead
     * ("Cube.pbp"), keeping the extension the core and the launch go by. Only for the PSP: PlayStation games
     * that come as an EBOOT.PBP keep their name, and with it their memory cards and states.
     */
    fun pspProgramName(fileName: String, folderName: String?): String =
        if (fileName.equals("EBOOT.PBP", ignoreCase = true) && !folderName.isNullOrBlank()) "$folderName.pbp"
        else fileName

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
