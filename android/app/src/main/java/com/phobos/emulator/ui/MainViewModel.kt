package com.phobos.emulator.ui

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.DocumentsContract
import android.util.Log
import android.widget.Toast
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.phobos.emulator.LogEntry
import com.phobos.emulator.LogLevel
import com.phobos.emulator.PerformanceStats
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.ActiveControls
import com.phobos.emulator.input.ControlCapture
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.input.Controls
import com.phobos.emulator.input.GameInputState
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.PixelBackdropScene
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.data.SettingsStore
import com.phobos.emulator.data.ThemeMode
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.launch.LaunchRequest
import com.phobos.emulator.launch.LaunchTarget
import com.phobos.emulator.launch.resolveLaunch
import com.phobos.emulator.ui.hud.HudEdit
import com.phobos.emulator.ui.hud.HudItem
import com.phobos.emulator.ui.hud.HudPosition
import com.phobos.emulator.ui.hud.HudPreset
import com.phobos.emulator.ui.hud.encodeHudFractions
import com.phobos.emulator.ui.hud.hudConfig
import com.phobos.emulator.ui.touch.ElementOverride
import com.phobos.emulator.ui.touch.TouchFamily
import com.phobos.emulator.ui.touch.TouchLayoutCodec
import com.phobos.emulator.ui.touch.TouchPrefs
import com.phobos.emulator.ui.touch.touchLayoutKey
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.CRC32
import java.util.zip.ZipInputStream
import kotlin.math.roundToInt

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import com.phobos.emulator.BuildConfig
import com.phobos.emulator.util.AppUpdate
import com.phobos.emulator.util.AppUpdater
import com.phobos.emulator.util.DriverAsset
import com.phobos.emulator.util.DriverDownloader
import com.phobos.emulator.util.DriverSource
import com.phobos.emulator.util.N64SaveFormat
import com.phobos.emulator.util.N64SaveImportPlan
import com.phobos.emulator.util.N64SaveKind
import com.phobos.emulator.util.N64SaveRead
import com.phobos.emulator.util.N64SaveTransfer
import com.phobos.emulator.util.ZxTape
import com.phobos.emulator.util.cueTracks
import com.phobos.emulator.util.cueWithTracksIn
import com.phobos.emulator.util.groupDiscSets
import com.phobos.emulator.util.m3uEntries
import com.phobos.emulator.util.m3uEntryPath
import com.phobos.emulator.util.newerDriverRelease
import com.phobos.emulator.util.romTitle
import com.phobos.emulator.util.withoutDiscNumber
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow

data class InstalledDriver(val name: String, val path: String, val source: String, val tag: String)

/** Logical picture size after the core's pixel-aspect correction (e.g. SNES ≈ 292.6 x 224, GBA 240 x 160). */
data class VideoGeometry(val width: Float, val height: Float) {
    val aspect: Float get() = width / height
}

/** A filled save-state slot: its preview (null for states saved before previews existed) and save time. */
data class StateSlotPreview(val image: Bitmap?, val savedAtMillis: Long)

/** The game left paused behind the Library (Pause menu > Library, or its hotkey), with its last frame when captured. */
data class RunningGame(val systemName: String, val romName: String, val frame: Bitmap?)
private data class DriverSidecar(val owner: String, val repo: String, val tag: String)

/** A game another app asked for, waiting on the user to pick its system. */
data class LaunchChoice(val rom: RomFile, val candidates: List<String>)

/** A game that wasn't started because its [system] lacks the firmware [keys] ([PhobosCore.missingFirmware]). */
data class FirmwareRequired(val system: String, val keys: List<String>)

/** Where an update of Phobos itself stands (Settings → About). */
sealed interface AppUpdateState {
    data object Idle : AppUpdateState
    data object Checking : AppUpdateState
    data class UpToDate(val checkedAt: Long) : AppUpdateState
    data class Available(val update: AppUpdate) : AppUpdateState
    /** [progress] is 0..1, or negative when the size isn't known. */
    data class Downloading(val update: AppUpdate, val progress: Float) : AppUpdateState
    /** Handed to Android's installer, which asks the user to confirm. */
    data class Installing(val update: AppUpdate) : AppUpdateState
    data class Failed(val message: String, val update: AppUpdate?) : AppUpdateState
}

/** Save files picked for import into the running N64 game: what they would change, and the ones that can't be used. */
data class SaveImportPreview(
    val plan: N64SaveImportPlan,
    val unreadable: List<N64SaveRead.Unreadable>,
    /** A Mupen64Plus SRAM or FlashRAM file was picked, whose byte order the user can override. */
    val offersCartridgeOrder: Boolean,
)

/** How exporting the running game's save went. */
sealed interface SaveExportResult {
    data class Done(val files: List<String>) : SaveExportResult
    /** Files already in the folder, which aren't replaced without asking. */
    data class Existing(val files: List<String>) : SaveExportResult
    data class Failed(val reason: String) : SaveExportResult
}

/**
 * The game the native core holds. The core is one per process but MainViewModel is one per activity, and a
 * frontend's clear-task launch replaces the activity while a game runs, so loads and unloads from every
 * instance take [lock], and only the instance that loaded the game ([owner]) unloads it on its way out.
 * Unloads run in [scope], which outlives the ViewModel that started them.
 */
private object CoreSession {
    val lock = Mutex()
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    @Volatile var system = ""
    @Volatile var rom = ""
    @Volatile var owner: Any? = null
}

