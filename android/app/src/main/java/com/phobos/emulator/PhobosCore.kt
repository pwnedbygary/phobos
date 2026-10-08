package com.phobos.emulator

import android.view.Surface

object PhobosCore {
    init {
        System.loadLibrary("phobos_android")
    }

    external fun stringFromJNI(): String
    external fun enumerateSystems(): List<String>
    external fun getSystemExtensions(systemName: String): List<String>
    external fun loadRom(systemName: String, uriString: String, romName: String): Boolean
    /** Why the last [loadRom] failed, when the game's medium said (a sentence for the player); empty otherwise. */
    external fun loadProblem(): String
    external fun loadSecondaryRom(systemName: String, uriString: String): Boolean
    external fun unloadSystem()
    external fun setEmulationRunning(running: Boolean)
    external fun setPause(paused: Boolean)
    external fun setFastForward(enabled: Boolean)
    external fun setFastForwardSpeed(speed: Float)
    external fun setNgcdLoadSpeed(speed: Int)
    external fun setZxLoadSpeed(speed: Int)
    external fun setMsxLoadSpeed(speed: Int)
    external fun setZxTapeAuto(enabled: Boolean)
    external fun setN64DebugLogging(enabled: Boolean)
    external fun setN64Upscale(factor: Int)
    external fun setN64Recompiler(enabled: Boolean)
    external fun setCustomDriverPath(path: String)
    external fun setPs1AnalogMode(enabled: Boolean)
    external fun togglePs1AnalogMode(): Boolean
    external fun setN64ExpansionPak(enabled: Boolean)
    external fun setN64DisableVIProcessing(enabled: Boolean)
    external fun setN64WeaveDeinterlacing(enabled: Boolean)
    external fun setN64SupersampleScanout(enabled: Boolean)
    external fun setN64ViOverclock(percent: Int)
    external fun setN64CountPerOp(value: Int)
    external fun setN64CpuOverclock(factor: Int)
    /** Asynchronous RDP: SyncFull stops waiting for the GPU (faster, less accurate). Applies immediately. */
    external fun setN64AsyncRdp(enabled: Boolean)
    external fun setN64FasterSync(enabled: Boolean)
    external fun setN64SkipCaches(enabled: Boolean)
    external fun setN64RspTaskMode(enabled: Boolean)
    external fun setPinFastestCore(enabled: Boolean)
    external fun setBusyWaitPacing(enabled: Boolean)
    /** Settings > Video; applies to the loaded game immediately. */
    external fun setVideoSettings(overscan: Boolean, colorEmulation: Boolean, interframeBlending: Boolean)
    external fun setN64Pak(pakName: String)
    external fun getRumbleState(): Boolean
    external fun resetSystem()
    external fun frameAdvance()
    external fun dumpNgGfx(dir: String)
    external fun setMuteAudio(muted: Boolean)
    external fun setShader(path: String): Boolean
    external fun saveState(path: String): Boolean
    /**
     * Saves a state to [path]: 1, or 0 when there's none to save (a game that ended by itself, such as a PSP program
     * that left, or a core stuck in a frame for two seconds), or -1 when it couldn't be written.
     */
    external fun trySaveState(path: String): Int
    external fun loadState(path: String): Boolean
    /** The loaded game's battery saves, one "name\tsize\tpath" entry each (N64: save.eeprom, save.ram, save.flash, save.pak). */
    external fun getSaveFiles(): Array<String>
    /** The loaded game's player-one buttons, one "bit\tname" entry per pad bit that presses a button. */
    external fun getButtonNames(): Array<String>
    /** Writes every battery save, the Controller Pak's included, to disk now. */
    external fun flushSaves()
    external fun takeScreenshot(path: String): Boolean
    external fun setFastBoot(enabled: Boolean)
    external fun setSkipBootRom(enabled: Boolean)
    external fun setRegion(regionIndex: Int)
    external fun setLogLevel(level: Int)
    external fun setRomFd(fd: Int)
    external fun setSecondaryRomFd(fd: Int)
    /** A PSP disc's title, disc ID, region and icon (ICON0.PNG) read from its image; null if it isn't a PSP disc. */
    external fun pspDiscInfo(fd: Int): PspDiscInfo?
    /** A file the next load (or disc change) opens by path where it is, instead of copying the descriptor's. */
    external fun setRomPath(path: String)
    external fun setSecondaryRomPath(path: String)
    external fun setTempFilePath(path: String)
    external fun setLoadDiskImageToRam(enabled: Boolean)
    external fun setOrientationMode(vertical: Boolean)
    external fun setHomePath(path: String)
    external fun setSavesPath(path: String)
    /** The folder name for the next game's PS1 memory cards: its name without the disc number. */
    external fun setMemoryCardKey(key: String)
    /** The PSP's memory stick: a folder the user picked, or "" for the shared one in the saves folder. */
    external fun setPspMemoryStickPath(path: String)
    /** The PSP's system fonts: the folder of the user's own copies (util/PspFonts.kt), or "" for none. */
    external fun setPspFontsPath(path: String)
    /** How many threads draw the PSP's pictures (the core's "GE Threads", as a game starts): 0 for Auto. */
    external fun setPspDrawingThreads(threads: Int)
    /** The whole multiple native code draws the PSP's picture at (util/PspVideo.kt's pictureMultiple()). */
    external fun setPictureMultiple(multiple: Int)
    external fun setVulkanCachePath(path: String)
    external fun setNativeLibraryDir(path: String)
    external fun setFirmwarePath(path: String)
    external fun mapFirmwareFile(name: String, path: String)
    /** Firmware keys [systemName] can't start without that aren't mapped ("fw_mcd": any Mega CD BIOS). */
    external fun missingFirmware(systemName: String): List<String>
    /** The sides of the LaserActive disc being played (its .mmi's media), in order; empty for other systems. */
    external fun getLaserdiscSides(): List<String>
    /** Puts [side] of the LaserActive disc in the tray, or takes the disc out for ""; false without one. */
    external fun setLaserdiscSide(side: String): Boolean
    external fun setSurface(surface: Any?)

