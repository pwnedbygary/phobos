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
    external fun loadSecondaryRom(systemName: String, uriString: String): Boolean
    external fun unloadSystem()
    external fun setEmulationRunning(running: Boolean)
    external fun setPause(paused: Boolean)
    external fun setFastForward(enabled: Boolean)
    external fun setFastForwardSpeed(speed: Float)
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
    external fun setN64Pak(pakName: String)
    external fun setCdSpeed(speed: Int)
    external fun getRumbleState(): Boolean
    external fun resetSystem()
    external fun frameAdvance()
    external fun setMuteAudio(muted: Boolean)
    external fun setShader(path: String): Boolean
    external fun saveState(path: String): Boolean
    external fun loadState(path: String): Boolean
    external fun takeScreenshot(path: String): Boolean
    external fun setFastBoot(enabled: Boolean)
    external fun setSkipBootRom(enabled: Boolean)
    external fun setRegion(regionIndex: Int)
    external fun setLogLevel(level: Int)
    external fun setRomFd(fd: Int)
    external fun setSecondaryRomFd(fd: Int)
    external fun setTempFilePath(path: String)
    external fun setLoadDiskImageToRam(enabled: Boolean)
    external fun setOrientationMode(vertical: Boolean)
    external fun setHomePath(path: String)
    external fun setSavesPath(path: String)
    external fun setVulkanCachePath(path: String)
    external fun setNativeLibraryDir(path: String)
    external fun setFirmwarePath(path: String)
    external fun mapFirmwareFile(name: String, path: String)
    external fun setSurface(surface: Any?)
    external fun isFirstFrameRendered(): Boolean
    external fun getBlacklistedPipelineCount(): Int
    external fun getNewLogs(): List<LogEntry>
    external fun setInput(lx: Float, ly: Float, rx: Float, ry: Float, buttons: Int)
    external fun setKeyboardKey(label: String, pressed: Boolean)
    external fun playTape(): Boolean
    external fun setTapeSpeed(speed: Int)
    external fun setZxControlScheme(scheme: Int)
    external fun setZxStickToKeys(enabled: Boolean)
    external fun setZxReversePitch(enabled: Boolean)
    external fun setZxKeyBinding(label: String, bit: Int)
    external fun setZxTapeMuted(muted: Boolean)
    external fun getZxTapeProgress(): Int
    external fun getPerformanceStats(): PerformanceStats

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
    val frameTime: Double,
    val activeCore: Int,
    val pipelineFailures: Int = 0,
    val isAdrenoDriver: Boolean = false
)

data class PhobosSetting(
    val name: String,
    val type: String,
    val value: String,
    val options: List<String> = emptyList()
)