class MainViewModel(
    private val context: Context,
    private val settingsStore: SettingsStore,
    initialSettings: EmulatorSettings = EmulatorSettings(),
) : ViewModel(), DefaultLifecycleObserver {
    val settings: StateFlow<EmulatorSettings> = settingsStore.settings
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), initialSettings)

    // The loaded game, as the level its controls resolve at; null with none loaded.
    private val _loadedGame = MutableStateFlow<ControlLevel.Game?>(null)
    val loadedGame: StateFlow<ControlLevel.Game?> = _loadedGame.asStateFlow()

    /**
     * The button bindings and hotkeys every input path reads: the loaded game's, else the global
     * ones. Resolved once per change, and kept current with no screen watching, since key handling
     * reads it outside composition.
     */
    val activeControls: StateFlow<ActiveControls> = combine(settings, _loadedGame) { s, game ->
        ActiveControls.at(game ?: ControlLevel.AllConsoles, s.inputMappings, s.hotkeys, s.controlOverrides)
    }.stateIn(
        viewModelScope, SharingStarted.Eagerly,
        ActiveControls.at(ControlLevel.AllConsoles, initialSettings.inputMappings, initialSettings.hotkeys, initialSettings.controlOverrides),
    )

    /** The press being captured for a binding, in Settings or the pause menu. */
    val controlCapture = ControlCapture(viewModelScope)

    // Driver download progress (downloaded, total); total = -1 when unknown.
    private val _downloadProgress = MutableStateFlow(Pair(-1L, -1L))
    val downloadProgress: StateFlow<Pair<Long, Long>> = _downloadProgress.asStateFlow()

    // One-shot events surfaced to the UI as Toasts.
    private val _driverUpdateEvent = MutableSharedFlow<String>()
    val driverUpdateEvent = _driverUpdateEvent.asSharedFlow()
    private val _driverErrorEvent = MutableSharedFlow<String>()
    val driverErrorEvent = _driverErrorEvent.asSharedFlow()
    private val _driverSuccessEvent = MutableSharedFlow<String>()
    val driverSuccessEvent = _driverSuccessEvent.asSharedFlow()

    companion object {
        // Task 17: slot index reserved for the "Auto" state (saved on unload,
        // loaded on boot). Any negative slot maps to <rom>.state.auto.
        const val AUTO_STATE_SLOT = -1

        // First launches unpack the core's assets before the systems are listed.
        private const val LAUNCH_READY_TIMEOUT_MS = 15_000L
        // A settings change made meanwhile moves [settings] past the stored values it waits for.
        private const val SETTINGS_READY_TIMEOUT_MS = 2_000L
        // "Once a day" with some slack, so a check around the same time each day isn't skipped.
        private const val APP_UPDATE_INTERVAL_MS = 20 * 3_600_000L

        private const val N64_APPLIES_ON_RESET = "Takes effect after Reset System or reloading the game"
        private const val N64_APPLIES_ON_RELOAD = "Takes effect the next time the game is loaded"
        private const val PREVIEW_MAX_WIDTH = 320
        // mia_temp's folder for the track files of the .cue sheets in play.
        private const val CUE_TRACKS = "cue_tracks"
        // CD systems whose mia media only read their discs (their save() writes nothing), so a disc can
        // load where it is instead of from a copy.
        private val IN_PLACE_SYSTEMS = setOf("PlayStation", "Mega CD", "Mega CD 32X", "PC Engine CD", "Neo Geo CD")
        // Systems whose disc native code can change while the game runs; the other CD systems read a disc from the start.
        private val DISC_SWAP_SYSTEMS = setOf("PlayStation")
        // The firmware keys each system's pak() in PhobosRunner.cpp reads, so a load copies only what its game can
        // use. Neo Geo's neogeo.zip is copied on its own.
        private val SYSTEM_FIRMWARE: Map<String, Set<String>> = run {
            val megaCd = setOf("fw_mcd_us", "fw_mcd_jp", "fw_mcd_eu")
            val mega32x = setOf("fw_32x_g", "fw_32x_m", "fw_32x_s")
            val pceCd = setOf("fw_pce_cd_3_jp", "fw_pce_cd_ge_jp")
            val gameBoy = setOf("fw_gb_boot", "fw_gbc_boot")
            val pocket = setOf("fw_ngp", "fw_ngpc")
            val zx = setOf("fw_zx48", "fw_zx128", "fw_zx128_sub")
            mapOf(
                "PlayStation" to setOf("fw_psx_us", "fw_psx_jp", "fw_psx_eu"),
                "Mega Drive" to megaCd,
                "Mega CD" to megaCd,
                "Mega 32X" to megaCd + mega32x,
                "Mega CD 32X" to megaCd + mega32x,
                "Nintendo 64" to setOf("fw_n64_pif_ntsc", "fw_n64_pif_pal", "fw_n64dd_us", "fw_n64dd_jp", "fw_n64dd_dev"),
                "Neo Geo CD" to setOf("fw_ng_cd"),
                "Neo Geo Pocket" to pocket,
                "Neo Geo Pocket Color" to pocket,
                "Game Boy" to gameBoy,
                "Game Boy Color" to gameBoy,
                "Game Boy Advance" to setOf("fw_gba"),
                "ColecoVision" to setOf("fw_coleco"),
                "PC Engine" to pceCd,
                "PC Engine CD" to pceCd,
                "SuperGrafx" to pceCd,
                "ZX Spectrum" to zx,
                "ZX Spectrum 128" to zx,
            )
        }
        // Systems whose pak() reads neogeo.zip (the BIOS, and the LSPC zoom table the Neo Geo CD shares).
        private val NEOGEO_ZIP_SYSTEMS = setOf("Neo Geo", "Neo Geo CD")
    }

    private var wasEmulationRunningBeforePause = false

    override fun onPause(owner: LifecycleOwner) {
        if (_isLoaded.value && !_isPaused.value) {
            wasEmulationRunningBeforePause = true
            setPause(true)
        } else {
            wasEmulationRunningBeforePause = false
        }
    }

    override fun onResume(owner: LifecycleOwner) {
        if (_isLoaded.value && wasEmulationRunningBeforePause) {
            setPause(false)
        }
    }

    fun setThemeMode(mode: ThemeMode) = viewModelScope.launch { settingsStore.setThemeMode(mode) }
    fun setTheme(id: String, followSystem: Boolean = false) = viewModelScope.launch { settingsStore.setTheme(id, followSystem) }
    fun setUiEffects(effects: UiEffects) = viewModelScope.launch { settingsStore.setUiEffects(effects) }
    fun setPixelBackdrop(scene: PixelBackdropScene) = viewModelScope.launch { settingsStore.setPixelBackdrop(scene) }
    fun setGlassEffects(level: GlassEffects) = viewModelScope.launch { settingsStore.setGlassEffects(level) }
    fun setRegionPreference(pref: RegionPreference) = viewModelScope.launch { settingsStore.setRegionPreference(pref) }
    fun setFastBoot(enabled: Boolean) = viewModelScope.launch { settingsStore.setFastBoot(enabled) }
    fun setMuteAudio(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setMuteAudio(enabled)
        PhobosCore.setMuteAudio(enabled)
    }
    fun setColorEmulation(enabled: Boolean) = viewModelScope.launch { settingsStore.setColorEmulation(enabled) }
    fun setInterframeBlending(enabled: Boolean) = viewModelScope.launch { settingsStore.setInterframeBlending(enabled) }
    fun setOverscan(enabled: Boolean) = viewModelScope.launch { settingsStore.setOverscan(enabled) }
    fun setAutoSaveState(enabled: Boolean) = viewModelScope.launch { settingsStore.setAutoSaveState(enabled) }
    fun setAutoLoadState(enabled: Boolean) = viewModelScope.launch { settingsStore.setAutoLoadState(enabled) }
    fun setN64Upscale(factor: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64Upscale(factor)
        PhobosCore.setN64Upscale(factor)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }

    // Most N64 Experimental options are only read by the core at load or reset
    // (PhobosRunner.cpp setN64*: changing the RDP scanout pipeline mid-frame can
    // deadlock the GPU fence). Say so while a game runs instead of appearing to
    // do nothing. Debounced so adjusting several options shows one toast.
    private var n64NoticeJob: kotlinx.coroutines.Job? = null
    private fun noticeN64SettingDeferred(message: String) {
        if (!_isLoaded.value || !currentSystemName.contains("Nintendo 64")) return
        n64NoticeJob?.cancel()
        n64NoticeJob = viewModelScope.launch(Dispatchers.Main) {
            delay(300)
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    /** Asynchronous RDP applies immediately (it only changes whether SyncFull waits for the GPU). */
    fun setN64AsyncRdp(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64AsyncRdp(enabled)
        PhobosCore.setN64AsyncRdp(enabled)
    }
    fun setN64FasterSync(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64FasterSync(enabled)
        PhobosCore.setN64FasterSync(enabled)
    }
    fun setN64SkipCaches(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64SkipCaches(enabled)
        PhobosCore.setN64SkipCaches(enabled)
    }
    fun setPinFastestCore(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setPinFastestCore(enabled)
        PhobosCore.setPinFastestCore(enabled)
    }
    fun setBusyWaitPacing(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setBusyWaitPacing(enabled)
        PhobosCore.setBusyWaitPacing(enabled)
    }
    fun setN64RspTaskMode(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64RspTaskMode(enabled)
        PhobosCore.setN64RspTaskMode(enabled)
    }
    fun setCustomDriverPath(path: String) = viewModelScope.launch {
        settingsStore.setCustomDriverPath(path)
        PhobosCore.setCustomDriverPath(path)
    }
    fun setPs1AnalogMode(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setPs1AnalogMode(enabled)
        PhobosCore.setPs1AnalogMode(enabled)
    }

    // Runtime DualShock analog toggle (like the physical Analog button).
    // Returns the NEW analog state (true=analog on), or null if no DualShock.
    fun togglePs1AnalogMode(): Boolean? {
        val newState = PhobosCore.togglePs1AnalogMode()
        viewModelScope.launch(Dispatchers.IO) {
            settingsStore.setPs1AnalogMode(newState)
        }
        return newState
    }
    fun setOrientationMode(vertical: Boolean) = viewModelScope.launch {
        settingsStore.setOrientationMode(vertical)
        PhobosCore.setOrientationMode(vertical)
    }
    fun setN64ExpansionPak(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64ExpansionPak(enabled)
        PhobosCore.setN64ExpansionPak(enabled)
        // RDRAM is sized when the game loads (ares n64 System::load).
        noticeN64SettingDeferred(N64_APPLIES_ON_RELOAD)
    }

    /** A ZX game's control scheme: the game's own, else its system's, else Kempston. */
    fun zxControlScheme(settings: EmulatorSettings, system: String, game: String): Int =
        settings.zxControlScheme["$system/$game"] ?: settings.zxControlScheme[system] ?: 0

    // ZX control scheme per game, rebinds per core (Layer 2.5 — the CUSTOM scheme).
    fun setZxControlScheme(system: String, game: String, scheme: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxControlScheme("$system/$game", scheme)
        PhobosCore.setZxControlScheme(scheme)
    }
    fun setZxStickToKeys(system: String, enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxStickToKeys(system, enabled)
        PhobosCore.setZxStickToKeys(enabled)
    }
    fun setZxReversePitch(system: String, enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxReversePitch(system, enabled)
        PhobosCore.setZxReversePitch(enabled)
    }
    fun setZxKeyBinding(system: String, label: String, bit: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxKeyBinding(system, label, bit)
        PhobosCore.setZxKeyBinding(label, bit)
    }

    fun setZxKeyboardOpacity(opacity: Float) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxKeyboardOpacity(opacity)
    }

    fun setZxTapeMuted(muted: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxTapeMuted(muted)
        PhobosCore.setZxTapeMuted(muted)
    }
    fun setN64DisableVIProcessing(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64DisableVIProcessing(enabled)
        PhobosCore.setN64DisableVIProcessing(enabled)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64WeaveDeinterlacing(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64WeaveDeinterlacing(enabled)
        PhobosCore.setN64WeaveDeinterlacing(enabled)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64SupersampleScanout(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64SupersampleScanout(enabled)
        PhobosCore.setN64SupersampleScanout(enabled)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64ViOverclock(percent: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64ViOverclock(percent)
        PhobosCore.setN64ViOverclock(percent)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64UseDefaultCountPerOp(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64UseDefaultCountPerOp(enabled)
        // When "use default" is on, force the stock value (2) to native.
        PhobosCore.setN64CountPerOp(if (enabled) 2 else settings.value.n64CountPerOp)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64CountPerOp(value: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64CountPerOp(value)
        settingsStore.setN64UseDefaultCountPerOp(false)
        PhobosCore.setN64CountPerOp(value)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64UseDefaultCpuOverclock(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64UseDefaultCpuOverclock(enabled)
        // When "use default" is on, force the stock value (0) to native.
        PhobosCore.setN64CpuOverclock(if (enabled) 0 else settings.value.n64CpuOverclock)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64CpuOverclock(factor: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64CpuOverclock(factor)
        settingsStore.setN64UseDefaultCpuOverclock(false)
        PhobosCore.setN64CpuOverclock(factor)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }
    fun setN64Pak(pak: String) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64Pak(pak)
        PhobosCore.setN64Pak(pak)
    }
    fun setFastForwardSpeed(speed: Float) = viewModelScope.launch {
        settingsStore.setFastForwardSpeed(speed)
        PhobosCore.setFastForwardSpeed(speed)
    }
    fun setNgcdLoadSpeed(speed: Int) = viewModelScope.launch {
        settingsStore.setNgcdLoadSpeed(speed)
        PhobosCore.setNgcdLoadSpeed(speed)
    }
    fun setZxLoadSpeed(speed: Int) = viewModelScope.launch {
        settingsStore.setZxLoadSpeed(speed)
        PhobosCore.setZxLoadSpeed(speed)
    }
    fun setZxTapeAuto(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setZxTapeAuto(enabled)
        PhobosCore.setZxTapeAuto(enabled)
    }
    fun setFullScreenMode(enabled: Boolean) = viewModelScope.launch { settingsStore.setFullScreenMode(enabled) }
    fun setShowTouchControls(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowTouchControls(enabled) }
    fun updateTouchPrefs(transform: (TouchPrefs) -> TouchPrefs) = viewModelScope.launch {
        settingsStore.updateTouchPrefs(transform)
    }
    fun touchLayoutOverrides(family: TouchFamily, landscape: Boolean): Map<String, ElementOverride> =
        TouchLayoutCodec.decode(settings.value.touchLayouts[touchLayoutKey(family, landscape)])
    fun saveTouchLayout(family: TouchFamily, landscape: Boolean, overrides: Map<String, ElementOverride>) = viewModelScope.launch {
        settingsStore.setTouchLayout(touchLayoutKey(family, landscape), TouchLayoutCodec.encode(overrides))
    }
    fun resetTouchLayout(family: TouchFamily, landscape: Boolean) = viewModelScope.launch {
        settingsStore.setTouchLayout(touchLayoutKey(family, landscape), "")
    }
    fun setShowPerformanceMonitor(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowPerformanceMonitor(enabled) }
    fun setPerfShowFps(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowFps(enabled) }
    fun setPerfShowFrameTime(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowFrameTime(enabled) }
    fun setPerfShowRam(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowRam(enabled) }
    fun setPerfShowCore(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowCore(enabled) }
    fun setPerfShowShaderFails(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowShaderFails(enabled) }
    fun setPerfShowGraph(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowGraph(enabled) }
    fun setPerfShowCpu(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowCpu(enabled) }
    fun setPerfShowGpu(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowGpu(enabled) }
    fun setPerfShowBattery(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowBattery(enabled) }
    fun setPerfShowThermal(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowThermal(enabled) }
    fun setPerfShowSystem(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowSystem(enabled) }
    fun setPerfShowClock(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowClock(enabled) }
    fun setPerfHudHorizontal(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfHudHorizontal(enabled) }
    fun setPerfHudOrder(order: List<HudItem>) = viewModelScope.launch { settingsStore.setPerfHudOrder(HudItem.encodeOrder(order)) }
    fun setPerfHudOpacity(opacity: Float) = viewModelScope.launch { settingsStore.setPerfHudOpacity(opacity) }
    fun applyPerfHudPreset(preset: HudPreset) = viewModelScope.launch {
        settingsStore.setPerfHudMetrics(preset.applyTo(settings.value.hudConfig()))
    }
    fun setPerfOverlayScale(scale: Float) = viewModelScope.launch { settingsStore.setPerfOverlayScale(scale) }
    fun setPerfHudPosition(position: HudPosition) = viewModelScope.launch { settingsStore.setPerfHudPosition(position.name) }
    fun saveHudEdit(edit: HudEdit) = viewModelScope.launch {
        settingsStore.savePerfHudEdit(
            position = edit.position.name,
            landscape = edit.landscape,
            fractions = edit.custom?.let(::encodeHudFractions),
            size = edit.size?.encode(),
            scale = edit.scale,
        )
    }
    fun clearPerfHudSizes() = viewModelScope.launch { settingsStore.clearPerfHudSizes() }
    fun setLogVerbosity(level: LogLevel) = viewModelScope.launch {
        settingsStore.setLogVerbosity(level)
        PhobosCore.setLogLevel(level.ordinal)
    }

    fun setN64DebugLogging(enabled: Boolean) {
        // Push to native SYNCHRONOUSLY (not on a dispatcher) so the emulation
        // thread sees it immediately — the pause-menu toggle previously
        // appeared dead because Dispatchers.IO could delay the JNI call until
        // a re-toggle. Native atomic is the source of truth for the stats
        // block; persist is best-effort.
        PhobosCore.setN64DebugLogging(enabled)
        viewModelScope.launch(Dispatchers.IO) {
            settingsStore.setN64DebugLogging(enabled)
        }
    }

    fun setPause(paused: Boolean) {
        _isPaused.value = paused
        PhobosCore.setPause(paused)
        // Controller input goes to the menus while paused, so a button held now would stay down for the game.
        if (paused) GameInputState.releaseAllButtons()
        // Back in the game, a stick or trigger still held from a binding is the game's again.
        else controlCapture.cancel()
    }

    private val _tabSteps = MutableSharedFlow<Int>(extraBufferCapacity = 4)
    /** Steps through the dock's tabs from L1/R1 (MainActivity), for MainScaffold: -1 back, +1 on. */
    val tabSteps: SharedFlow<Int> = _tabSteps.asSharedFlow()
    fun stepTab(step: Int) { _tabSteps.tryEmit(step) }

    fun addSystemRomPath(system: String, path: String) = viewModelScope.launch {
        settingsStore.addSystemRomPath(system, path)
    }

    fun removeSystemRomPath(system: String, path: String) = viewModelScope.launch {
        settingsStore.removeSystemRomPath(system, path)
    }

    fun setSystemRomPath(system: String, path: String) = viewModelScope.launch {
        settingsStore.setSystemRomPath(system, path)
    }

    fun setSystemFirmwarePath(system: String, path: String) = viewModelScope.launch {
        settingsStore.setSystemFirmwarePath(system, path)
    }

    fun clearAllFirmware() = viewModelScope.launch {
        settings.value.systemFirmwarePaths.keys.forEach { key ->
            settingsStore.setSystemFirmwarePath(key, "")
        }
    }

    fun setFirmwarePath(path: String) = viewModelScope.launch { settingsStore.setGlobalPath(SettingsStore.FIRMWARE_PATH, path) }
    fun setSavesPath(path: String) = viewModelScope.launch { settingsStore.setGlobalPath(SettingsStore.SAVES_PATH, path) }
    fun setStatesPath(path: String) = viewModelScope.launch { settingsStore.setGlobalPath(SettingsStore.STATES_PATH, path) }
    fun setScreenshotsPath(path: String) = viewModelScope.launch { settingsStore.setGlobalPath(SettingsStore.SCREENSHOTS_PATH, path) }

    /**
     * Vulkan Cache Path (Task 40): persists the SAF URI and pushes the resolved
     * real path to native. On change, copies any existing pipeline cache from the
     * old location so the user doesn't lose their warm shader cache.
     */
    fun setVulkanCachePath(path: String) = viewModelScope.launch(Dispatchers.IO) {
        val oldPath = settings.value.vulkanCachePath
        settingsStore.setVulkanCachePath(path)
        val newReal = resolveSafPath(path)
        if (newReal != null) {
            // Copy-on-change: carry the existing cache (+ driver-UUID sidecar) over.
            if (oldPath.isNotEmpty() && oldPath != path) {
                val oldReal = resolveSafPath(oldPath)
                if (oldReal != null) {
                    try {
                        val oldCache = File(oldReal, "n64_vulkan_pipeline_cache.bin")
                        val newCache = File(newReal, "n64_vulkan_pipeline_cache.bin")
                        if (oldCache.exists()) {
                            newCache.parentFile?.mkdirs()
                            oldCache.copyTo(newCache, overwrite = true)
                            val oldUuid = File(oldReal, "n64_vulkan_pipeline_cache.bin.uuid")
                            if (oldUuid.exists()) {
                                oldUuid.copyTo(File(newReal, "n64_vulkan_pipeline_cache.bin.uuid"), overwrite = true)
                            }
                            Log.i("Phobos", "Copied Vulkan pipeline cache to $newReal")
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Failed to copy Vulkan cache: ${e.message}")
                    }
                }
            }
            PhobosCore.setVulkanCachePath(newReal)
            Log.i("Phobos", "Vulkan cache path set: $newReal")
        } else {
            // Unset/unresolvable: fall back to internal default dir.
            val fallback = File(context.filesDir, "vulkan_cache")
            fallback.mkdirs()
            PhobosCore.setVulkanCachePath(fallback.absolutePath)
        }
    }

    fun setShaderPath(path: String) = viewModelScope.launch {
        settingsStore.setShaderPath(path)
        PhobosCore.setShader(path)
    }

    fun setAspectRatioMode(mode: AspectRatioMode) = viewModelScope.launch {
        settingsStore.setAspectRatioMode(mode)
    }

    fun unloadSystem() {
        // Not viewModelScope: quitting a game a frontend started finishes the activity straight away.
        CoreSession.scope.launch {
            CoreSession.lock.withLock { if (CoreSession.owner === sessionToken) closeSessionLocked(autoSave = true) }
        }
    }

    override fun onCleared() {
        // The activity is finishing for good (Back from the Library, a frontend's clear-task launch, or returning
        // to the frontend): a game this instance loaded is saved and unloaded, unless another instance's load
        // has replaced it already.
        CoreSession.scope.launch {
            CoreSession.lock.withLock { if (CoreSession.owner === sessionToken) closeSessionLocked(autoSave = true) }
        }
    }

    /**
     * Unloads the game the core holds, whichever instance loaded it, snapshotting it first when [autoSave] and
     * Auto-Save State (Task 17) are on. Call with [CoreSession.lock] held.
     */
    private suspend fun closeSessionLocked(autoSave: Boolean) {
        val sysName = CoreSession.system
        val romName = CoreSession.rom
        if (sysName.isEmpty()) return
        // The snapshot comes BEFORE any teardown, while the core is still alive.
        if (autoSave && settings.value.autoSaveState && romName.isNotEmpty()) {
            try { performSaveState(sysName, romName, AUTO_STATE_SLOT) } catch (e: Exception) {
                Log.e("Phobos", "Auto-save failed: ${e.message}")
            }
        }
        // Clear the loaded/paused flags BEFORE the native teardown. The
        // teardown is slow (64DD: flushSavesToDisk copies a ~70MB
        // program.disk), and while it runs the OLD system's singleton
        // hardware (rdram, cartridge.rom, dd.disk) is being freed. If a
        // new EmulatorScreen composes during that window with a STALE
        // _isLoaded=true, its LaunchedEffect(isLoaded) fires
        // setEmulationRunning(true) -> a fresh emu thread grabs the OLD
        // root and runs CPU::LW against the freed buffers -> SIGSEGV on
        // unload (the 64DD quit -> reload crash). Clearing the flag first
        // makes the new screen show "Initializing..." until the load lands.
        _isLoaded.value = false
        _isPaused.value = false
        _loadedGame.value = null
        _runningFrame.value = null
        PhobosCore.setEmulationRunning(false)
        PhobosCore.unloadSystem()
        // Native code removes its own copies on unload; the .cue tracks copied for it go too.
        File(context.cacheDir, "mia_temp/$CUE_TRACKS").deleteRecursively()
        File(context.cacheDir, "cue-sheet.cue").delete()
        CoreSession.system = ""
        CoreSession.rom = ""
        CoreSession.owner = null
        currentSystemName = ""
        currentRomName = ""
        loadedRom = null
        _loadedDiscs.value = emptyList()
        _currentDisc.value = -1
    }

    fun setSystemVisibility(system: String, visible: Boolean) = viewModelScope.launch {
        settingsStore.setSystemVisibility(system, visible)
    }

    // Controller edits run one at a time, each on the settings as stored, so a console or game
    // change is never worked out from bindings another edit is still writing.
    private val controlsEdit = Mutex()
    private fun editControls(edit: suspend (EmulatorSettings) -> Unit) = viewModelScope.launch {
        controlsEdit.withLock { edit(settingsStore.settings.first()) }
    }

    fun resetDefaultMapping() = editControls { settingsStore.resetDefaultMapping() }

    fun clearAllMappings() = editControls { settingsStore.clearAllMappings() }

    /** Binds [bit] at [level]; a button already using [binding] there takes [bit]'s old binding. */
    fun bindButton(level: ControlLevel, bit: Int, binding: String) = editControls { s ->
        val scope = level.scope
        if (scope == null) settingsStore.updateInputMapping(bit, binding)
        else settingsStore.setScopedMappings(scope, Controls.bindButton(level, bit, binding, s.inputMappings, s.controlOverrides))
    }

    fun unbindButton(level: ControlLevel, bit: Int) = editControls { s ->
        val scope = level.scope
        if (scope == null) settingsStore.clearInputMapping(bit)
        else settingsStore.setScopedMappings(scope, Controls.unbindButton(level, bit, s.inputMappings, s.controlOverrides))
    }

    /** [bit] at a console or game [level] goes back to what the level inherits. */
    fun inheritButton(level: ControlLevel, bit: Int) = editControls {
        level.scope?.let { settingsStore.setScopedMappings(it, mapOf(bit to null)) }
    }

    fun inheritAllButtons(level: ControlLevel) = editControls {
        level.scope?.let { settingsStore.clearScopedMappings(it) }
    }

    /** Sets hotkey [action] at [level]; an empty [combo] unbinds it there. */
    fun bindHotkey(level: ControlLevel, action: String, combo: List<Int>) = editControls { s ->
        val scope = level.scope
        if (scope == null) settingsStore.setHotkey(action, combo)
        else settingsStore.setScopedHotkey(scope, action, Controls.hotkeyChange(level, action, combo, s.hotkeys, s.controlOverrides))
    }

    fun inheritHotkey(level: ControlLevel, action: String) = editControls {
        level.scope?.let { settingsStore.setScopedHotkey(it, action, null) }
    }

    fun inheritAllHotkeys(level: ControlLevel) = editControls {
        level.scope?.let { settingsStore.clearScopedHotkeys(it) }
    }

    /** The loaded game's button names by pad bit (N64 "Z" for L2); empty with no game loaded. */
    suspend fun consoleButtonNames(): Map<Int, String> = withContext(Dispatchers.IO) {
        if (_isLoaded.value) Controls.buttonNames(PhobosCore.getButtonNames().toList()) else emptyMap()
    }

    private fun getSanitizedSystemName(name: String): String {
        return name.replace("/", "_").replace("\\", "_").replace(":", "_").trim()
    }

    fun saveState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            performSaveState(systemName, romName, slot)
        }
    }

    /** File name of a state slot; slot < 0 is the Auto slot (Task 17). */
    private fun stateFileName(romName: String, slot: Int) =
        if (slot < 0) "$romName.state.auto" else "$romName.state$slot"

    private fun slotLabel(slot: Int) = if (slot < 0) "Auto" else "Slot $slot"

    // Task 50 preview stored next to its state. PNG data under a non-image
    // extension so gallery apps don't index save-state folders on shared storage.
    private fun thumbnailName(stateFileName: String) = "$stateFileName.thumb"

    // Bumped after a state is saved or deleted so slot previews reload.
    private val _stateRevision = MutableStateFlow(0)
    val stateRevision: StateFlow<Int> = _stateRevision.asStateFlow()

    // Shared save logic (also used by Auto-Save State, Task 17 — slot < 0 = "Auto").
    // Must run on Dispatchers.IO; performs the native snapshot + copies the result
    // to the configured SAF path (or internal fallback). Returns success.
    private suspend fun performSaveState(systemName: String, romName: String, slot: Int): Boolean {
        // Per-call temp files, so a concurrent load or save can't replace this state before it is copied.
        val tempFile = File(context.cacheDir, "state-save-${System.nanoTime()}.tmp")
        val tempThumb = File(context.cacheDir, "state-thumb-${System.nanoTime()}.tmp")
        try {
            return saveStateFromTemp(systemName, romName, slot, tempFile, tempThumb)
        } finally {
            tempFile.delete()
            tempThumb.delete()
        }
    }

    private suspend fun saveStateFromTemp(
        systemName: String, romName: String, slot: Int, tempFile: File, tempThumb: File,
    ): Boolean {
        val fileName = stateFileName(romName, slot)
        val slotLabel = slotLabel(slot)
        val sanitizedName = getSanitizedSystemName(systemName)

        // 1. Tell native to save to a local accessible path
        val nativeSuccess = PhobosCore.saveState(tempFile.absolutePath)
        if (!nativeSuccess) {
            Log.e("Phobos", "Native saveState failed")
            withContext(Dispatchers.Main) {
                Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
            }
            return false
        }

        // Best effort: a failed capture only drops the slot's preview, never the save.
        val thumb = tempThumb.takeIf { capturePreview(it) }
        // A multi-disc game's state notes its disc, so loading it puts that disc back.
        val disc = _currentDisc.value.takeIf { it >= 0 && romName == currentRomName && _loadedDiscs.value.size > 1 }
        val discNote = disc?.let { File(context.cacheDir, "state-disc-${System.nanoTime()}.tmp").apply { writeText("${it + 1}") } }

        // 2. Copy from local path to the user's selected SAF path.
        //    Task 41: fall back to internal storage when no SAF path is
        //    configured so states aren't silently lost.
        val baseUriString = settings.value.statesPath
        val internalStatesDir = File(context.filesDir, "states/$sanitizedName")
        val saved = try {
            if (baseUriString.isNotEmpty()) {
                val baseUri = Uri.parse(baseUriString)
                val rootDir = DocumentFile.fromTreeUri(context, baseUri)
                val systemDir = rootDir?.findFile(sanitizedName) ?: rootDir?.createDirectory(sanitizedName)
                val stateFile = systemDir?.findFile(fileName) ?: systemDir?.createFile("application/octet-stream", fileName)
                if (stateFile != null) {
                    context.contentResolver.openOutputStream(stateFile.uri, "wt")?.use { output ->
                        tempFile.inputStream().use { input -> input.copyTo(output) }
                    }
                    systemDir?.let {
                        writeSafSidecar(it, thumbnailName(fileName), thumb)
                        writeSafSidecar(it, discName(fileName), discNote)
                    }
                    Log.i("Phobos", "Synced state to SAF: ${stateFile.uri}")
                    withContext(Dispatchers.Main) {
                        Toast.makeText(context, "Saved state to $slotLabel", Toast.LENGTH_SHORT).show()
                    }
                    true
                } else false
            } else {
                // Internal fallback: filesDir/states/<system>/<file>
                if (!internalStatesDir.exists()) internalStatesDir.mkdirs()
                tempFile.copyTo(File(internalStatesDir, fileName), overwrite = true)
                val internalThumb = File(internalStatesDir, thumbnailName(fileName))
                runCatching { if (thumb != null) thumb.copyTo(internalThumb, overwrite = true) else internalThumb.delete() }
                val internalDisc = File(internalStatesDir, discName(fileName))
                runCatching { if (discNote != null) discNote.copyTo(internalDisc, overwrite = true) else internalDisc.delete() }
                Log.i("Phobos", "Saved state to internal: ${File(internalStatesDir, fileName).absolutePath}")
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "Saved state to $slotLabel", Toast.LENGTH_SHORT).show()
                }
                true
            }
        } catch (e: Exception) {
            Log.e("Phobos", "Failed to sync state to SAF: ${e.message}")
            withContext(Dispatchers.Main) {
                Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
            }
            false
        }
        discNote?.delete()
        if (saved) _stateRevision.update { it + 1 }
        return saved
    }

    /**
     * Writes a menu-sized PNG of the current frame to [target]. The native screenshot is a
     * full-size uncompressed PNG (about 20 MB for a 4x N64 frame), so it is downscaled first.
     */
    private fun capturePreview(target: File): Boolean {
        val shot = File(context.cacheDir, "state-shot-${System.nanoTime()}.png")
        return try {
            if (!PhobosCore.takeScreenshot(shot.absolutePath) || shot.length() == 0L) return false
            val bitmap = toDisplayAspect(decodePreview { shot.inputStream() } ?: return false)
            target.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
            target.length() > 0
        } catch (e: Exception) {
            Log.w("Phobos", "State preview not captured: ${e.message}")
            false
        } finally {
            shot.delete()
        }
    }

    /**
     * Stretches a frame to the picture's display aspect (an N64 progressive scanout is
     * 640x240 but shows at 4:3). Skipped when the picture is rotated relative to the frame
     * (WonderSwan vertical), since the screenshot is not.
     */
    private fun toDisplayAspect(bitmap: Bitmap): Bitmap {
        val geometry = PhobosCore.getVideoGeometry()
        if (geometry.size != 2 || geometry[0] <= 0f || geometry[1] <= 0f) return bitmap
        if ((geometry[0] >= geometry[1]) != (bitmap.width >= bitmap.height)) return bitmap
        val height = (bitmap.width * geometry[1] / geometry[0]).roundToInt().coerceAtLeast(1)
        if (height == bitmap.height) return bitmap
        return Bitmap.createScaledBitmap(bitmap, bitmap.width, height, true).also { bitmap.recycle() }
    }

    /** Decodes a preview at roughly menu size; an upscaled N64 scanout can be 2560 px wide. */
    private fun decodePreview(open: () -> InputStream?): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        open()?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0) return null
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= PREVIEW_MAX_WIDTH) sample *= 2
        val options = BitmapFactory.Options().apply { inSampleSize = sample }
        return open()?.use { BitmapFactory.decodeStream(it, null, options) }
    }

    /** Copies [source] into [dir] as [name] beside a state, or removes the file there when [source] is null. */
    private fun writeSafSidecar(dir: DocumentFile, name: String, source: File?) {
        try {
            val existing = dir.findFile(name)
            if (source == null) {
                existing?.delete()
                return
            }
            val target = existing ?: dir.createFile("application/octet-stream", name) ?: return
            context.contentResolver.openOutputStream(target.uri, "wt")?.use { output ->
                source.inputStream().use { input -> input.copyTo(output) }
            }
        } catch (e: Exception) {
            Log.w("Phobos", "State sidecar $name not saved: ${e.message}")
        }
    }

    // The disc a multi-disc game's state was saved on, as "2" for disc 2.
    private fun discName(stateFileName: String) = "$stateFileName.disc"

    /** The disc (index) a multi-disc game's state in [slot] was saved on, or null. */
    private fun stateDisc(systemName: String, romName: String, slot: Int): Int? {
        val name = discName(stateFileName(romName, slot))
        val sanitizedName = getSanitizedSystemName(systemName)
        val text = runCatching {
            val baseUriString = settings.value.statesPath
            if (baseUriString.isNotEmpty()) {
                DocumentFile.fromTreeUri(context, Uri.parse(baseUriString))?.findFile(sanitizedName)?.findFile(name)
                    ?.let { note -> context.contentResolver.openInputStream(note.uri)?.bufferedReader()?.use { it.readText() } }
            } else {
                File(context.filesDir, "states/$sanitizedName/$name").takeIf { it.exists() }?.readText()
            }
        }.getOrNull()
        return text?.trim()?.toIntOrNull()?.minus(1)?.takeIf { it >= 0 }
    }

    /** The state in [slot] with its preview, or null when the slot is empty. */
    suspend fun stateSlotPreview(systemName: String, romName: String, slot: Int): StateSlotPreview? =
        withContext(Dispatchers.IO) {
            val fileName = stateFileName(romName, slot)
            val sanitizedName = getSanitizedSystemName(systemName)
            try {
                val baseUriString = settings.value.statesPath
                if (baseUriString.isNotEmpty()) {
                    val systemDir = DocumentFile.fromTreeUri(context, Uri.parse(baseUriString))
                        ?.findFile(sanitizedName) ?: return@withContext null
                    val children = systemDir.listFiles()
                    val state = children.firstOrNull { it.name == fileName } ?: return@withContext null
                    val image = children.firstOrNull { it.name == thumbnailName(fileName) }?.let { thumb ->
                        decodePreview { context.contentResolver.openInputStream(thumb.uri) }
                    }
                    StateSlotPreview(image, state.lastModified())
                } else {
                    val dir = File(context.filesDir, "states/$sanitizedName")
                    val state = File(dir, fileName).takeIf { it.exists() } ?: return@withContext null
                    val thumb = File(dir, thumbnailName(fileName))
                    StateSlotPreview(if (thumb.exists()) decodePreview { thumb.inputStream() } else null, state.lastModified())
                }
            } catch (e: Exception) {
                Log.w("Phobos", "State preview unavailable: ${e.message}")
                null
            }
        }

    fun loadState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            performLoadState(systemName, romName, slot, announceFailure = true)
        }
    }

    // Shared load logic (also used by Auto-Load State, Task 17 — slot < 0 = "Auto").
    // Returns true if a state was found and loaded. [announceFailure] says so in a toast when
    // it wasn't; Auto-Load at game start stays quiet.
    private suspend fun performLoadState(systemName: String, romName: String, slot: Int, announceFailure: Boolean = false): Boolean {
        val startTime = System.currentTimeMillis()
        val fileName = stateFileName(romName, slot)
        val slotLabel = slotLabel(slot)
        // Per-call temp file (see performSaveState); only the SAF path uses it.
        val tempFile = File(context.cacheDir, "state-load-${System.nanoTime()}.tmp")
        val sanitizedName = getSanitizedSystemName(systemName)
        Log.d("Phobos", "loadState start: $fileName")

        return try {
            // Task 41: prefer SAF when configured, else internal storage
            // (filesDir/states/<system>/<file>).
            val baseUriString = settings.value.statesPath
            val internalStateFile = File(context.filesDir, "states/$sanitizedName/$fileName")
            val stateFile: java.io.File? = if (baseUriString.isNotEmpty()) {
                val baseUri = Uri.parse(baseUriString)
                val rootDir = DocumentFile.fromTreeUri(context, baseUri)
                val systemDir = rootDir?.findFile(sanitizedName)
                val safState = systemDir?.findFile(fileName)
                if (safState != null && safState.exists()) {
                    // Copy SAF -> temp for native load
                    context.contentResolver.openInputStream(safState.uri)?.use { input ->
                        tempFile.outputStream().use { output ->
                            val buffer = ByteArray(64 * 1024)
                            var bytesRead: Int
                            while (input.read(buffer).also { bytesRead = it } != -1) {
                                output.write(buffer, 0, bytesRead)
                            }
                        }
                    }
                    tempFile
                } else null
            } else {
                // Internal fallback (if it exists)
                if (internalStateFile.exists()) internalStateFile else null
            }

            if (stateFile != null) {
                // A multi-disc game's state goes back with the disc it was saved on.
                val disc = (if (_loadedDiscs.value.size > 1 && romName == currentRomName) stateDisc(systemName, romName, slot) else null)
                    ?.takeIf { it in _loadedDiscs.value.indices && it != _currentDisc.value }
                if (disc != null && systemName !in DISC_SWAP_SYSTEMS) {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(context, "The state in $slotLabel was saved on disc ${disc + 1}; start the game from that disc to load it", Toast.LENGTH_LONG).show()
                    }
                    return false
                }
                // Paused from the disc change until the state is in, so the game never runs the new disc with the old state.
                if (disc != null) PhobosCore.setPause(true)
                val nativeSuccess = try {
                    if (disc != null && !swapToDisc(context, disc)) {
                        withContext(Dispatchers.Main) {
                            Toast.makeText(context, "The state in $slotLabel needs disc ${disc + 1}, which couldn't be inserted", Toast.LENGTH_LONG).show()
                        }
                        return false
                    }
                    PhobosCore.loadState(stateFile.absolutePath)
                } finally {
                    if (disc != null) PhobosCore.setPause(_isPaused.value)
                }
                Log.d("Phobos", "loadState: Native Unserialize took ${System.currentTimeMillis() - startTime}ms")
                if (nativeSuccess) {
                    Log.i("Phobos", "Successfully loaded state from $fileName. Total time: ${System.currentTimeMillis() - startTime}ms")
                    withContext(Dispatchers.Main) {
                        Toast.makeText(context, "Loaded state from $slotLabel", Toast.LENGTH_SHORT).show()
                    }
                    true
                } else {
                    Log.e("Phobos", "Native loadState failed")
                    if (announceFailure) withContext(Dispatchers.Main) {
                        Toast.makeText(context, "Couldn't load the state in $slotLabel", Toast.LENGTH_SHORT).show()
                    }
                    false
                }
            } else {
                Log.w("Phobos", "loadState: no state file found for $fileName")
                if (announceFailure) withContext(Dispatchers.Main) {
                    Toast.makeText(context, "No state in $slotLabel", Toast.LENGTH_SHORT).show()
                }
                false
            }
        } catch (e: Exception) {
            Log.e("Phobos", "Error during loadState: ${e.message}")
            if (announceFailure) withContext(Dispatchers.Main) {
                Toast.makeText(context, "Couldn't load the state in $slotLabel", Toast.LENGTH_SHORT).show()
            }
            false
        } finally {
            tempFile.delete()
        }
    }

    fun deleteState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            val fileName = stateFileName(romName, slot)
            val slotLabel = slotLabel(slot)
            val sanitizedName = getSanitizedSystemName(systemName)
            val baseUriString = settings.value.statesPath
            var deleted = false
            try {
                if (baseUriString.isNotEmpty()) {
                    val baseUri = Uri.parse(baseUriString)
                    val rootDir = DocumentFile.fromTreeUri(context, baseUri)
                    val systemDir = rootDir?.findFile(sanitizedName)
                    val safState = systemDir?.findFile(fileName)
                    if (safState != null && safState.exists()) {
                        deleted = safState.delete()
                        Log.i("Phobos", "Deleted state from SAF: $fileName (result=$deleted)")
                    }
                    systemDir?.findFile(thumbnailName(fileName))?.delete()
                    systemDir?.findFile(discName(fileName))?.delete()
                }
                // Also remove the internal fallback copy if present.
                val internalStateFile = File(context.filesDir, "states/$sanitizedName/$fileName")
                if (internalStateFile.exists()) {
                    deleted = internalStateFile.delete() || deleted
                    Log.i("Phobos", "Deleted state internally: $fileName")
                }
                File(context.filesDir, "states/$sanitizedName/${thumbnailName(fileName)}").delete()
                File(context.filesDir, "states/$sanitizedName/${discName(fileName)}").delete()
            } catch (e: Exception) {
                Log.e("Phobos", "Error during deleteState: ${e.message}")
            }
            if (deleted) {
                _stateRevision.update { it + 1 }
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "Deleted state from $slotLabel", Toast.LENGTH_SHORT).show()
                }
            } else {
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "No state in $slotLabel", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    // ─── N64 save import and export (pause menu) ─────────────────────────────

    /** The running N64 game's battery saves: the size native keeps and the file it writes, per save type. */
    private fun gameSaveFiles(): Map<N64SaveKind, Pair<Int, File>> =
        PhobosCore.getSaveFiles().mapNotNull { entry ->
            val fields = entry.split('\t')
            if (fields.size != 3) return@mapNotNull null
            val kind = N64SaveKind.ofFileName(fields[0]) ?: return@mapNotNull null
            val size = fields[1].toIntOrNull() ?: return@mapNotNull null
            kind to (size to File(fields[2]))
        }.toMap()

    /** Reads the picked files and matches them against the running game's saves; nothing is written. */
    suspend fun previewSaveImport(uris: List<Uri>, cartridgeOrder: Boolean): SaveImportPreview = withContext(Dispatchers.IO) {
        val game = gameSaveFiles().mapValues { it.value.first }
        val reads = uris.map { readSaveForImport(it, cartridgeOrder) }
        val sources = reads.filterIsInstance<N64SaveRead.Ok>().map { it.source }
        SaveImportPreview(
            plan = N64SaveTransfer.plan(sources, game),
            unreadable = reads.filterIsInstance<N64SaveRead.Unreadable>(),
            offersCartridgeOrder = sources.any {
                it.format == N64SaveFormat.MUPEN64PLUS && (N64SaveKind.SRAM in it.parts || N64SaveKind.FLASH in it.parts)
            },
        )
    }

    private fun readSaveForImport(uri: Uri, cartridgeOrder: Boolean): N64SaveRead {
        val name = displayName(uri) ?: uri.lastPathSegment?.substringAfterLast('/') ?: "save"
        return try {
            val data = context.contentResolver.openInputStream(uri)?.use { input ->
                val out = ByteArrayOutputStream()
                val buffer = ByteArray(64 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    out.write(buffer, 0, count)
                    if (out.size() > N64SaveTransfer.MAX_FILE_SIZE) return N64SaveRead.Unreadable(name, "it is larger than any N64 save")
                }
                out.toByteArray()
            } ?: return N64SaveRead.Unreadable(name, "it couldn't be opened")
            N64SaveTransfer.read(name, data, cartridgeOrder)
        } catch (e: Exception) {
            N64SaveRead.Unreadable(name, "it couldn't be read (${e.message})")
        }
    }

    private fun displayName(uri: Uri): String? = try {
        context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
    } catch (e: Exception) {
        null
    }

    /**
     * Imports [plan] into the running game. The game unloads first, which writes its current save to disk;
     * the files the import replaces and the game's auto-save state (loading it would bring the old save
     * back) then move to a dated folder under Backups beside the save, and the game loads again with the
     * imported save.
     */
    fun importSave(context: Context, plan: N64SaveImportPlan) {
        val rom = loadedRom ?: return
        val system = currentSystemName.takeIf { it.isNotEmpty() } ?: return
        val romName = currentRomName
        viewModelScope.launch(Dispatchers.IO) {
            val targets = gameSaveFiles().mapValues { it.value.second }
            if (plan.writes.isEmpty() || plan.writes.keys.any { it !in targets }) return@launch
            startLoad(context, system, rom) { writeImportedSave(system, romName, plan, targets) }
        }
    }

    private suspend fun writeImportedSave(system: String, romName: String, plan: N64SaveImportPlan, targets: Map<N64SaveKind, File>) {
        val saveDir = targets[N64SaveKind.EEPROM]?.parentFile ?: targets[N64SaveKind.SRAM]?.parentFile
            ?: targets[N64SaveKind.FLASH]?.parentFile ?: targets.values.first().parentFile ?: return
        val stamp = SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date())
        val backup = File(saveDir, "Backups/$stamp")
        val replaced = plan.writes.keys.mapNotNull { kind -> targets[kind] }
        val existed = replaced.filter { it.isFile }.toSet()
        val restoreStates = mutableListOf<() -> Unit>()
        var writing = false
        val message = try {
            for (target in existed) {
                backup.mkdirs()
                target.copyTo(File(backup, target.name), overwrite = true)
            }
            moveAutoStateTo(backup, system, romName, restoreStates)
            writing = true
            for ((kind, data) in plan.writes) {
                val target = targets.getValue(kind)
                target.parentFile?.mkdirs()
                target.writeBytes(data)
            }
            "Imported the ${plan.writes.keys.joinToString(" and ") { it.label }} save"
        } catch (e: Exception) {
            Log.e("Phobos", "Save import failed", e)
            // Before the writes the originals are untouched, and a backup copy may be incomplete.
            if (writing) {
                for (target in replaced) {
                    runCatching { if (target in existed) File(backup, target.name).copyTo(target, overwrite = true) else target.delete() }
                }
            }
            restoreStates.forEach { runCatching(it) }
            if (restoreStates.isNotEmpty()) _stateRevision.update { it + 1 }
            "The save couldn't be imported: ${e.message}"
        }
        withContext(Dispatchers.Main) { Toast.makeText(context, message, Toast.LENGTH_LONG).show() }
    }

    /**
     * Moves the game's auto-save state and its preview into [dir], from the States folder and internal
     * storage, adding to [restore] how to put each file back. Throws if the state stays behind, since
     * loading it would bring the old save back over the import.
     */
    private fun moveAutoStateTo(dir: File, systemName: String, romName: String, restore: MutableList<() -> Unit>) {
        val stateName = stateFileName(romName, -1)
        val sanitizedName = getSanitizedSystemName(systemName)
        val baseUriString = settings.value.statesPath
        val safDir = if (baseUriString.isNotEmpty()) DocumentFile.fromTreeUri(context, Uri.parse(baseUriString))?.findFile(sanitizedName) else null
        for (name in listOf(stateName, thumbnailName(stateName))) {
            val document = safDir?.findFile(name)
            if (safDir != null && document != null) {
                val copy = File(dir, name)
                dir.mkdirs()
                val copied = context.contentResolver.openInputStream(document.uri)?.use { input ->
                    copy.outputStream().use { input.copyTo(it) }
                    true
                } ?: false
                if (copied && document.delete()) {
                    restore += {
                        safDir.createFile("application/octet-stream", name)?.let { restored ->
                            context.contentResolver.openOutputStream(restored.uri, "wt")?.use { output -> copy.inputStream().use { it.copyTo(output) } }
                        }
                    }
                } else if (name == stateName) {
                    throw IOException("the auto-save state couldn't be moved aside")
                }
            }
            val internal = File(context.filesDir, "states/$sanitizedName/$name")
            if (internal.isFile) {
                val copy = File(dir, "internal/$name")
                copy.parentFile?.mkdirs()
                internal.copyTo(copy, overwrite = true)
                if (internal.delete()) {
                    restore += { copy.copyTo(internal, overwrite = true) }
                } else if (name == stateName) {
                    throw IOException("the auto-save state couldn't be moved aside")
                }
            }
        }
        _stateRevision.update { it + 1 }
    }

    /**
     * Writes the running game's save into [folder] in [format]. Files already there are only replaced
     * when [replace] is set; otherwise their names come back for the user to confirm.
     */
    suspend fun exportSave(folder: Uri, format: N64SaveFormat, replace: Boolean): SaveExportResult = withContext(Dispatchers.IO) {
        try {
            PhobosCore.flushSaves()
            val files = gameSaveFiles()
            val saves = files.mapNotNull { (kind, entry) -> entry.second.takeIf { it.isFile }?.let { kind to it.readBytes() } }.toMap()
            if (saves.isEmpty()) return@withContext SaveExportResult.Failed("This game hasn't saved anything yet.")
            val romBase = files.values.first().second.parentFile?.name ?: currentRomName
            val outputs = N64SaveTransfer.export(format, saves, romBase)
            val root = DocumentFile.fromTreeUri(context, folder)
                ?: return@withContext SaveExportResult.Failed("The folder couldn't be opened.")
            val existing = outputs.map { it.first }.filter { findDocument(root, it) != null }
            if (existing.isNotEmpty() && !replace) return@withContext SaveExportResult.Existing(existing)
            for ((path, data) in outputs) {
                val document = findDocument(root, path) ?: createDocument(root, path)
                    ?: return@withContext SaveExportResult.Failed("$path couldn't be created.")
                context.contentResolver.openOutputStream(document.uri, "wt")?.use { it.write(data) }
                    ?: return@withContext SaveExportResult.Failed("$path couldn't be written.")
            }
            SaveExportResult.Done(outputs.map { it.first })
        } catch (e: Exception) {
            Log.e("Phobos", "Save export failed", e)
            SaveExportResult.Failed(e.message ?: "The export failed.")
        }
    }

    // A document at [path] ("name" or "folder/name") under [root], if there is one.
    private fun findDocument(root: DocumentFile, path: String): DocumentFile? {
        val parts = path.split('/')
        var dir = root
        for (part in parts.dropLast(1)) dir = dir.findFile(part)?.takeIf { it.isDirectory } ?: return null
        return dir.findFile(parts.last())
    }

    private fun createDocument(root: DocumentFile, path: String): DocumentFile? {
        val parts = path.split('/')
        var dir = root
        for (part in parts.dropLast(1)) {
            dir = dir.findFile(part)?.takeIf { it.isDirectory } ?: dir.createDirectory(part) ?: return null
        }
        return dir.createFile("application/octet-stream", parts.last())
    }

    private val _systems = MutableStateFlow<List<String>>(emptyList())
    val systems: List<String> get() = _systems.value

    val visibleSystems: StateFlow<List<String>> = combine(settings, _systems) { s, sys ->
        // Merge the two ZX Spectrum entries into one (48K + 128K auto-select at
        // load time by filename — see loadRom). Keep the native "ZX Spectrum 128"
        // system available internally (it's what loads 128K BIOS), just don't
        // show it as a separate Library entry.
        val merged = sys.map {
            if (it == "ZX Spectrum 128") "ZX Spectrum" else it
        }.distinct()
        merged.filter { it !in s.hiddenSystems }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    private val _roms = MutableStateFlow<List<RomFile>>(emptyList())
    val roms: StateFlow<List<RomFile>> = _roms

    // The running game's discs (empty for a single file) and the index of the one in the drive.
    private val _loadedDiscs = MutableStateFlow<List<RomFile>>(emptyList())
    val loadedDiscs: StateFlow<List<RomFile>> = _loadedDiscs.asStateFlow()
    private val _currentDisc = MutableStateFlow(-1)
    val currentDisc: StateFlow<Int> = _currentDisc.asStateFlow()

    /** The disc a multi-disc game last ran from, preselected when it starts again. */
    fun lastDisc(systemName: String, game: RomFile): Int =
        (settings.value.lastDisc["$systemName/${game.name}"] ?: 0).coerceIn(0, (game.discs.size - 1).coerceAtLeast(0))

    private fun rememberDisc(systemName: String, gameName: String, disc: Int) {
        viewModelScope.launch { settingsStore.setLastDisc("$systemName/$gameName", disc) }
    }

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    val logs: StateFlow<List<LogEntry>> = _logs

    private val _isPaused = MutableStateFlow(false)
    val isPaused: StateFlow<Boolean> = _isPaused

    private val _isLoaded = MutableStateFlow(false)
    val isLoaded: StateFlow<Boolean> = _isLoaded

    // True while the EmulatorScreen composable is on screen. Lets the
    // activity-level key fallback decide whether a "library" hotkey press
    // means "swap to library" (emulator visible) or "swap back to the game"
    // (library/settings visible, game loaded + paused).
    private val _emulatorScreenVisible = MutableStateFlow(false)
    val emulatorScreenVisible: StateFlow<Boolean> = _emulatorScreenVisible

    // The game a captured frame shows: a capture that finishes after a game change isn't passed off as the new game's.
    private data class CapturedFrame(val systemName: String, val romName: String, val bitmap: Bitmap)
    private val _runningFrame = MutableStateFlow<CapturedFrame?>(null)
    /** The loaded game while another screen shows, for the Library's Running card; null otherwise. */
    val runningGame: StateFlow<RunningGame?> = combine(_isLoaded, _emulatorScreenVisible, _runningFrame) { loaded, visible, frame ->
        if (loaded && !visible && currentSystemName.isNotEmpty() && currentRomName.isNotEmpty()) {
            val bitmap = frame?.takeIf { it.systemName == currentSystemName && it.romName == currentRomName }?.bitmap
            RunningGame(currentSystemName, currentRomName, bitmap)
        } else null
    }.stateIn(viewModelScope, SharingStarted.Eagerly, null)

    /** Whether [romName] on [systemName] is the game that's loaded. */
    fun isRunning(systemName: String, romName: String): Boolean =
        _isLoaded.value && currentSystemName == systemName && currentRomName == romName

    // The EmulatorScreen on show. When a frontend starts a game over the running one, the new game's screen
    // arrives before the old one leaves, so only the current screen's exit counts.
    private var emulatorScreen: Any? = null

    fun emulatorScreenShown(screen: Any) {
        emulatorScreen = screen
        _emulatorScreenVisible.value = true
    }

    fun emulatorScreenGone(screen: Any) {
        if (emulatorScreen !== screen) return
        emulatorScreen = null
        _emulatorScreenVisible.value = false
    }

    /** Whether another EmulatorScreen has replaced [screen]. */
    fun emulatorScreenReplaced(screen: Any): Boolean = emulatorScreen.let { it != null && it !== screen }

    // One-shot navigation requests from outside the NavHost (debug loader,
    // activity key fallback). MainScaffold collects and navigates. A channel, so a
    // request made before MainScaffold collects (it is composed once the stored
    // theme loads, and the debug loader can ask sooner) is delivered, not dropped.
    private val _navEvents = Channel<String>(Channel.BUFFERED)
    val navEvents: Flow<String> = _navEvents.receiveAsFlow()

    fun navigateTo(route: String) { _navEvents.trySend(route) }

    /** Pause + leave to the library; the game stays loaded (swap-screen feature). */
    fun swapToLibrary() {
        setPause(true)
        captureRunningFrame()
        navigateTo("library")
    }

    private fun captureRunningFrame() {
        val systemName = currentSystemName
        val romName = currentRomName
        viewModelScope.launch(Dispatchers.IO) {
            val shot = File(context.cacheDir, "running-${System.nanoTime()}.png")
            val frame = try {
                if (PhobosCore.takeScreenshot(shot.absolutePath) && shot.length() > 0) {
                    decodePreview { shot.inputStream() }?.let { toDisplayAspect(it) }
                } else null
            } catch (e: Exception) {
                Log.w("Phobos", "Running game's frame not captured: ${e.message}")
                null
            } finally {
                shot.delete()
            }
            _runningFrame.value = frame?.let { CapturedFrame(systemName, romName, it) }
        }
    }

    /** Return to the running game from library/settings; resumes emulation. */
    fun swapBackToGame() {
        if (!isLoaded.value || emulatorScreenVisible.value) return
        val sys = currentSystemName
        val rom = currentRomName
        if (sys.isEmpty() || rom.isEmpty()) return
        navigateTo("emulator/${Uri.encode(sys)}/${Uri.encode(rom)}")
        setPause(false)
    }

    // Unsupported-system popup (set when a broken core's load is refused —
    // ZX Spectrum 128, PC Engine, PC Engine CD, SuperGrafx, Neo Geo).
    // Holds the system NAME to show in the dialog; null = no popup.
    private val _unsupportedSystem = MutableStateFlow<String?>(null)
    val unsupportedSystem: StateFlow<String?> = _unsupportedSystem

    // Neo Geo fails to load only when its mandatory BIOS is missing (the core
    // now returns false gracefully instead of SIGSEGVing). Surface a targeted
    // message so the user knows to supply neogeo.zip rather than "load failed".
    private val _biosRequired = MutableStateFlow<String?>(null)
    val biosRequired: StateFlow<String?> = _biosRequired
    fun dismissBiosRequired() { _biosRequired.value = null }

    private val _firmwareRequired = MutableStateFlow<FirmwareRequired?>(null)
    val firmwareRequired: StateFlow<FirmwareRequired?> = _firmwareRequired
    fun dismissFirmwareRequired() { _firmwareRequired.value = null }

    // When the Neo Geo BIOS was present but the ROM still failed to load, the
    // failure is the ROM itself (e.g. it isn't actually a Neo Geo MVS/AES game,
    // like a CPS-1 zip mis-categorised as Neo Geo) — NOT a missing BIOS. Surface
    // a truthful message instead of the misleading "BIOS Required" popup.
    private val _neoGeoRomLoadFailed = MutableStateFlow<String?>(null)
    val neoGeoRomLoadFailed: StateFlow<String?> = _neoGeoRomLoadFailed
    fun dismissNeoGeoRomLoadFailed() { _neoGeoRomLoadFailed.value = null }

    // The ZX Spectrum's tape, polled ~10 Hz while a ZX game is loaded, for the keyboard's stripe and
    // the tape controls.
    private val _zxTape = MutableStateFlow(ZxTape())
    val zxTape: StateFlow<ZxTape> = _zxTape

    private fun refreshZxTape() {
        val next = if (_isLoaded.value && currentSystemName.contains("ZX Spectrum", ignoreCase = true)) {
            ZxTape.of(PhobosCore.getZxTapeState())
        } else ZxTape()
        if (next != _zxTape.value) _zxTape.value = next
    }

    init {
        viewModelScope.launch(Dispatchers.Default) {
            while (true) {
                refreshZxTape()
                delay(100)
            }
        }
    }

    fun setZxTapePlaying(play: Boolean) = viewModelScope.launch(Dispatchers.Default) {
        PhobosCore.setZxTapePlaying(play)
        refreshZxTape()
    }

    fun playZxTapeFromStart() = viewModelScope.launch(Dispatchers.Default) {
        PhobosCore.playTape()
        refreshZxTape()
    }

    fun rewindZxTape() = viewModelScope.launch(Dispatchers.Default) {
        PhobosCore.rewindZxTape()
        refreshZxTape()
    }

    // Current emulated system name ("Nintendo 64", "PlayStation", ...) — used by
    // the rumble loop and input routing to decide system-specific behavior.
    @Volatile
    private var currentSystemName: String = ""

    /** Name of the currently loaded system, or empty when nothing is loaded. */
    val loadedSystemName: String get() = currentSystemName

    /** Base name (no extension) of the currently loaded ROM — key for auto-save/load. */
    @Volatile
    private var currentRomName: String = ""

    // This instance, as the owner of the game in CoreSession.
    private val sessionToken = Any()

    // The file the running game came from, for Reload.
    @Volatile
    private var loadedRom: RomFile? = null

    // The running game was started by another app (a frontend), so leaving it returns there.
    @Volatile
    private var externalSession = false

    private val _launchChoice = MutableStateFlow<LaunchChoice?>(null)
    val launchChoice: StateFlow<LaunchChoice?> = _launchChoice.asStateFlow()

    // Returning to the app that started the game: MainActivity finishes its task.
    private val _leaveToFrontend = Channel<Unit>(Channel.CONFLATED)
    val leaveToFrontend: Flow<Unit> = _leaveToFrontend.receiveAsFlow()

    /**
     * Waits (bounded) until a load finds what a load from the Library finds: the core's systems, listed once the
     * assets are unpacked, and the stored settings in [settings] (which start from the defaults when the startup
     * read timed out). False when the systems didn't come in time.
     */
    suspend fun awaitLaunchReady(): Boolean {
        val ready = withTimeoutOrNull(LAUNCH_READY_TIMEOUT_MS) { _systems.first { it.isNotEmpty() } } != null
        withTimeoutOrNull(SETTINGS_READY_TIMEOUT_MS) {
            val stored = settingsStore.settings.first()
            settings.first { it == stored }
        }
        return ready
    }

    /** Starts the game [request] asks for (a frontend's launch), or asks which system it is for. */
    fun launchExternal(request: LaunchRequest) {
        viewModelScope.launch {
            if (!awaitLaunchReady()) {
                Log.w("Phobos", "Launch: not ready in time for ${request.uri}")
                return@launch
            }
            val native = _systems.value
            val extensions = native.associateWith { PhobosCore.getSystemExtensions(it) }
            val target = resolveLaunch(context, request, native, extensions, settings.value.systemRomPaths)
            Log.i("Phobos", "Launch: ${request.uri} (system hint ${request.systemHint}) -> $target")
            when (target) {
                is LaunchTarget.Ready -> startExternal(target.system, target.rom)
                is LaunchTarget.ChooseSystem -> _launchChoice.value = LaunchChoice(target.rom, target.candidates)
                is LaunchTarget.Unreadable -> {
                    Toast.makeText(
                        context,
                        "Phobos can't read ${target.name}. Add its folder to the system in Phobos (Add ROM Folder), then try again.",
                        Toast.LENGTH_LONG,
                    ).show()
                    if (!_isLoaded.value) _leaveToFrontend.trySend(Unit)
                }
            }
        }
    }

    fun chooseLaunchSystem(system: String) {
        val choice = _launchChoice.value ?: return
        _launchChoice.value = null
        startExternal(system, choice.rom)
    }

    /** The user closed the system choice: back to the app that asked, unless a game is running here. */
    fun cancelLaunchChoice() {
        _launchChoice.value = null
        if (!_isLoaded.value) _leaveToFrontend.trySend(Unit)
    }

    private fun startExternal(system: String, rom: RomFile) {
        externalSession = true
        if (_isLoaded.value && currentSystemName == system && currentRomName == rom.name) {
            // The game that's running (a frontend's resume): it carries on where it is.
            if (!_emulatorScreenVisible.value) swapBackToGame()
            return
        }
        startLoad(context, system, rom)
        navigateTo("emulator/${Uri.encode(system)}/${Uri.encode(rom.name)}")
    }

    /** After quitting or a failed load: true when the game came from another app, which Phobos returns to. */
    fun leaveGame(): Boolean {
        if (!externalSession) return false
        externalSession = false
        _leaveToFrontend.trySend(Unit)
        return true
    }

    private val _perfStats = MutableStateFlow(PerformanceStats(0.0, 0.0, -1))
    val perfStats: StateFlow<PerformanceStats> = _perfStats

    // Logical size of the running game's picture (pixel-aspect corrected), for aspect-correct
    // display; null until the first frame of a game.
    private val _videoGeometry = MutableStateFlow<VideoGeometry?>(null)
    val videoGeometry: StateFlow<VideoGeometry?> = _videoGeometry

    private val _currentSlot = MutableStateFlow(0)
    val currentSlot: StateFlow<Int> = _currentSlot

    // Triggered once when GPU pipeline failures are detected (broken built-in driver).
    // User can dismiss; won't re-show until next game load.
    private val _showDriverSuggestion = MutableStateFlow(false)
    val showDriverSuggestion: StateFlow<Boolean> = _showDriverSuggestion
    private var driverSuggestionShown = false

    // Debounce the slot toast: rapid cycling (holding the hotkey) fires one
    // incrementSlot per press, which would toast EVERY intermediate slot.
    // Only the slot settled on after a short quiet period gets a toast.
    private var slotToastJob: kotlinx.coroutines.Job? = null
    private fun showSlotToast() {
        slotToastJob?.cancel()
        slotToastJob = viewModelScope.launch {
            delay(350)
            withContext(Dispatchers.Main) {
                val label = if (_currentSlot.value < 0) "Slot Auto" else "Slot ${_currentSlot.value}"
                Toast.makeText(context, "Selected $label", Toast.LENGTH_SHORT).show()
            }
        }
    }

    // Slot cycler wraps 0-9 -> Auto (AUTO_STATE_SLOT = -1) -> 0, so the Auto
    // slot is reachable after slot 9 (and before slot 0) like other emulators.
    fun incrementSlot() {
        _currentSlot.value = when (val v = _currentSlot.value) {
            9 -> AUTO_STATE_SLOT
            AUTO_STATE_SLOT -> 0
            else -> v + 1
        }
        showSlotToast()
    }

    fun decrementSlot() {
        _currentSlot.value = when (val v = _currentSlot.value) {
            AUTO_STATE_SLOT -> 9
            0 -> AUTO_STATE_SLOT
            else -> v - 1
        }
        showSlotToast()
    }

    fun resetSystem() {
        viewModelScope.launch(Dispatchers.IO) {
            PhobosCore.resetSystem()
        }
    }

    fun setN64Recompiler(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64Recompiler(enabled)
        PhobosCore.setN64Recompiler(enabled)
        noticeN64SettingDeferred(N64_APPLIES_ON_RESET)
    }

    fun setSkipBootRom(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setSkipBootRom(enabled)
        PhobosCore.setSkipBootRom(enabled)
    }

    fun takeScreenshot(systemName: String, romName: String) {
        viewModelScope.launch(Dispatchers.IO) {
            // Task 41: resolve the SAF path to a REAL filesystem path; fall back to
            // internal storage when unset or unresolvable (SAF URIs aren't usable
            // as native file paths).
            val baseDir = resolveSafPath(settings.value.screenshotsPath)
                ?: File(context.filesDir, "screenshots").absolutePath
            val dir = File(baseDir, systemName)
            if (!dir.exists()) dir.mkdirs()
            
            val fileName = "${romName}_${System.currentTimeMillis()}.png"
            val file = File(dir, fileName)
            
            val success = PhobosCore.takeScreenshot(file.absolutePath)
            if (success) {
                Log.i("Phobos", "Screenshot saved: ${file.absolutePath}")
            } else {
                Log.e("Phobos", "Failed to take screenshot")
            }
        }
    }

    init {
        viewModelScope.launch {
            // Initial settings sync (Main thread is fine for small JNI setters)
            PhobosCore.setLogLevel(settings.value.logVerbosity.ordinal)
            PhobosCore.setMuteAudio(settings.value.muteAudio)
            PhobosCore.setFastBoot(settings.value.fastBoot)
            PhobosCore.setFastForwardSpeed(settings.value.fastForwardSpeed)
            PhobosCore.setNgcdLoadSpeed(settings.value.ngcdLoadSpeed)
            PhobosCore.setZxLoadSpeed(settings.value.zxLoadSpeed)
            PhobosCore.setZxTapeAuto(settings.value.zxTapeAuto)
            PhobosCore.setN64DebugLogging(settings.value.n64DebugLogging)
            PhobosCore.setN64CountPerOp(if (settings.value.n64UseDefaultCountPerOp) 2 else settings.value.n64CountPerOp)
            PhobosCore.setN64CpuOverclock(if (settings.value.n64UseDefaultCpuOverclock) 0 else settings.value.n64CpuOverclock)
            PhobosCore.setNativeLibraryDir(context.applicationInfo.nativeLibraryDir)

            // Settings > Video also applies to a running game. Off the main thread: the native
            // setter waits for a game load to finish.
            launch(Dispatchers.IO) {
                settingsStore.settings
                    .map { Triple(it.overscan, it.colorEmulation, it.interframeBlending) }
                    .distinctUntilChanged()
                    .collect { (overscan, colorEmulation, interframeBlending) ->
                        PhobosCore.setVideoSettings(overscan, colorEmulation, interframeBlending)
                    }
            }
            
            withContext(Dispatchers.IO) {
                settingsStore.initializeDefaults()
                extractAssets()
                // Initialize systems list in background
                _systems.value = PhobosCore.enumerateSystems().sorted()
            }

            // Move log collection to background to avoid janking the main thread
            launch(Dispatchers.Default) {
                while (true) {
                    val newLogs = PhobosCore.getNewLogs()
                    if (newLogs.isNotEmpty()) {
                        // Batch update the log list to avoid excessive Main thread work
                        val currentLogs = _logs.value
                        val updatedLogs = (currentLogs + newLogs).takeLast(2000) // Reduced limit to 2000
                        _logs.value = updatedLogs
                    }
                    
                    val geometry = if (_isLoaded.value) {
                        PhobosCore.getVideoGeometry().let { g ->
                            if (g.size == 2 && g[0] > 0f && g[1] > 0f) VideoGeometry(g[0], g[1]) else null
                        }
                    } else null
                    if (geometry != _videoGeometry.value) _videoGeometry.value = geometry

                    if (_isLoaded.value && !_isPaused.value) {
                        val stats = PhobosCore.getPerformanceStats()
                        _perfStats.value = stats
                        // Show driver suggestion once if pipeline failures are detected
                        // AND the user is on the BUILT-IN Adreno driver (no custom
                        // driver installed). Turnip's device name is "Turnip Adreno
                        // (TM) 740" — it contains "Adreno", so isAdrenoDriver alone
                        // can't distinguish; require customDriverPath to be empty so
                        // the popup never fires when a custom driver is active.
                        val noCustomDriver = settings.value.customDriverPath.isEmpty()
                        if (stats.pipelineFailures > 0 && stats.isAdrenoDriver && noCustomDriver && !driverSuggestionShown) {
                            driverSuggestionShown = true
                            _showDriverSuggestion.value = true
                        }
                    }
                    
                    delay(500)
                }
            }

            // Rumble (N64 Rumble Pak + PS1 DualShock): poll the native motor state
            // at ~30 Hz and drive the device vibrator. Binary motor — a continuous
            // waveform while on, cancelled on the falling edge. Also cancelled
            // whenever the rumble source isn't active (pause, quit, pak change).
            launch(Dispatchers.Default) {
                val vibrator = context.getSystemService(Vibrator::class.java)
                var rumbleActive = false
                while (true) {
                    // Which systems can rumble:
                    //  - Nintendo 64: only when a Rumble Pak is attached (n64Pak setting)
                    //  - PlayStation: whenever a DualShock is connected (analog mode on;
                    //    the ares core exposes its Rumble node and the native side already
                    //    feeds it into rumbleState)
                    val rumbleSupported = when (currentSystemName) {
                        "Nintendo 64" -> settings.value.n64Pak == "Rumble Pak"
                        "PlayStation" -> settings.value.ps1AnalogMode
                        else -> false
                    }
                    val rumbleOn = _isLoaded.value && !_isPaused.value &&
                        rumbleSupported &&
                        PhobosCore.getRumbleState()
                    if (rumbleOn && !rumbleActive) {
                        rumbleActive = true
                        try {
                            // Decaying hit envelope: a strong initial pulse that
                            // ramps quickly to zero. Games like Mario Tennis write
                            // the rumble bit continuously during a rally (correct
                            // emulation), but a sustained buzz is unpleasant. This
                            // makes each rally period feel like a single "hit" —
                            // strong impact, quick decay — instead of an endless
                            // buzz. Waveform timesteps (ms) with per-step amplitude
                            // (0-255): 100ms full, then decaying steps to 0.
                            val timings = longArrayOf(
                                0, 100, 80, 80, 80, 80, 80, 100, 100
                            )
                            val amps = intArrayOf(
                                0, 255, 200, 150, 100, 60, 30, 10, 0
                            )
                            vibrator?.vibrate(VibrationEffect.createWaveform(timings, amps, -1))
                        } catch (_: Exception) {}
                    } else if (!rumbleOn && rumbleActive) {
                        rumbleActive = false
                        try { vibrator?.cancel() } catch (_: Exception) {}
                    }
                    delay(33)
                }
            }
        }
    }

    fun clearLogs() {
        _logs.value = emptyList()
    }

    fun dismissDriverSuggestion() {
        _showDriverSuggestion.value = false
    }

    fun resetDriverSuggestion() {
        driverSuggestionShown = false
    }

    fun installCustomDriver(context: Context, uri: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val driverDir = File(context.filesDir, "gpu_drivers")
                if (!driverDir.exists()) driverDir.mkdirs()

                // Resolve the REAL filename from the SAF URI — uri.lastPathSegment
                // for a content:// document returns the doc ID (e.g.
                // "msf:1000172790"), NOT the filename. Query DISPLAY_NAME so raw
                // .so files get a proper name with the right extension.
                var name = "driver.so"
                context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                        if (idx >= 0) cursor.getString(idx)?.let { name = it }
                    }
                }
                val isZip = name.endsWith(".zip", ignoreCase = true) ||
                            name.endsWith(".adpkg", ignoreCase = true) ||
                            name.endsWith(".apk", ignoreCase = true)

                context.contentResolver.openInputStream(uri)?.use { input ->
                    if (isZip) {
                        val path = extractSoFromZip(input, driverDir, name.substringBeforeLast("."), null)
                        if (path != null) {
                            setCustomDriverPath(path)
                            Log.i("Phobos", "Installed driver: $path")
                            _driverSuccessEvent.emit("Installed ${name.substringBeforeLast(".")}")
                        } else {
                            Log.e("Phobos", "No .so found in driver package")
                            _driverErrorEvent.emit("No .so driver found in ${name}")
                        }
                    } else {
                        // Raw .so (or any non-zip): copy it directly as the driver.
                        // Ensure a .so extension (some SAF providers return the
                        // doc ID as the display name — e.g. "msf:1000172790").
                        val soName = if (name.endsWith(".so", ignoreCase = true)) name else "$name.so"
                        val outFile = resolveDriverTarget(driverDir, soName, null)
                        runCatching { File(driverDir, "$soName.source").delete() }
                        FileOutputStream(outFile).use { output ->
                            input.copyTo(output)
                        }
                        Log.i("Phobos", "Installed driver: ${outFile.absolutePath}")
                        setCustomDriverPath(outFile.absolutePath)
                        _driverSuccessEvent.emit("Installed ${outFile.name}")
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to install custom driver: ${e.message}")
                _driverErrorEvent.emit("Install failed: ${e.message}")
            }
        }
    }

    /**
     * Extracts the first .so entry from a driver package stream into [driverDir]
     * and returns its absolute path, or null if the package has no .so. Shared by
     * the manual-upload and GitHub-download install paths.
     */
    private fun extractSoFromZip(input: InputStream, driverDir: File, desiredName: String, identity: String?): String? {
        ZipInputStream(input).use { zip ->
            var entry = zip.nextEntry
            while (entry != null) {
                if (!entry.isDirectory && entry.name.endsWith(".so")) {
                    val outFile = resolveDriverTarget(driverDir, desiredName, identity)
                    FileOutputStream(outFile).use { output -> zip.copyTo(output) }
                    return outFile.absolutePath
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return null
    }

    private fun sanitizeDriverName(name: String): String {
        val clean = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        return if (clean.endsWith(".so", true)) clean else "$clean.so"
    }

    /**
     * Resolves the installed-driver target file in [driverDir].
     * - If [identity] (e.g. "owner/repo@tag") matches an already-installed
     *   driver (read from its sidecar), that driver's file is returned so
     *   re-installing the SAME driver overwrites it instead of duplicating.
     * - The [desired] name itself encodes the driver identity (repo+tag for
     *   downloads, filename for manual uploads), so distinct drivers always
     *   get distinct files and the same driver overwrites — no duplicates.
     */
    private fun resolveDriverTarget(driverDir: File, desired: String, identity: String?): File {
        if (identity != null) {
            driverDir.listFiles { f -> f.isFile && f.name.endsWith(".so", true) }?.forEach { f ->
                val info = readDriverSourceSidecar(f.absolutePath)
                if (info != null && "${info.owner}/${info.repo}@${info.tag}" == identity) return f
            }
        }
        return File(driverDir, sanitizeDriverName(desired))
    }

    fun installCustomDriverFromFolder(context: Context, treeUri: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val driverDir = File(context.filesDir, "gpu_drivers")
                if (!driverDir.exists()) driverDir.mkdirs()

                // Scan the picked folder for candidate driver files: .so (raw)
                // or .zip/.adpkg (packaged). List them; if exactly one driver
                // file is found, install it directly. (The SAF file picker's
                // MIME/name filtering hid non-turnip files — a folder picker
                // + scan bypasses that entirely.)
                val docFile = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, treeUri)
                val candidates = mutableListOf<DocumentFile>()
                if (docFile != null) {
                    docFile.listFiles().forEach { f ->
                        val n = f.name ?: ""
                        if (!f.isDirectory && (n.endsWith(".so", true) || n.endsWith(".zip", true) || n.endsWith(".adpkg", true))) {
                            candidates.add(f)
                        }
                    }
                }
                if (candidates.isEmpty()) {
                    Log.e("Phobos", "No driver files found in folder")
                    _driverErrorEvent.emit("No driver files found in folder")
                    return@launch
                }
                val file = candidates[0]
                val name = file.name ?: "driver"
                val isZip = name.endsWith(".zip", true) || name.endsWith(".adpkg", true)
                context.contentResolver.openInputStream(file.uri)?.use { input ->
                    if (isZip) {
                        ZipInputStream(input).use { zip ->
                            var entry = zip.nextEntry
                            while (entry != null) {
                                if (!entry.isDirectory && entry.name.endsWith(".so")) {
                                    val outFile = resolveDriverTarget(driverDir, entry.name.substringAfterLast("/"), null)
                                    FileOutputStream(outFile).use { output -> zip.copyTo(output) }
                                    Log.i("Phobos", "Extracted driver: ${outFile.absolutePath}")
                                    setCustomDriverPath(outFile.absolutePath)
                                    _driverSuccessEvent.emit("Installed ${outFile.name}")
                                    break
                                }
                                zip.closeEntry()
                                entry = zip.nextEntry
                            }
                        }
                    } else {
                        val outFile = resolveDriverTarget(driverDir, name, null)
                        runCatching { File(driverDir, "$name.source").delete() }
                        FileOutputStream(outFile).use { output -> input.copyTo(output) }
                        Log.i("Phobos", "Installed driver: ${outFile.absolutePath}")
                        setCustomDriverPath(outFile.absolutePath)
                        _driverSuccessEvent.emit("Installed ${outFile.name}")
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to install driver from folder: ${e.message}")
                _driverErrorEvent.emit("Install failed: ${e.message}")
            }
        }
    }

    // ── GitHub driver downloader ──────────────────────────────────────────────

    /** Fetches driver assets for a GitHub source (runs on IO). */
    suspend fun fetchDriverReleases(source: DriverSource): List<DriverAsset> =
        withContext(Dispatchers.IO) { DriverDownloader.fetchAssets(source) }

    /**
     * Downloads a driver asset, extracts its .so into gpu_drivers, activates it,
     * and records the source repo + tag in a sidecar file for update checks.
     */
    fun downloadAndInstallDriver(context: Context, source: DriverSource, asset: DriverAsset) {
        viewModelScope.launch(Dispatchers.IO) {
            _downloadProgress.value = 0L to -1L
            try {
                val driverDir = File(context.filesDir, "gpu_drivers")
                if (!driverDir.exists()) driverDir.mkdirs()
                val tempFile = File(context.cacheDir, "driver_dl_${System.currentTimeMillis()}.zip")
                DriverDownloader.download(asset.url, tempFile) { d, t -> _downloadProgress.value = d to t }
                tempFile.inputStream().use { input ->
                    val path = extractSoFromZip(input, driverDir, "${source.repo}_${source.owner}_${asset.tag}", "${source.owner}/${source.repo}@${asset.tag}")
                    if (path != null) {
                        setCustomDriverPath(path)
                        writeDriverSourceSidecar(path, source, asset.tag)
                        Log.i("Phobos", "Downloaded & installed driver: $path (${source.owner}/${source.repo} @ ${asset.tag})")
                        _driverSuccessEvent.emit("Installed ${asset.name}")
                    } else {
                        _driverErrorEvent.emit("No .so driver found in ${asset.name}")
                    }
                }
                runCatching { tempFile.delete() }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to download/install driver: ${e.message}")
                _driverErrorEvent.emit("Download failed: ${e.message}")
            } finally {
                _downloadProgress.value = -1L to -1L
            }
        }
    }

    /** Lists drivers currently installed in gpu_drivers (for the delete dialog). */
    suspend fun getInstalledDrivers(): List<InstalledDriver> = withContext(Dispatchers.IO) {
        val driverDir = File(context.filesDir, "gpu_drivers")
        if (!driverDir.exists()) return@withContext emptyList()
        (driverDir.listFiles { f -> f.isFile && f.name.endsWith(".so", true) } ?: emptyArray())
            .map { file ->
                val info = readDriverSourceSidecar(file.absolutePath)
                InstalledDriver(
                    name = file.name,
                    path = file.absolutePath,
                    source = info?.let { "${it.owner}/${it.repo}" } ?: "Unknown source",
                    tag = info?.tag ?: ""
                )
            }
    }

    /** Deletes an installed driver by filename; clears the active path if it was active. */
    fun deleteDriver(name: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val driverDir = File(context.filesDir, "gpu_drivers")
            val file = File(driverDir, name)
            runCatching { File(driverDir, "$name.source").delete() }
            runCatching { file.delete() }
            if (settings.value.customDriverPath == file.absolutePath) {
                setCustomDriverPath("")
            }
        }
    }

    fun setDriverUpdateNotifications(enabled: Boolean) =
        viewModelScope.launch { settingsStore.setDriverUpdateNotifications(enabled) }

    /**
     * Checks the active driver's source repo for a newer release and emits a
     * toast event when one is found. Throttled to once per 6h and gated by the
     * driver-update-notifications setting. Safe to call on every app launch.
     */
    fun checkForDriverUpdates() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (!settings.value.driverUpdateNotifications) return@launch
                val path = settings.value.customDriverPath
                if (path.isEmpty()) return@launch
                val now = System.currentTimeMillis()
                if (now - settings.value.driverUpdateCheckTime < 6 * 3600_000L) return@launch
                settingsStore.setDriverUpdateCheckTime(now)
                val info = readDriverSourceSidecar(path) ?: return@launch
                val source = DriverSource(info.owner, "", info.owner, info.repo)
                val newer = newerDriverRelease(info.tag, DriverDownloader.fetchAssets(source)) ?: return@launch
                _driverUpdateEvent.emit("Driver update available: ${info.owner}/${info.repo} → ${newer.tag}")
            } catch (e: Exception) {
                Log.w("Phobos", "Driver update check failed: ${e.message}")
            }
        }
    }

    private val _appUpdate = MutableStateFlow<AppUpdateState>(AppUpdateState.Idle)
    val appUpdate: StateFlow<AppUpdateState> = _appUpdate.asStateFlow()
    private val _appUpdateEvent = MutableSharedFlow<String>()
    val appUpdateEvent = _appUpdateEvent.asSharedFlow()
    private var appUpdateJob: Job? = null

    init {
        viewModelScope.launch {
            AppUpdater.installFailures.collect { message ->
                _appUpdate.update { if (it is AppUpdateState.Installing) AppUpdateState.Failed(message, it.update) else it }
            }
        }
    }

    /**
     * Checks GitHub for a newer Phobos: from Settings → About, or ([automatic]) when Phobos starts, at most
     * once a day when allowed, saying so in a toast unless a game is on screen. Not while an update is being
     * checked, downloaded or confirmed in Android's installer.
     */
    fun checkForAppUpdate(automatic: Boolean) {
        if (appUpdateJob?.isActive == true || _appUpdate.value is AppUpdateState.Installing) return
        appUpdateJob = viewModelScope.launch(Dispatchers.IO) {
            // At launch [settings] can still be the defaults, so the stored values decide.
            val current = if (automatic) settingsStore.settings.first() else settings.value
            if (automatic) {
                val now = System.currentTimeMillis()
                if (!current.appUpdateAutoCheck || now - current.appUpdateCheckTime < APP_UPDATE_INTERVAL_MS) return@launch
                settingsStore.setAppUpdateCheckTime(now)
            }
            _appUpdate.value = AppUpdateState.Checking
            _appUpdate.value = try {
                val update = AppUpdater.check(BuildConfig.VERSION_CODE.toLong(), BuildConfig.FLAVOR, current.appUpdateNightly)
                if (update == null) {
                    AppUpdateState.UpToDate(System.currentTimeMillis())
                } else {
                    if (automatic && !_emulatorScreenVisible.value) {
                        _appUpdateEvent.emit("Phobos ${update.manifest.versionName} is available in Settings → About")
                    }
                    AppUpdateState.Available(update)
                }
            } catch (e: Exception) {
                Log.w("Phobos", "Update check failed: ${e.message}")
                if (automatic) AppUpdateState.Idle else AppUpdateState.Failed(e.message ?: "The check failed.", null)
            }
        }
    }

    /** Downloads the update found and hands it to Android's installer. */
    fun installAppUpdate() {
        val update = when (val state = _appUpdate.value) {
            is AppUpdateState.Available -> state.update
            is AppUpdateState.Failed -> state.update
            else -> null
        } ?: return
        if (appUpdateJob?.isActive == true) return
        appUpdateJob = viewModelScope.launch(Dispatchers.IO) {
            _appUpdate.value = AppUpdateState.Downloading(update, 0f)
            try {
                var shownPercent = Int.MIN_VALUE
                val apk = AppUpdater.download(context, update, File(context.cacheDir, "updates")) { progress ->
                    val percent = (progress * 100).toInt()
                    if (percent != shownPercent) {
                        shownPercent = percent
                        _appUpdate.value = AppUpdateState.Downloading(update, progress)
                    }
                }
                _appUpdate.value = AppUpdateState.Installing(update)
                AppUpdater.install(context, apk)
            } catch (e: Exception) {
                Log.w("Phobos", "Update failed: ${e.message}")
                _appUpdate.value = AppUpdateState.Failed(e.message ?: "The update failed.", update)
            }
        }
    }

    fun setAppUpdateAutoCheck(enabled: Boolean) = viewModelScope.launch { settingsStore.setAppUpdateAutoCheck(enabled) }

    /** Switches the channel, then checks again (see [checkForAppUpdate] for when it doesn't). */
    fun setAppUpdateNightly(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setAppUpdateNightly(enabled)
        withTimeoutOrNull(SETTINGS_READY_TIMEOUT_MS) { settings.first { it.appUpdateNightly == enabled } } ?: return@launch
        checkForAppUpdate(automatic = false)
    }

    private fun writeDriverSourceSidecar(soPath: String, source: DriverSource, tag: String) {
        runCatching { File("$soPath.source").writeText("${source.owner}/${source.repo}\n$tag") }
    }

    private fun readDriverSourceSidecar(soPath: String): DriverSidecar? {
        return runCatching {
            val lines = File("$soPath.source").readLines()
            if (lines.size >= 2) {
                val (owner, repo) = lines[0].split("/", limit = 2)
                DriverSidecar(owner, repo, lines[1])
            } else null
        }.getOrNull()
    }

    private val biosMap = mapOf(
        "scph5501.bin" to "fw_psx_us",
        "scph5500.bin" to "fw_psx_jp",
        "scph5502.bin" to "fw_psx_eu",
        "scph101.bin" to "fw_psx_us_v45",
        "neogeo.zip" to "fw_ng_bios",
        "aes.zip" to "fw_ng_aes",
        "neocd.zip" to "fw_ng_cd",
        "neocd.bin" to "fw_ng_cd",
        "ngcd.zip" to "fw_ng_cd",
        "ngcd.bin" to "fw_ng_cd",
        "ngp.zip" to "fw_ngp",
        "ngpc.zip" to "fw_ngpc",
        "ngp.bin" to "fw_ngp",
        "ngpc.bin" to "fw_ngpc",
        "Neo Geo Pocket - BIOS (World).bin" to "fw_ngp",
        "Neo Geo Pocket Color - BIOS (World).bin" to "fw_ngpc",
        "64dd_ipl.bin" to "fw_n64dd_jp",
        "n64dd_ipl.bin" to "fw_n64dd_jp",
        "n64dd_ipl_jp.bin" to "fw_n64dd_jp",
        "n64dd_ipl_us.bin" to "fw_n64dd_us",
        "n64dd_ipl_dev.bin" to "fw_n64dd_dev",
        "[bios] nintendo 64dd ipl (japan) (v1.0).zip" to "fw_n64dd_jp",
        "[bios] nintendo 64dd ipl (japan) (v1.2).zip" to "fw_n64dd_jp",
        "[bios] nintendo 64dd ipl (usa) (proto).zip" to "fw_n64dd_us",
        "[bios] nintendo 64dd ipl (usa) (proto) (v1.0).zip" to "fw_n64dd_us",
        "[bios] nintendo 64dd ipl (usa) (proto) (v1.1).zip" to "fw_n64dd_us",
        "nintendo 64dd ipl (japan).bin" to "fw_n64dd_jp",
        "nintendo 64dd ipl (usa) (proto).bin" to "fw_n64dd_us",
        "pif.ntsc.rom" to "fw_n64_pif_ntsc",
        "pif.pal.rom" to "fw_n64_pif_pal",
        "segacd_usa.bin" to "fw_mcd_us",
        "segacd_jp.bin" to "fw_mcd_jp",
        "segacd_eu.bin" to "fw_mcd_eu",
        "mcd_v1_10.bin" to "fw_mcd_us",
        "bios_cd_u.bin" to "fw_mcd_us",
        "mcd_v1_10j.bin" to "fw_mcd_jp",
        "bios_cd_j.bin" to "fw_mcd_jp",
        "mcd_v1_10e.bin" to "fw_mcd_eu",
        "bios_cd_e.bin" to "fw_mcd_eu",
        "32x_g_bios.bin" to "fw_32x_g",
        "32x_m_bios.bin" to "fw_32x_m",
        "32x_s_bios.bin" to "fw_32x_s",
        "sh2.boot.mrom" to "fw_32x_m",
        "sh2.boot.srom" to "fw_32x_s",
        "syscard1.pce" to "fw_pce_cd_1_jp",
        "syscard3.pce" to "fw_pce_cd_3_jp",
        "syscard3u.pce" to "fw_pce_cd_3_us",
        "syscard3us.pce" to "fw_pce_cd_3_us",
        "gexpress.pce" to "fw_pce_cd_ge_jp",
        "disksys.rom" to "fw_fds",
        "coleco.rom" to "fw_coleco",
        "colecovision.rom" to "fw_coleco",
        "gba_bios.bin" to "fw_gba",
        "dmg_boot.bin" to "fw_gb_boot",
        "cgb_boot.bin" to "fw_gbc_boot",
        "sgb_boot.bin" to "fw_sgb_boot",
        // Alternate filenames
        "gb_bios.bin" to "fw_gb_boot",
        "gbc_bios.bin" to "fw_gbc_boot",
        "bios_u.sms" to "fw_ms_us",
        "bios_j.sms" to "fw_ms_jp",
        "bios_e.sms" to "fw_ms_eu",
        "msx.rom" to "fw_msx",
        "msx2.rom" to "fw_msx2_main",
        "msx2ext.rom" to "fw_msx2_sub",
        "gg_bios.bin" to "fw_gg",
        "game_gear_bios.bin" to "fw_gg",
        // Common PSX BIOS filenames
        "scph1000.bin" to "fw_psx_jp",
        "scph1001.bin" to "fw_psx_us",
        "scph7001.bin" to "fw_psx_us",
        "scph7003.bin" to "fw_psx_eu",
        "scph7502.bin" to "fw_psx_eu",
        // FDS alternate
        "fds.rom" to "fw_fds",
        // ZX Spectrum 48K system ROM (ares's copy is used when none is set)
        "48.rom" to "fw_zx48",
        "zx48.rom" to "fw_zx48",
        "zxspectrum.rom" to "fw_zx48",
        "spectrum.rom" to "fw_zx48",
        "zx spectrum 48k.rom" to "fw_zx48",
        "zx spectrum (48k).rom" to "fw_zx48",
        "[bios] zx spectrum (48k).rom" to "fw_zx48",
        "sinclair zx spectrum.rom" to "fw_zx48",
        // ZX Spectrum 128 BIOS + SUB (e.g. Fuse 128-0.rom / 128-1.rom)
        "128-0.rom" to "fw_zx128",
        "128_0.rom" to "fw_zx128",
        "1280.rom" to "fw_zx128",
        "zx128.rom" to "fw_zx128",
        "zx spectrum 128.rom" to "fw_zx128",
        "[bios] zx spectrum 128.rom" to "fw_zx128",
        "128-1.rom" to "fw_zx128_sub",
        "128_1.rom" to "fw_zx128_sub",
        "1281.rom" to "fw_zx128_sub",
        "zx128_sub.rom" to "fw_zx128_sub"
    )

    // Firmware key aliases: when one key matches via scan, also populate its aliases.
    // E.g. neogeo.zip → fw_ng_bios should also show under fw_ng_aes and fw_ng_mvs.
    private val biosAliases = mapOf(
        "fw_ng_bios" to listOf("fw_ng_aes", "fw_ng_mvs"),
        "fw_ng_aes"  to listOf("fw_ng_bios", "fw_ng_mvs"),
        "fw_ng_mvs"  to listOf("fw_ng_bios", "fw_ng_aes"),
    )

    // Keyword-based heuristic firmware matching.  Works for ANY filename
    // convention — only checks whether the name contains recognizable keywords.
    private fun keywordFirmwareMatch(name: String): String? {
        // N64DD IPL: contains both "64dd" and "ipl"
        if (name.contains("64dd") && name.contains("ipl")) {
            if (name.contains("dev")) return "fw_n64dd_dev"
            if (name.contains("usa") || name.contains("(us") || name.contains("proto")) return "fw_n64dd_us"
            return "fw_n64dd_jp"
        }
        return null
    }

    private val biosCrcMap = mapOf(
        "1105ca35" to "fw_psx_us", // SCPH-5501
        "ff3d245b" to "fw_psx_jp", // SCPH-5500
        "3273398d" to "fw_psx_eu", // SCPH-5502
        "74360e22" to "fw_n64dd_jp", // N64DD IPL (JP)
        "5ec82be9" to "fw_n64_pif_sm5", // N64 PIF SM5
        "4353387a" to "fw_n64_pif_ntsc", // N64 PIF NTSC
        "59b859e7" to "fw_n64_pif_pal",   // N64 PIF PAL
        "32fce34a" to "fw_gb_boot",      // DMG Boot ROM
        "ebf565e8" to "fw_gbc_boot",     // CGB Boot ROM
        "e03ee2d7" to "fw_sgb_boot",      // SGB Boot ROM
        // ColecoVision
        "3c0c41ef" to "fw_coleco",  // ColecoVision BIOS (std 8KB)
        "6605af34" to "fw_coleco",  // ColecoVision BIOS variant
        "ff0ecca5" to "fw_coleco",  // BIOS.col
        "c8338226" to "fw_coleco",  // colecoa.rom
        "32584700" to "fw_coleco",  // Coleco_Bios.bin
        "df1c9a84" to "fw_coleco",  // czz50.rom
        // Common PSX BIOS CRC variants
        "924e3926" to "fw_psx_us",      // SCPH-1001
        "55847d8c" to "fw_psx_jp",      // SCPH-1000
        "a56e4c9e" to "fw_psx_eu",      // SCPH-7003
        "f7b04630" to "fw_psx_us",      // SCPH-7001
        // Master System BIOS
        "48cd46be" to "fw_ms_us",        // SMS BIOS US
        "80eb3c3c" to "fw_ms_jp",        // SMS BIOS JP
        "d0569c83" to "fw_ms_eu",        // SMS BIOS EU
        // Game Gear BIOS
        "eecf3fa1" to "fw_gg",           // Game Gear BIOS
        // 32X BIOS
        "5c12eae8" to "fw_32x_g",        // 32X_G_BIOS.BIN (68000)
        "dd9c46b8" to "fw_32x_m",        // 32X_M_BIOS.BIN (SH-2 master)
        "bfda1fe5" to "fw_32x_s",        // 32X_S_BIOS.BIN (SH-2 slave)
        // MSX BIOS
        "ee229390" to "fw_msx",          // MSX BIOS JP
        "fcb98b8a" to "fw_msx2_main",    // MSX2 MAIN JP
        "57798735" to "fw_msx2_sub",      // MSX2 SUB JP
        // Neo Geo Pocket
        "11726b6d" to "fw_ngp",          // NGP BIOS (RetroArch standard)
        "6232df8d" to "fw_ngp",          // NGP BIOS (World, 64KB)
        "cdc1a5c2" to "fw_ngpc",         // NGPC BIOS (RetroArch standard)
        "6eeb6f40" to "fw_ngpc"          // NGPC BIOS (World, 64KB)
    )

    fun scanFirmware(context: Context) {
        val firmwareUriString = settings.value.firmwarePath
        if (firmwareUriString.isEmpty()) return

        viewModelScope.launch(Dispatchers.IO) {
            try {
                val rootUri = Uri.parse(firmwareUriString)
                val rootDir = DocumentFile.fromTreeUri(context, rootUri)
                if (rootDir == null) {
                    Log.e("Phobos", "Firmware scan: DocumentFile.fromTreeUri returned null for $firmwareUriString — SAF permission lost?")
                    return@launch
                }
                val allFiles = rootDir.listFiles()
                Log.i("Phobos", "Firmware scan: found ${allFiles.size} files in firmware folder")
                var matchedCount = 0
                allFiles.forEach { file ->
                    val name = file.name?.lowercase() ?: ""
                    Log.d("Phobos", "Firmware scan: checking '$name'")
                    
                    // 1. Check by filename (matches raw .bin/.rom and known .zip names)
                    val keyByName = biosMap[name]
                    if (keyByName != null) {
                        Log.i("Phobos", "Firmware scan: MATCHED '$name' -> $keyByName")
                        settingsStore.setSystemFirmwarePath(keyByName, file.uri.toString())
                        // Also populate aliases (e.g. neogeo.zip shows under AES and MVS too)
                        biosAliases[keyByName]?.forEach { alias ->
                            settingsStore.setSystemFirmwarePath(alias, file.uri.toString())
                        }
                        matchedCount++
                        return@forEach
                    }

                    // 2. ZIP archives: match by inner file name AND inner CRC32.
                    // Many BIOS sets ship as No-Intro "[BIOS] ... .zip"; the outer
                    // zip's CRC is meaningless, so we must inspect the contents.
                    if (name.endsWith(".zip")) {
                        var zipMatched = false
                        try {
                            context.contentResolver.openInputStream(file.uri)?.use { input ->
                                java.util.zip.ZipInputStream(java.io.BufferedInputStream(input)).use { zis ->
                                    var entry = zis.nextEntry
                                    while (entry != null) {
                                        if (!entry.isDirectory) {
                                            val innerName = entry.name.substringAfterLast('/').lowercase()
                                            // inner name match
                                            val keyByInnerName = biosMap[innerName]
                                            if (keyByInnerName != null) {
                                                Log.i("Phobos", "Firmware scan: ZIP '$name' inner '$innerName' -> $keyByInnerName")
                                                settingsStore.setSystemFirmwarePath(keyByInnerName, file.uri.toString())
                                                biosAliases[keyByInnerName]?.forEach { alias ->
                                                    settingsStore.setSystemFirmwarePath(alias, file.uri.toString())
                                                }
                                                matchedCount++
                                                zipMatched = true
                                                break
                                            }
                                            // inner CRC match
                                            val crc = CRC32()
                                            val buffer = ByteArray(8192)
                                            var bytesRead: Int
                                            while (zis.read(buffer).also { bytesRead = it } != -1) {
                                                crc.update(buffer, 0, bytesRead)
                                            }
                                            val crcString = String.format("%08x", crc.value)
                                            val keyByCrc = biosCrcMap[crcString]
                                            if (keyByCrc != null) {
                                                Log.i("Phobos", "Firmware scan: ZIP '$name' inner '$innerName' CRC=$crcString -> $keyByCrc")
                                                settingsStore.setSystemFirmwarePath(keyByCrc, file.uri.toString())
                                                biosAliases[keyByCrc]?.forEach { alias ->
                                                    settingsStore.setSystemFirmwarePath(alias, file.uri.toString())
                                                }
                                                matchedCount++
                                                zipMatched = true
                                                break
                                            }
                                        }
                                        zis.closeEntry()
                                        entry = zis.nextEntry
                                    }
                                }
                            }
                        } catch (e: Exception) {
                            Log.e("Phobos", "Error reading ZIP firmware ${file.name}: ${e.message}")
                        }
                        if (zipMatched) return@forEach
                    }

                    // 2.5 Keyword heuristic: match by filename content, not exact name.
                    //     Catches any naming convention (e.g. "N64DD IPLROM [Japan].n64").
                    val keyByKeyword = keywordFirmwareMatch(name)
                    if (keyByKeyword != null) {
                        Log.i("Phobos", "Firmware scan: KEYWORD MATCHED '$name' -> $keyByKeyword")
                        settingsStore.setSystemFirmwarePath(keyByKeyword, file.uri.toString())
                        biosAliases[keyByKeyword]?.forEach { alias ->
                            settingsStore.setSystemFirmwarePath(alias, file.uri.toString())
                        }
                        matchedCount++
                        return@forEach
                    }

                    // 3. Check by CRC32 (raw non-zip files)
                    try {
                        context.contentResolver.openInputStream(file.uri)?.use { input ->
                            val crc = CRC32()
                            val buffer = ByteArray(8192)
                            var bytesRead: Int
                            while (input.read(buffer).also { bytesRead = it } != -1) {
                                crc.update(buffer, 0, bytesRead)
                            }
                            val crcString = String.format("%08x", crc.value)
                            val keyByCrc = biosCrcMap[crcString]
                            if (keyByCrc != null) {
                                Log.i("Phobos", "Firmware scan: CRC MATCHED '$name' (CRC=$crcString) -> $keyByCrc")
                                settingsStore.setSystemFirmwarePath(keyByCrc, file.uri.toString())
                                matchedCount++
                            }
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Error calculating CRC for ${file.name}: ${e.message}")
                    }
                }
                Log.i("Phobos", "Firmware scan complete: $matchedCount firmware file(s) matched")
            } catch (e: Exception) {
                Log.e("Phobos", "Error scanning firmware: ${e.message}")
            }
        }
    }

    fun exportLogs(context: Context) {
        viewModelScope.launch(Dispatchers.IO) {
            val currentLogs = logs.value
            val logText = currentLogs.joinToString("\n") { entry ->
                val levelName = when (entry.level) {
                    LogLevel.TRACE.ordinal -> "TRACE"
                    LogLevel.DEBUG.ordinal -> "DEBUG"
                    LogLevel.INFO.ordinal -> "INFO"
                    LogLevel.WARN.ordinal -> "WARN"
                    LogLevel.ERROR.ordinal -> "ERROR"
                    LogLevel.FATAL.ordinal -> "FATAL"
                    else -> "LOG"
                }
                "[$levelName] ${entry.message}"
            }
            
            if (logText.isEmpty()) {
                Log.d("Phobos", "No logs to export")
                return@launch
            }
            val fileName = "phobos_logs_${System.currentTimeMillis()}.txt"

            try {
                val file = File(context.cacheDir, fileName)
                file.writeText(logText)

                val uri = FileProvider.getUriForFile(
                    context,
                    "${context.packageName}.fileprovider",
                    file
                )

                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }

                context.startActivity(Intent.createChooser(intent, "Share Phobos Logs"))
            } catch (e: Exception) {
                Log.e("Phobos", "Error exporting logs: ${e.message}")
            }
        }
    }

    fun togglePause() {
        _isPaused.value = !_isPaused.value
        PhobosCore.setPause(_isPaused.value)
        if (!_isPaused.value) controlCapture.cancel()
    }

    fun scanRoms(context: Context, systemName: String, directoryUris: List<Uri>) {
        viewModelScope.launch {
            val extensions = PhobosCore.getSystemExtensions(systemName)
            // CD systems also list .m3u playlists, which gather a game's discs.
            val discSystem = "cue" in extensions || "chd" in extensions
            val foundRoms = withContext(Dispatchers.IO) {
                val result = mutableListOf<RomFile>()
                directoryUris.forEach { uri ->
                    val rootDir = DocumentFile.fromTreeUri(context, uri)
                    if (rootDir != null) {
                        scanRecursive(rootDir, if (discSystem) extensions + "m3u" else extensions, result)
                    }
                }
                val files = result.distinctBy { it.uri }
                (if (discSystem) withDiscSets(context, files) else files).sortedBy { it.name }
            }
            _roms.value = foundRoms
        }
    }

    /**
     * Each multi-disc game as one entry: the files an .m3u playlist lists, or files of one folder that
     * differ only in "(Disc N)". A playlist that lists none of the scanned files is left out.
     */
    private fun withDiscSets(context: Context, files: List<RomFile>): List<RomFile> {
        val (playlists, others) = files.partition { it.name.endsWith(".m3u", ignoreCase = true) }
        val listed = mutableSetOf<RomFile>()
        val fromPlaylists = playlists.mapNotNull { playlist ->
            val text = runCatching {
                context.contentResolver.openInputStream(playlist.uri)?.bufferedReader()?.use { it.readText() }
            }.getOrNull() ?: return@mapNotNull null
            val playlistPath = pathOf(playlist)
            val discs = m3uEntries(text).mapNotNull { entry ->
                // Beside the playlist, or wherever the entry's path leads from the playlist's folder.
                others.firstOrNull { it.parentUri == playlist.parentUri && it.name.equals(entry, ignoreCase = true) }
                    ?: playlistPath?.let { m3uEntryPath(it, entry) }
                        ?.let { path -> others.firstOrNull { pathOf(it).equals(path, ignoreCase = true) } }
            }
            if (discs.isEmpty()) return@mapNotNull null
            listed += discs
            RomFile(playlist.name, discs.first().uri, playlist.parentUri, discs)
        }
        val (sets, singles) = groupDiscSets(others.filter { it !in listed }, { it.name }, { it.parentUri })
        return fromPlaylists + sets.map { RomFile(it.title, it.discs.first().uri, it.discs.first().parentUri, it.discs) } + singles
    }

    // A scanned file's path, to follow a playlist's entries: the storage provider's document id
    // ("EBFF-F6C0:ROMs/psx/Game/Game.m3u"), or a file's own path. Other providers' ids aren't paths.
    private fun pathOf(file: RomFile): String? = when (file.uri.scheme) {
        "file" -> file.uri.path
        "content" -> file.uri.takeIf { it.authority == "com.android.externalstorage.documents" }
            ?.let { runCatching { DocumentsContract.getDocumentId(it) }.getOrNull() }
        else -> null
    }

    private fun scanRecursive(directory: DocumentFile, extensions: List<String>, result: MutableList<RomFile>) {
        directory.listFiles().forEach { file ->
            if (file.isDirectory) {
                scanRecursive(file, extensions, result)
            } else {
                val name = file.name?.lowercase() ?: ""
                val ext = name.substringAfterLast('.', "")
                if (ext.isNotEmpty() && (extensions.contains(ext) || ext == "zip")) {
                    result.add(RomFile(file.name ?: "Unknown", file.uri, directory.uri))
                }
            }
        }
    }

    /** [disc] is the disc a multi-disc game starts from. */
    fun loadRom(context: Context, systemName: String, rom: RomFile, disc: Int = 0) {
        externalSession = false
        startLoad(context, systemName, rom, disc)
    }

    /** Loads the running game's file again (the Reload hotkey), wherever it was started from, from the disc in the drive. */
    fun reloadGame(context: Context) {
        val rom = loadedRom ?: return
        val system = currentSystemName.takeIf { it.isNotEmpty() } ?: return
        startLoad(context, system, rom, _currentDisc.value.coerceAtLeast(0))
    }

    /** [beforeLoad] runs once the previous game is unloaded, with its battery saves written to disk. */
    private fun startLoad(context: Context, systemName: String, rom: RomFile, disc: Int = 0, beforeLoad: (suspend () -> Unit)? = null) {
        viewModelScope.launch(Dispatchers.IO) {
            CoreSession.lock.withLock {
                // A loaded game is saved and unloaded first, as quitting it would, even one a replaced activity
                // loaded; reloading the running game starts it over without saving it.
                val reload = CoreSession.owner === sessionToken && CoreSession.system == systemName && CoreSession.rom == rom.name
                closeSessionLocked(autoSave = !reload)
                beforeLoad?.invoke()
                loadLocked(context, systemName, rom, disc)
            }
        }
    }

    private suspend fun loadLocked(context: Context, systemName: String, rom: RomFile, disc: Int = 0) {
        withContext(Dispatchers.IO) {
            // For the merged "ZX Spectrum" entry, native detects 48K vs 128K by
            // CONTENT (TAP header block scan) — see PhobosRunner initialize().
            // effectiveSystem stays "ZX Spectrum"; native upgrades to
            // "ZX Spectrum 128" internally when the tape is a 128K loader.
            val effectiveSystem = systemName
            Log.d("Phobos", "loadRom starting: system='$effectiveSystem', name='${rom.name}'")

            // Reset driver suggestion for new game load.
            // Only N64 Vulkan games trigger this, but reset here to be safe.
            _showDriverSuggestion.value = false
            driverSuggestionShown = false

            // Sync current settings to native before loading
            val currentSettings = settings.value
            
            // For Neo Geo, copy neogeo.zip to mia_temp via ContentResolver
            // (firmware paths are content URIs, not real filesystem paths)
            var ngBiosPresent = false
            if (systemName in NEOGEO_ZIP_SYSTEMS) {
                val miaTempPath = File(context.cacheDir, "mia_temp")
                if (!miaTempPath.exists()) miaTempPath.mkdirs()
                val destFile = File(miaTempPath, "neogeo.zip")
                var copied = false
                val firmwareUri = currentSettings.systemFirmwarePaths["fw_ng_bios"]
                if (firmwareUri != null) {
                    try {
                        context.contentResolver.openInputStream(Uri.parse(firmwareUri))?.use { input ->
                            destFile.outputStream().use { output -> input.copyTo(output) }
                        }
                        copied = destFile.exists()
                    } catch (_: Exception) {}
                }
                if (!copied) {
                    rom.parentUri?.let { pUri ->
                        val parentDir = DocumentFile.fromTreeUri(context, pUri)
                        parentDir?.findFile("neogeo.zip")?.let { biosFile ->
                            try {
                                context.contentResolver.openInputStream(biosFile.uri)?.use { input ->
                                    destFile.outputStream().use { output -> input.copyTo(output) }
                                }
                                copied = destFile.exists()
                                if (copied) Log.i("Phobos", "Copied neogeo.zip from ROM folder")
                            } catch (_: Exception) {}
                        }
                    }
                }
                if (!copied) {
                    try {
                        val romFile = if (rom.uri.scheme == "file") java.io.File(rom.uri.path ?: "") else null
                        val parent = romFile?.parentFile
                        val biosFile = parent?.let { java.io.File(it, "neogeo.zip") }?.takeIf { it.exists() }
                            ?: parent?.parentFile?.let { java.io.File(it, "neogeo.zip") }?.takeIf { it.exists() }
                        if (biosFile != null && biosFile.exists()) {
                            biosFile.inputStream().use { input ->
                                destFile.outputStream().use { output -> input.copyTo(output) }
                            }
                            copied = destFile.exists()
                            if (copied) Log.i("Phobos", "Copied neogeo.zip from file path ${biosFile.path}")
                        }
                    } catch (_: Exception) {}
                }
                if (copied) Log.i("Phobos", "neogeo.zip ready in mia_temp")
                ngBiosPresent = copied
            }
            Log.d("Phobos", "Syncing settings to native: Driver='${currentSettings.customDriverPath}', Recompiler=${currentSettings.n64Recompiler}")
            
            PhobosCore.setFastBoot(currentSettings.fastBoot)
            PhobosCore.setSkipBootRom(currentSettings.skipBootRom)
            PhobosCore.setN64Recompiler(currentSettings.n64Recompiler)
            PhobosCore.setN64Upscale(currentSettings.n64Upscale)
            PhobosCore.setRegion(currentSettings.regionPreference.ordinal)
            PhobosCore.setFastForwardSpeed(currentSettings.fastForwardSpeed)
            PhobosCore.setNgcdLoadSpeed(currentSettings.ngcdLoadSpeed)
            PhobosCore.setZxLoadSpeed(currentSettings.zxLoadSpeed)
            PhobosCore.setZxTapeAuto(currentSettings.zxTapeAuto)
            PhobosCore.setCustomDriverPath(currentSettings.customDriverPath)
            PhobosCore.setPs1AnalogMode(currentSettings.ps1AnalogMode)
            PhobosCore.setN64ExpansionPak(currentSettings.n64ExpansionPak)
            PhobosCore.setN64DisableVIProcessing(currentSettings.n64DisableVIProcessing)
            PhobosCore.setN64WeaveDeinterlacing(currentSettings.n64WeaveDeinterlacing)
            PhobosCore.setN64SupersampleScanout(currentSettings.n64SupersampleScanout)
            PhobosCore.setN64ViOverclock(currentSettings.n64ViOverclock)
            PhobosCore.setN64CountPerOp(if (currentSettings.n64UseDefaultCountPerOp) 2 else currentSettings.n64CountPerOp)
            PhobosCore.setN64CpuOverclock(if (currentSettings.n64UseDefaultCpuOverclock) 0 else currentSettings.n64CpuOverclock)
            PhobosCore.setN64AsyncRdp(currentSettings.n64AsyncRdp)
            PhobosCore.setN64FasterSync(currentSettings.n64FasterSync)
            PhobosCore.setN64SkipCaches(currentSettings.n64SkipCaches)
            PhobosCore.setN64RspTaskMode(currentSettings.n64RspTaskMode)
            PhobosCore.setPinFastestCore(currentSettings.pinFastestCore)
            PhobosCore.setBusyWaitPacing(currentSettings.busyWaitPacing)
            PhobosCore.setVideoSettings(currentSettings.overscan, currentSettings.colorEmulation, currentSettings.interframeBlending)
            PhobosCore.setN64Pak(currentSettings.n64Pak)
            // Push the persisted N64 debug-logging toggle on EVERY load so native
            // matches DataStore at emulation start. The init block pushes the
            // DataStore default (false) before the persisted value loads, and the
            // EmulatorScreen LaunchedEffect only fires when its key changes —
            // both leave native OFF while the UI shows ON until a manual
            // off→on re-toggle. loadRom is the one place all other settings are
            // guaranteed to sync, so sync this one here too.
            PhobosCore.setN64DebugLogging(currentSettings.n64DebugLogging)

            // ZX per-core control scheme + rebinds + toggles (keyboard cores).
            if (effectiveSystem.contains("ZX Spectrum", ignoreCase = true)) {
                PhobosCore.setZxControlScheme(zxControlScheme(currentSettings, effectiveSystem, rom.name))
                PhobosCore.setZxStickToKeys(currentSettings.zxStickToKeys[effectiveSystem] ?: false)
                PhobosCore.setZxReversePitch(currentSettings.zxReversePitch[effectiveSystem] ?: false)
                val binds = currentSettings.zxKeyBindings[effectiveSystem] ?: emptyMap()
                binds.forEach { (label, bit) -> PhobosCore.setZxKeyBinding(label, bit) }
                PhobosCore.setZxTapeMuted(currentSettings.zxTapeMuted)
            }

            // Resolve the user-configured Saves Path (SAF content:// URI) to a
            // real filesystem path native code can write to; fall back to the
            // internal saves dir when unset or unresolvable.
            val savesDir = resolveSafPath(currentSettings.savesPath)
                ?: File(context.filesDir, "saves").absolutePath
            PhobosCore.setSavesPath(savesDir)
            Log.i("Phobos", "Saves path resolved: $savesDir")
            PhobosCore.setMemoryCardKey(withoutDiscNumber(romTitle(rom.name)))

            // Vulkan pipeline cache dir (Task 40): user-configured path, else
            // internal default (files/vulkan_cache).
            val cacheDir = resolveSafPath(currentSettings.vulkanCachePath)
                ?: File(context.filesDir, "vulkan_cache").absolutePath
            PhobosCore.setVulkanCachePath(cacheDir)
            Log.i("Phobos", "Vulkan cache path resolved: $cacheDir")
            _isPaused.value = false
            _isLoaded.value = false
            _loadedGame.value = null
            PhobosCore.setPause(false)

            // Set writable temp directory for MIA
            val miaTempPath = File(context.cacheDir, "mia_temp").absolutePath
            
            val tempDir = File(miaTempPath)
            if (!tempDir.exists()) {
                tempDir.mkdirs()
            }
            removeStaleGameCopies(tempDir)
            PhobosCore.setTempFilePath(miaTempPath)

            // Sync Firmware Path and Mappings
            // For each mapped firmware, if it's a URI, copy it to a temp file so native code can read it.
            // Only the firmware the game's system reads; a system the app doesn't list gets all of it.
            val firmwareKeys = if (effectiveSystem in PhobosCore.enumerateSystems()) SYSTEM_FIRMWARE[effectiveSystem].orEmpty() else null
            currentSettings.systemFirmwarePaths.forEach { (key, uriString) ->
                if (firmwareKeys != null && key !in firmwareKeys) return@forEach
                try {
                    val firmwareFileName = "fw_$key"
                    val tempFile = File(miaTempPath, firmwareFileName)
                    
                    if (uriString.startsWith("content://")) {
                        val uri = Uri.parse(uriString)
                        // Use DocumentFile to handle permissions properly if it's from SAF
                        val docFile = DocumentFile.fromSingleUri(context, uri)
                        if (docFile != null && docFile.exists()) {
                            val fileName = docFile.name?.lowercase() ?: ""
                            context.contentResolver.openInputStream(uri)?.use { input ->
                                if (fileName.endsWith(".zip")) {
                                    // No-Intro BIOS sets ship as .zip. Extract the first
                                    // non-directory entry to the temp file — the raw
                                    // zip bytes would be garbage when loaded as an IPL.
                                    java.util.zip.ZipInputStream(java.io.BufferedInputStream(input)).use { zis ->
                                        var entry = zis.nextEntry
                                        var extracted = false
                                        while (entry != null && !extracted) {
                                            if (!entry.isDirectory) {
                                                tempFile.outputStream().use { output ->
                                                    zis.copyTo(output)
                                                }
                                                extracted = true
                                            }
                                            zis.closeEntry()
                                            entry = zis.nextEntry
                                        }
                                        if (!extracted) Log.w("Phobos", "ZIP firmware ${docFile.name} had no files")
                                    }
                                } else {
                                    tempFile.outputStream().use { output ->
                                        input.copyTo(output)
                                    }
                                }
                            }
                            PhobosCore.mapFirmwareFile(key, tempFile.absolutePath)
                        } else {
                            Log.w("Phobos", "Firmware file not found or inaccessible: $uriString")
                        }
                    } else {
                        PhobosCore.mapFirmwareFile(key, uriString)
                    }
                } catch (e: Exception) {
                    Log.e("Phobos", "Failed to map firmware $key: ${e.message}")
                }
            }

            val missingFirmware = PhobosCore.missingFirmware(effectiveSystem)
            if (missingFirmware.isNotEmpty()) {
                Log.w("Phobos", "$effectiveSystem needs firmware: $missingFirmware")
                _firmwareRequired.value = FirmwareRequired(effectiveSystem, missingFirmware)
                return@withContext
            }

            // A multi-disc game starts from the chosen disc; its session, states and cards go by the game's name.
            val discIndex = if (rom.discs.isEmpty()) -1 else disc.coerceIn(0, rom.discs.lastIndex)
            val file = if (discIndex >= 0) rom.discs[discIndex] else rom
            try {
                val inPlace = inPlacePath(effectiveSystem, file)
                val descriptor = if (inPlace == null) openGameFile(context, file, startsGame = true) else null
                if (inPlace != null || descriptor != null) descriptor.use {
                    if (inPlace != null) {
                        PhobosCore.setRomFd(-1)
                        PhobosCore.setRomPath(inPlace)
                    } else {
                        PhobosCore.setRomFd(descriptor!!.detachFd())
                    }
                    val success = PhobosCore.loadRom(effectiveSystem, file.uri.toString(), rom.name)
                    if (success) {
                        CoreSession.system = effectiveSystem
                        CoreSession.rom = rom.name
                        CoreSession.owner = sessionToken
                        loadedRom = rom
                        _isLoaded.value = true
                        currentSystemName = effectiveSystem
                        currentRomName = rom.name
                        _loadedGame.value = ControlLevel.Game(effectiveSystem, rom.name)
                        _loadedDiscs.value = rom.discs
                        _currentDisc.value = discIndex
                        if (rom.discs.size > 1) rememberDisc(effectiveSystem, rom.name, discIndex)
                        // Auto-Load State (Task 17): restore the auto-saved state
                        // right after the core is loaded but BEFORE the emulation
                        // thread starts (the thread is spawned by EmulatorScreen's
                        // LaunchedEffect(isLoaded) after this coroutine returns, so
                        // no race with runMutex). Silently skip when no auto state
                        // exists (performLoadState returns false + logs only).
                        // A state from another disc is skipped: the player chose to start from this one.
                        if (settings.value.autoLoadState) {
                            val autoStateDisc = if (rom.discs.size > 1) stateDisc(effectiveSystem, rom.name, AUTO_STATE_SLOT) else null
                            if (autoStateDisc != null && autoStateDisc != discIndex) {
                                Log.i("Phobos", "Auto-load skipped: the auto state is from disc ${autoStateDisc + 1}")
                            } else try {
                                performLoadState(effectiveSystem, rom.name, AUTO_STATE_SLOT)
                            } catch (e: Exception) {
                                Log.e("Phobos", "Auto-load failed: ${e.message}")
                            }
                        }
                    } else {
                        Log.e("Phobos", "Native loadRom failed for $effectiveSystem")
                        // Broken core (ZX 128K / PCE): native refuses before
                        // spawning any threads. Neo Geo refuses only when its
                        // mandatory BIOS is absent. Surface a clear popup
                        // instead of a hang/crash.
                        if (effectiveSystem in NEOGEO_ZIP_SYSTEMS) {
                            // Only blame the BIOS when neogeo.zip was genuinely
                            // absent. If it was copied but the ROM still failed,
                            // the ROM itself is the problem (wrong/missing game).
                            if (ngBiosPresent) _neoGeoRomLoadFailed.value = effectiveSystem
                            else _biosRequired.value = effectiveSystem
                        } else if (effectiveSystem.contains("ZX Spectrum", ignoreCase = true) ||
                            effectiveSystem.contains("PC Engine", ignoreCase = true) ||
                            effectiveSystem.contains("SuperGrafx", ignoreCase = true)) {
                            _unsupportedSystem.value = effectiveSystem
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error opening ROM FD: ${e.message}")
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "Couldn't open ${file.name}: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    fun dismissUnsupportedSystem() { _unsupportedSystem.value = null }

    fun loadSecondaryRom(context: Context, systemName: String, rom: RomFile) {
        viewModelScope.launch(Dispatchers.IO) {
            Log.d("Phobos", "loadSecondaryRom starting: system='$systemName', name='${rom.name}'")
            // A picked file that is one of the game's discs counts as that disc; any other file as none.
            if (insertDisc(context, systemName, rom)) _currentDisc.value = _loadedDiscs.value.indexOfFirst { it.uri == rom.uri }
        }
    }

    /** Puts disc [index] of the running game in the drive, as swapping discs on the console would. */
    fun changeDisc(context: Context, index: Int) {
        viewModelScope.launch(Dispatchers.IO) { swapToDisc(context, index) }
    }

    private suspend fun swapToDisc(context: Context, index: Int): Boolean {
        val disc = _loadedDiscs.value.getOrNull(index) ?: return false
        val system = currentSystemName.takeIf { it.isNotEmpty() } ?: return false
        val inserted = insertDisc(context, system, disc)
        if (inserted) {
            _currentDisc.value = index
            rememberDisc(system, currentRomName, index)
        }
        withContext(Dispatchers.Main) {
            Toast.makeText(context, if (inserted) "Disc ${index + 1} inserted" else "Couldn't insert disc ${index + 1}", Toast.LENGTH_SHORT).show()
        }
        return inserted
    }

    /** Hands [file] to the core as the new disc, or the 64DD disk. */
    private fun insertDisc(context: Context, systemName: String, file: RomFile): Boolean =
        try {
            val inPlace = inPlacePath(systemName, file)
            val descriptor = if (inPlace == null) openGameFile(context, file, startsGame = false) else null
            if (inPlace != null || descriptor != null) {
                descriptor.use {
                    if (inPlace != null) {
                        PhobosCore.setSecondaryRomFd(-1)
                        PhobosCore.setSecondaryRomPath(inPlace)
                    } else {
                        PhobosCore.setSecondaryRomFd(descriptor!!.detachFd())
                    }
                    PhobosCore.loadSecondaryRom(systemName, file.uri.toString()).also { success ->
                        if (!success) Log.e("Phobos", "Native loadSecondaryRom failed for $systemName")
                    }
                }
            } else false
        } catch (e: Exception) {
            Log.e("Phobos", "Error opening Secondary ROM FD: ${e.message}")
            false
        }

    /**
     * [file]'s path when it can load where it is: a .cue or .chd disc of a system that only reads its discs,
     * in a folder the app can read by path. Null means native code copies it from a descriptor instead.
     */
    private fun inPlacePath(systemName: String, file: RomFile): String? {
        if (systemName !in IN_PLACE_SYSTEMS) return null
        val name = file.name.lowercase()
        if (!name.endsWith(".cue") && !name.endsWith(".chd")) return null
        val path = when (file.uri.scheme) {
            "file" -> file.uri.path
            "content" -> file.uri.takeIf { it.authority == "com.android.externalstorage.documents" }?.let { resolveSafPath(it.toString()) }
            else -> null
        } ?: return null
        return path.takeIf { File(it).canRead() }
    }

    /**
     * The descriptor native code copies [file] from. Native code copies only that file into mia_temp, where a
     * .cue sheet's tracks would be missing, so they're copied into mia_temp/cue_tracks and the sheet handed
     * over names them there. [startsGame] clears the previous game's tracks; a disc change keeps the
     * session's, since the disc in the drive still reads them until the new one is in.
     */
    private fun openGameFile(context: Context, file: RomFile, startsGame: Boolean): ParcelFileDescriptor? {
        if (!file.name.endsWith(".cue", ignoreCase = true)) return context.contentResolver.openFileDescriptor(file.uri, "r")
        val tracksDir = File(context.cacheDir, "mia_temp/$CUE_TRACKS")
        if (startsGame) tracksDir.deleteRecursively()
        tracksDir.mkdirs()
        val sheet = context.contentResolver.openInputStream(file.uri)?.bufferedReader()?.use { it.readText() } ?: return null
        for (track in cueTracks(sheet)) {
            val (source, size) = trackBeside(context, file, track)
                ?: throw FileNotFoundException("$track, which ${file.name} names, isn't beside it")
            val target = File(tracksDir, track.substringAfterLast('/'))
            if (target.length() == size) continue
            context.contentResolver.openInputStream(source)?.use { input -> target.outputStream().use { input.copyTo(it) } }
                ?: throw FileNotFoundException(track)
        }
        val handedOver = File(context.cacheDir, "cue-sheet.cue")
        handedOver.writeText(cueWithTracksIn(sheet, CUE_TRACKS))
        return ParcelFileDescriptor.open(handedOver, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    /** A .cue sheet's track, looked up from the sheet's folder, and its size. */
    private fun trackBeside(context: Context, sheet: RomFile, track: String): Pair<Uri, Long>? {
        if (sheet.uri.scheme == "file") {
            val trackFile = File(File(sheet.uri.path ?: return null).parentFile, track)
            return if (trackFile.isFile) Uri.fromFile(trackFile) to trackFile.length() else null
        }
        var dir = sheet.parentUri?.let { DocumentFile.fromTreeUri(context, it) } ?: return null
        val parts = track.split('/').filter { it.isNotEmpty() }
        for (part in parts.dropLast(1)) dir = childNamed(dir, part)?.takeIf { it.isDirectory } ?: return null
        val trackFile = childNamed(dir, parts.lastOrNull() ?: return null)?.takeIf { it.isFile } ?: return null
        return trackFile.uri to trackFile.length()
    }

    // Sheets don't always match the files' case ("GAME.BIN" beside "Game.bin").
    private fun childNamed(dir: DocumentFile, name: String): DocumentFile? =
        dir.findFile(name) ?: dir.listFiles().firstOrNull { it.name.equals(name, ignoreCase = true) }

    /**
     * Game copies in mia_temp that were never unloaded (the app closed mid-game, or builds that kept every copy).
     * Only top-level files with a game's extension go, and not neogeo.zip, already copied for a Neo Geo game by
     * then; the driver's redirected files sit in folders and the firmware copies are named fw_*.
     */
    private fun removeStaleGameCopies(tempDir: File) {
        val extensions = PhobosCore.enumerateSystems().flatMap { PhobosCore.getSystemExtensions(it) }.toSet() + "zip" + "7z"
        tempDir.listFiles { file -> file.isFile && file.extension.lowercase() in extensions && file.name != "neogeo.zip" && !file.name.startsWith("fw_") }
            ?.forEach { if (it.delete()) Log.i("Phobos", "Removed stale temp copy ${it.name}") }
    }
    
    private fun extractAssets() {
        val root = File(context.filesDir, "ares")
        if (!root.exists()) {
            Log.d("Phobos", "Creating ares root dir: ${root.absolutePath}")
            root.mkdirs()
        }

        // Extract directories
        extractFolder("Database", File(root, "Database"))
        extractFolder("System", File(root, "System"))
        
        // Also tell native where the home is
        PhobosCore.setHomePath(root.absolutePath)

        // Default native saves dir — the configured Saves Path (if any) is
        // resolved and pushed at load time in loadRom(). Left alone while a game
        // a replaced activity loaded is still in the core: native writes its
        // battery saves to the current path when it unloads.
        val savesDir = File(context.filesDir, "saves")
        if (!savesDir.exists()) savesDir.mkdirs()
        if (CoreSession.system.isEmpty()) PhobosCore.setSavesPath(savesDir.absolutePath)
    }

    /**
     * Resolves a SAF content:// tree/document URI (from OpenDocumentTree) to a
     * real filesystem path native code can use, or returns null when it can't.
     *
     * Handles the external-storage provider's docId format:
     *   "primary:ROMS"  -> /storage/emulated/0/ROMS
     *   "1C1F-1234:Dir" -> /storage/1C1F-1234/Dir
     * Non-content URIs (plain paths) pass through unchanged.
     */
    private fun resolveSafPath(uriString: String): String? {
        if (uriString.isEmpty()) return null
        val uri = Uri.parse(uriString)
        if (uri.scheme != "content") return uriString
        return try {
            // A file inside a picked folder carries both ids; its own is the document id.
            val docId = when {
                DocumentsContract.isDocumentUri(context, uri) -> DocumentsContract.getDocumentId(uri)
                DocumentsContract.isTreeUri(uri) -> DocumentsContract.getTreeDocumentId(uri)
                else -> DocumentsContract.getDocumentId(uri)
            }
            val colon = docId.indexOf(':')
            if (colon < 0) return null
            val volumeId = docId.substring(0, colon)
            val relPath = docId.substring(colon + 1)
            val base = if (volumeId == "primary") {
                Environment.getExternalStorageDirectory().absolutePath
            } else {
                "/storage/$volumeId"
            }
            if (relPath.isEmpty()) base else "$base/$relPath"
        } catch (e: Exception) {
            null
        }
    }

    private fun extractFolder(assetPath: String, destDir: File) {
        if (!destDir.exists()) {
            Log.d("Phobos", "Creating directory: ${destDir.absolutePath}")
            destDir.mkdirs()
        }

        val assets = context.assets.list(assetPath) ?: return
        
        assets.forEach { fileName ->
            val subAssetPath = if (assetPath.isEmpty()) fileName else "$assetPath/$fileName"
            val destFile = File(destDir, fileName)
            
            // Optimization: check if it's a file by trying to open it
            // list() is slow, so we only use it if we're sure it's a directory or on the first level
            var isDir = false
            try {
                // Directories can't be opened as assets directly in most cases
                context.assets.open(subAssetPath).close()
            } catch (e: Exception) {
                isDir = true
            }

            if (isDir) {
                extractFolder(subAssetPath, destFile)
            } else {
                if (!destFile.exists()) {
                    Log.d("Phobos", "Extracting asset: $subAssetPath")
                    try {
                        context.assets.open(subAssetPath).use { input ->
                            destFile.outputStream().use { output ->
                                input.copyTo(output)
                            }
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Failed to extract $subAssetPath: ${e.message}")
                    }
                }
            }
        }
    }

    fun clearRoms() {
        _roms.value = emptyList()
    }
}

data class RomFile(
    val name: String,
    val uri: Uri,
    val parentUri: Uri? = null,
    /** A multi-disc game's discs in order, from an .m3u playlist or "(Disc N)" files; [name] is then the game's. */
    val discs: List<RomFile> = emptyList(),
)