    // The surface the core draws to. A game screen can replace another (a frontend starting a game over the
    // running one), and the old screen can lose its surface after the new screen attached its own.
    private var attachedSurface: Surface? = null

    fun attachSurface(surface: Surface) {
        attachedSurface = surface
        setSurface(surface)
    }

    /** Whether the core draws to [surface], rather than to a screen's that replaced it. */
    fun drawsTo(surface: Surface): Boolean = attachedSurface === surface

    /** Sends [surface] again after a size change, if the core still draws to it. */
    fun refreshSurface(surface: Surface) {
        if (drawsTo(surface)) setSurface(surface)
    }

    fun detachSurface(surface: Surface) {
        if (!drawsTo(surface)) return
        attachedSurface = null
        setSurface(null)
    }

    external fun isFirstFrameRendered(): Boolean
    external fun getBlacklistedPipelineCount(): Int
    external fun getNewLogs(): List<LogEntry>
    external fun setInput(lx: Float, ly: Float, rx: Float, ry: Float, buttons: Int)
    external fun setKeyboardKey(label: String, pressed: Boolean)
    external fun playTape(): Boolean
    external fun setZxControlScheme(scheme: Int)
    external fun setZxStickToKeys(enabled: Boolean)
    external fun setZxReversePitch(enabled: Boolean)
    external fun setZxKeyBinding(label: String, bit: Int)
    external fun setZxTapeMuted(muted: Boolean)
    /** The ZX Spectrum tape: [in (1 or 0), playing (1 or 0), position ms, length ms]. */
    external fun getZxTapeState(): IntArray
    /** Plays the tape from where it stands (from its start once it has run out), or stops it. */
    external fun setZxTapePlaying(play: Boolean)
    /** Stops the tape at its start. */
    external fun rewindZxTape()
    /**
     * The MSX tape, as [getZxTapeState] gives the ZX Spectrum's, then [data tape (1 or 0), recording armed (1 or 0),
     * recording (1 or 0)]; the MSX's motor relay plays and stops it.
     */
    external fun getMsxTapeState(): IntArray
    /** Winds the MSX tape back to its start; it plays from there if the motor is on. */
    external fun rewindMsxTape()
    /** Puts the game's data tape (a .wav with its saves) in the MSX's deck, or the game's own tape back. */
    external fun setMsxTape(data: Boolean)
    /** Arms recording onto the data tape: the motor relay then records what the MSX saves, at the tape's end. */
    external fun setMsxTapeRecord(armed: Boolean)
    external fun getPerformanceStats(): PerformanceStats
    /** Recent frame-to-frame intervals in ms, oldest first (up to 240). */
    external fun getFrameTimes(): FloatArray
    /** Logical [width, height] of the last frame after the core's pixel-aspect correction; 0s before the first frame. */
    external fun getVideoGeometry(): FloatArray
    /** The running core's refresh-rate hint (Hz), updated as soon as ares reports it. */
    external fun getRefreshRateHint(): Double

    object Input {
        const val UP       = 1 shl 0
        const val DOWN     = 1 shl 1
        const val LEFT     = 1 shl 2
        const val RIGHT    = 1 shl 3
        const val A        = 1 shl 4
        const val B        = 1 shl 5
        const val X        = 1 shl 6
        const val Y        = 1 shl 7
        const val L1       = 1 shl 8
        const val R1       = 1 shl 9
        const val L2       = 1 shl 10
        const val R2       = 1 shl 11
        const val L3       = 1 shl 12
        const val R3       = 1 shl 13
        const val SELECT   = 1 shl 14
        const val START    = 1 shl 15
        const val HOME     = 1 shl 16
        const val LS_UP    = 1 shl 17
        const val LS_DOWN  = 1 shl 18
        const val LS_LEFT  = 1 shl 19
        const val LS_RIGHT = 1 shl 20
        const val RS_UP    = 1 shl 21
        const val RS_DOWN  = 1 shl 22
        const val RS_LEFT  = 1 shl 23
        const val RS_RIGHT = 1 shl 24
    }
}

enum class LogLevel {
    TRACE, DEBUG, INFO, WARN, ERROR, FATAL, NONE
}

data class LogEntry(
    val level: Int,
    val message: String
)

data class PerformanceStats(
    val fps: Double,
    /** Average emulation work time per frame (ms), excluding pacing sleep. */
    val frameTime: Double,
    /** CPU core the emulation thread last ran on (-1 before the first frame). */
    val activeCore: Int,
    val pipelineFailures: Int = 0,
    val isAdrenoDriver: Boolean = false,
    /** Emulation thread id, for per-thread CPU sampling (0 before the first frame). */
    val emuTid: Int = 0,
    /** The running core's native refresh rate. */
    val targetFps: Double = 60.0,
    /** Unpaused time played since the game was loaded, in ms. */
    val playTimeMs: Long = 0,
)

data class PhobosSetting(
    val name: String,
    val type: String,
    val value: String,
    val options: List<String> = emptyList()
)

data class PspDiscInfo(
    val title: String,
    val discId: String,
    val region: String,
    val icon: ByteArray
)
