package com.phobos.emulator.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
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
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.data.SettingsStore
import com.phobos.emulator.data.ThemeMode
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.zip.CRC32
import java.util.zip.ZipInputStream

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner

class MainViewModel(private val context: Context, private val settingsStore: SettingsStore) : ViewModel(), DefaultLifecycleObserver {
    val settings: StateFlow<EmulatorSettings> = settingsStore.settings
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), EmulatorSettings())

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
    fun setRegionPreference(pref: RegionPreference) = viewModelScope.launch { settingsStore.setRegionPreference(pref) }
    fun setFastBoot(enabled: Boolean) = viewModelScope.launch { settingsStore.setFastBoot(enabled) }
    fun setMuteAudio(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setMuteAudio(enabled)
        PhobosCore.setMuteAudio(enabled)
    }
    fun setColorEmulation(enabled: Boolean) = viewModelScope.launch { settingsStore.setColorEmulation(enabled) }
    fun setInterframeBlending(enabled: Boolean) = viewModelScope.launch { settingsStore.setInterframeBlending(enabled) }
    fun setOverscan(enabled: Boolean) = viewModelScope.launch { settingsStore.setOverscan(enabled) }
    fun setRunAhead(enabled: Boolean) = viewModelScope.launch { settingsStore.setRunAhead(enabled) }
    fun setAutoSaveMemory(enabled: Boolean) = viewModelScope.launch { settingsStore.setAutoSaveMemory(enabled) }
    fun setN64Upscale(factor: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64Upscale(factor)
        PhobosCore.setN64Upscale(factor)
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
    }

    // ZX per-core control scheme + rebinds (Layer 2.5 — the CUSTOM scheme).
    fun setZxControlScheme(system: String, scheme: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setZxControlScheme(system, scheme)
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
    }
    fun setN64WeaveDeinterlacing(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64WeaveDeinterlacing(enabled)
        PhobosCore.setN64WeaveDeinterlacing(enabled)
    }
    fun setN64SupersampleScanout(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64SupersampleScanout(enabled)
        PhobosCore.setN64SupersampleScanout(enabled)
    }
    fun setN64ViOverclock(percent: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64ViOverclock(percent)
        PhobosCore.setN64ViOverclock(percent)
    }
    fun setN64UseDefaultCountPerOp(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64UseDefaultCountPerOp(enabled)
        // When "use default" is on, force the stock value (2) to native.
        PhobosCore.setN64CountPerOp(if (enabled) 2 else settings.value.n64CountPerOp)
    }
    fun setN64CountPerOp(value: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64CountPerOp(value)
        settingsStore.setN64UseDefaultCountPerOp(false)
        PhobosCore.setN64CountPerOp(value)
    }
    fun setN64UseDefaultCpuOverclock(enabled: Boolean) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64UseDefaultCpuOverclock(enabled)
        // When "use default" is on, force the stock value (0) to native.
        PhobosCore.setN64CpuOverclock(if (enabled) 0 else settings.value.n64CpuOverclock)
    }
    fun setN64CpuOverclock(factor: Int) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64CpuOverclock(factor)
        settingsStore.setN64UseDefaultCpuOverclock(false)
        PhobosCore.setN64CpuOverclock(factor)
    }
    fun setN64Pak(pak: String) = viewModelScope.launch(Dispatchers.IO) {
        settingsStore.setN64Pak(pak)
        PhobosCore.setN64Pak(pak)
    }
    fun setFastForwardSpeed(speed: Float) = viewModelScope.launch {
        settingsStore.setFastForwardSpeed(speed)
        PhobosCore.setFastForwardSpeed(speed)
    }
    fun setFullScreenMode(enabled: Boolean) = viewModelScope.launch { settingsStore.setFullScreenMode(enabled) }
    fun setShowTouchControls(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowTouchControls(enabled) }
    fun setShowPerformanceMonitor(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowPerformanceMonitor(enabled) }
    fun setPerfShowFps(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowFps(enabled) }
    fun setPerfShowFrameTime(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowFrameTime(enabled) }
    fun setPerfShowRam(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowRam(enabled) }
    fun setPerfShowCore(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowCore(enabled) }
    fun setPerfShowShaderFails(enabled: Boolean) = viewModelScope.launch { settingsStore.setPerfShowShaderFails(enabled) }
    fun setPerfOverlayScale(scale: Float) = viewModelScope.launch { settingsStore.setPerfOverlayScale(scale) }
    fun setPerfOverlayPosX(x: Float) = viewModelScope.launch { settingsStore.setPerfOverlayPosX(x) }
    fun setPerfOverlayPosY(y: Float) = viewModelScope.launch { settingsStore.setPerfOverlayPosY(y) }
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
    }

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
        viewModelScope.launch(Dispatchers.IO) {
            PhobosCore.setEmulationRunning(false)
            PhobosCore.unloadSystem()
            _isLoaded.value = false
            _isPaused.value = false
            currentSystemName = ""
        }
    }

    fun setSystemVisibility(system: String, visible: Boolean) = viewModelScope.launch {
        settingsStore.setSystemVisibility(system, visible)
    }

    fun resetDefaultMapping() = viewModelScope.launch {
        settingsStore.resetDefaultMapping()
    }

    fun setHotkey(action: String, combo: List<Int>) = viewModelScope.launch {
        settingsStore.setHotkey(action, combo)
    }

    fun clearAllMappings() = viewModelScope.launch {
        settingsStore.clearAllMappings()
    }

    fun clearInputMapping(aresBit: Int) = viewModelScope.launch {
        settingsStore.clearInputMapping(aresBit)
    }

    fun updateInputMapping(aresBit: Int, binding: String) = viewModelScope.launch {
        settingsStore.updateInputMapping(aresBit, binding)
    }

    private fun getSanitizedSystemName(name: String): String {
        return name.replace("/", "_").replace("\\", "_").replace(":", "_").trim()
    }

    fun saveState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            val fileName = "$romName.state$slot"
            val tempFile = File(context.cacheDir, "temp_state")
            val sanitizedName = getSanitizedSystemName(systemName)
            
            // 1. Tell native to save to a local accessible path
            val nativeSuccess = PhobosCore.saveState(tempFile.absolutePath)
            
            if (nativeSuccess) {
                // 2. Copy from local path to the user's selected SAF path.
                //    Task 41: fall back to internal storage when no SAF path is
                //    configured so states aren't silently lost.
                val baseUriString = settings.value.statesPath
                val internalStatesDir = File(context.filesDir, "states/$sanitizedName")
                if (baseUriString.isNotEmpty()) {
                    try {
                        val baseUri = Uri.parse(baseUriString)
                        val rootDir = DocumentFile.fromTreeUri(context, baseUri)
                        val systemDir = rootDir?.findFile(sanitizedName) ?: rootDir?.createDirectory(sanitizedName)
                        
                        val stateFile = systemDir?.findFile(fileName) ?: systemDir?.createFile("application/octet-stream", fileName)
                        
                        if (stateFile != null) {
                            context.contentResolver.openOutputStream(stateFile.uri)?.use { output ->
                                tempFile.inputStream().use { input ->
                                    input.copyTo(output)
                                }
                            }
                            Log.i("Phobos", "Synced state to SAF: ${stateFile.uri}")
                            withContext(Dispatchers.Main) {
                                Toast.makeText(context, "Saved state to Slot $slot", Toast.LENGTH_SHORT).show()
                            }
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Failed to sync state to SAF: ${e.message}")
                        withContext(Dispatchers.Main) {
                            Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
                        }
                    }
                } else {
                    // Internal fallback: filesDir/states/<system>/<file>
                    try {
                        if (!internalStatesDir.exists()) internalStatesDir.mkdirs()
                        tempFile.copyTo(File(internalStatesDir, fileName), overwrite = true)
                        Log.i("Phobos", "Saved state to internal: ${File(internalStatesDir, fileName).absolutePath}")
                        withContext(Dispatchers.Main) {
                            Toast.makeText(context, "Saved state to Slot $slot", Toast.LENGTH_SHORT).show()
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Failed to save state internally: ${e.message}")
                        withContext(Dispatchers.Main) {
                            Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            } else {
                Log.e("Phobos", "Native saveState failed")
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    fun loadState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            val startTime = System.currentTimeMillis()
            val fileName = "$romName.state$slot"
            val tempFile = File(context.cacheDir, "temp_state")
            val sanitizedName = getSanitizedSystemName(systemName)
            Log.d("Phobos", "loadState start: $fileName")

            try {
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
                        val copyStart = System.currentTimeMillis()
                        context.contentResolver.openInputStream(safState.uri)?.use { input ->
                            tempFile.outputStream().use { output ->
                                // Use a larger buffer for faster SAF copying
                                val buffer = ByteArray(64 * 1024)
                                var bytesRead: Int
                                while (input.read(buffer).also { bytesRead = it } != -1) {
                                    output.write(buffer, 0, bytesRead)
                                }
                            }
                        }
                        val copyEnd = System.currentTimeMillis()
                        Log.d("Phobos", "loadState: SAF Copy took ${copyEnd - copyStart}ms")
                        tempFile
                    } else null
                } else {
                    // Internal fallback (if it exists)
                    if (internalStateFile.exists()) internalStateFile else null
                }

                if (stateFile != null) {
                    val nativeStart = System.currentTimeMillis()
                    val nativeSuccess = PhobosCore.loadState(stateFile.absolutePath)
                    val nativeEnd = System.currentTimeMillis()
                    Log.d("Phobos", "loadState: Native Unserialize took ${nativeEnd - nativeStart}ms")

                    if (nativeSuccess) {
                        Log.i("Phobos", "Successfully loaded state from $fileName. Total time: ${System.currentTimeMillis() - startTime}ms")
                        withContext(Dispatchers.Main) {
                            Toast.makeText(context, "Loaded state from Slot $slot", Toast.LENGTH_SHORT).show()
                        }
                    } else {
                        Log.e("Phobos", "Native loadState failed")
                    }
                } else {
                    Log.w("Phobos", "loadState: no state file found for $fileName")
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error during loadState: ${e.message}")
            }
        }
    }

    fun deleteState(systemName: String, romName: String, slot: Int = 0) {
        viewModelScope.launch(Dispatchers.IO) {
            val fileName = "$romName.state$slot"
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
                }
                // Also remove the internal fallback copy if present.
                val internalStateFile = File(context.filesDir, "states/$sanitizedName/$fileName")
                if (internalStateFile.exists()) {
                    deleted = internalStateFile.delete() || deleted
                    Log.i("Phobos", "Deleted state internally: $fileName")
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error during deleteState: ${e.message}")
            }
            if (deleted) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "Deleted state from Slot $slot", Toast.LENGTH_SHORT).show()
                }
            } else {
                withContext(Dispatchers.Main) {
                    Toast.makeText(context, "No state in Slot $slot", Toast.LENGTH_SHORT).show()
                }
            }
        }
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

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    val logs: StateFlow<List<LogEntry>> = _logs

    private val _isPaused = MutableStateFlow(false)
    val isPaused: StateFlow<Boolean> = _isPaused

    private val _isLoaded = MutableStateFlow(false)
    val isLoaded: StateFlow<Boolean> = _isLoaded

    // Unsupported-system popup (set when a broken core's load is refused —
    // ZX Spectrum 128, PC Engine, PC Engine CD, SuperGrafx, Neo Geo).
    // Holds the system NAME to show in the dialog; null = no popup.
    private val _unsupportedSystem = MutableStateFlow<String?>(null)
    val unsupportedSystem: StateFlow<String?> = _unsupportedSystem

    // ZX tape-load progress: -1 = no tape, else (playing?10000:0)+pct*100.
    // Polled ~10 Hz while a ZX game is loaded for the loading progress bar.
    private val _zxTapeProgress = MutableStateFlow(-1)
    val zxTapeProgress: StateFlow<Int> = _zxTapeProgress

    init {
        // Poll tape progress for the ZX loading overlay.
        viewModelScope.launch(Dispatchers.Default) {
            while (true) {
                val loaded = _isLoaded.value
                val sys = currentSystemName
                val next = if (loaded && (sys.contains("ZX Spectrum", ignoreCase = true))) {
                    PhobosCore.getZxTapeProgress()
                } else -1
                if (next != _zxTapeProgress.value) _zxTapeProgress.value = next
                delay(100)
            }
        }
    }

    // Current emulated system name ("Nintendo 64", "PlayStation", ...) — used by
    // the rumble loop and input routing to decide system-specific behavior.
    @Volatile
    private var currentSystemName: String = ""

    /** Name of the currently loaded system, or empty when nothing is loaded. */
    val loadedSystemName: String get() = currentSystemName

    private val _perfStats = MutableStateFlow(PerformanceStats(0.0, 0.0, 0))
    val perfStats: StateFlow<PerformanceStats> = _perfStats

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
                Toast.makeText(context, "Selected Save Slot ${_currentSlot.value}", Toast.LENGTH_SHORT).show()
            }
        }
    }

    fun incrementSlot() {
        _currentSlot.value = (_currentSlot.value + 1) % 10
        showSlotToast()
    }

    fun decrementSlot() {
        _currentSlot.value = if (_currentSlot.value == 0) 9 else _currentSlot.value - 1
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
            PhobosCore.setN64DebugLogging(settings.value.n64DebugLogging)
            PhobosCore.setN64CountPerOp(if (settings.value.n64UseDefaultCountPerOp) 2 else settings.value.n64CountPerOp)
            PhobosCore.setN64CpuOverclock(if (settings.value.n64UseDefaultCpuOverclock) 0 else settings.value.n64CpuOverclock)
            PhobosCore.setNativeLibraryDir(context.applicationInfo.nativeLibraryDir)
            
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
                            // Pulse-burst pattern: 120ms on / 60ms off, repeating.
                            // Re-triggering the motor at its resonant frequency makes
                            // small vibration motors feel stronger than a continuous
                            // 2s burst (which the driver may also dampen).
                            vibrator?.vibrate(VibrationEffect.createWaveform(longArrayOf(0, 120, 60), 0))
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
                        ZipInputStream(input).use { zip ->
                            var entry = zip.nextEntry
                            while (entry != null) {
                                if (!entry.isDirectory && entry.name.endsWith(".so")) {
                                    val outFile = File(driverDir, entry.name.substringAfterLast("/"))
                                    FileOutputStream(outFile).use { output ->
                                        zip.copyTo(output)
                                    }
                                    Log.i("Phobos", "Extracted driver: ${outFile.absolutePath}")
                                    setCustomDriverPath(outFile.absolutePath)
                                    break // Usually only one .so per package
                                }
                                zip.closeEntry()
                                entry = zip.nextEntry
                            }
                        }
                    } else {
                        // Raw .so (or any non-zip): copy it directly as the driver.
                        // Ensure a .so extension (some SAF providers return the
                        // doc ID as the display name — e.g. "msf:1000172790").
                        val soName = if (name.endsWith(".so", ignoreCase = true)) name else "$name.so"
                        val outFile = File(driverDir, soName)
                        FileOutputStream(outFile).use { output ->
                            input.copyTo(output)
                        }
                        Log.i("Phobos", "Installed driver: ${outFile.absolutePath}")
                        setCustomDriverPath(outFile.absolutePath)
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to install custom driver: ${e.message}")
            }
        }
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
                                    val outFile = File(driverDir, entry.name.substringAfterLast("/"))
                                    FileOutputStream(outFile).use { output -> zip.copyTo(output) }
                                    Log.i("Phobos", "Extracted driver: ${outFile.absolutePath}")
                                    setCustomDriverPath(outFile.absolutePath)
                                    break
                                }
                                zip.closeEntry()
                                entry = zip.nextEntry
                            }
                        }
                    } else {
                        val outFile = File(driverDir, name)
                        FileOutputStream(outFile).use { output -> input.copyTo(output) }
                        Log.i("Phobos", "Installed driver: ${outFile.absolutePath}")
                        setCustomDriverPath(outFile.absolutePath)
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to install driver from folder: ${e.message}")
            }
        }
    }

    private val biosMap = mapOf(
        "scph5501.bin" to "fw_psx_us",
        "scph5500.bin" to "fw_psx_jp",
        "scph5502.bin" to "fw_psx_eu",
        "scph101.bin" to "fw_psx_us_v45",
        "neogeo.zip" to "fw_ng_bios",
        "aes.zip" to "fw_ng_aes",
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
        // ZX Spectrum 48K system ROM (required to boot)
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
    }

    fun scanRoms(context: Context, systemName: String, directoryUris: List<Uri>) {
        viewModelScope.launch {
            val extensions = PhobosCore.getSystemExtensions(systemName)
            val foundRoms = withContext(Dispatchers.IO) {
                val result = mutableListOf<RomFile>()
                directoryUris.forEach { uri ->
                    val rootDir = DocumentFile.fromTreeUri(context, uri)
                    if (rootDir != null) {
                        scanRecursive(rootDir, extensions, result)
                    }
                }
                result.distinctBy { it.uri }.sortedBy { it.name }
            }
            _roms.value = foundRoms
        }
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

    fun loadRom(context: Context, systemName: String, rom: RomFile) {
        viewModelScope.launch(Dispatchers.IO) {
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
            if (systemName.contains("Neo Geo")) {
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
                if (copied) Log.i("Phobos", "neogeo.zip ready in mia_temp")
            }
            Log.d("Phobos", "Syncing settings to native: Driver='${currentSettings.customDriverPath}', Recompiler=${currentSettings.n64Recompiler}")
            
            PhobosCore.setFastBoot(currentSettings.fastBoot)
            PhobosCore.setSkipBootRom(currentSettings.skipBootRom)
            PhobosCore.setN64Recompiler(currentSettings.n64Recompiler)
            PhobosCore.setN64Upscale(currentSettings.n64Upscale)
            PhobosCore.setRegion(currentSettings.regionPreference.ordinal)
            PhobosCore.setFastForwardSpeed(currentSettings.fastForwardSpeed)
            PhobosCore.setCustomDriverPath(currentSettings.customDriverPath)
            PhobosCore.setPs1AnalogMode(currentSettings.ps1AnalogMode)
            PhobosCore.setN64ExpansionPak(currentSettings.n64ExpansionPak)
            PhobosCore.setN64DisableVIProcessing(currentSettings.n64DisableVIProcessing)
            PhobosCore.setN64WeaveDeinterlacing(currentSettings.n64WeaveDeinterlacing)
            PhobosCore.setN64SupersampleScanout(currentSettings.n64SupersampleScanout)
            PhobosCore.setN64ViOverclock(currentSettings.n64ViOverclock)
            PhobosCore.setN64CountPerOp(if (currentSettings.n64UseDefaultCountPerOp) 2 else currentSettings.n64CountPerOp)
            PhobosCore.setN64CpuOverclock(if (currentSettings.n64UseDefaultCpuOverclock) 0 else currentSettings.n64CpuOverclock)
            PhobosCore.setN64Pak(currentSettings.n64Pak)

            // ZX per-core control scheme + rebinds + toggles (keyboard cores).
            if (effectiveSystem.contains("ZX Spectrum", ignoreCase = true)) {
                PhobosCore.setZxControlScheme(currentSettings.zxControlScheme[effectiveSystem] ?: 0)
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

            // Vulkan pipeline cache dir (Task 40): user-configured path, else
            // internal default (files/vulkan_cache).
            val cacheDir = resolveSafPath(currentSettings.vulkanCachePath)
                ?: File(context.filesDir, "vulkan_cache").absolutePath
            PhobosCore.setVulkanCachePath(cacheDir)
            Log.i("Phobos", "Vulkan cache path resolved: $cacheDir")
            _isPaused.value = false
            _isLoaded.value = false
            PhobosCore.setPause(false)

            // Set writable temp directory for MIA
            val miaTempPath = File(context.cacheDir, "mia_temp").absolutePath
            
            val tempDir = File(miaTempPath)
            if (!tempDir.exists()) {
                tempDir.mkdirs()
            }
            PhobosCore.setTempFilePath(miaTempPath)

            // Sync Firmware Path and Mappings
            // For each mapped firmware, if it's a URI, copy it to a temp file so native code can read it.
            currentSettings.systemFirmwarePaths.forEach { (key, uriString) ->
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

            // Open the ROM file descriptor
            try {
                context.contentResolver.openFileDescriptor(rom.uri, "r")?.use { pfd ->
                    PhobosCore.setRomFd(pfd.detachFd())
                    val success = PhobosCore.loadRom(effectiveSystem, rom.uri.toString(), rom.name)
                    if (success) {
                        _isLoaded.value = true
                        currentSystemName = effectiveSystem
                    } else {
                        Log.e("Phobos", "Native loadRom failed for $effectiveSystem")
                        // Broken core (ZX 128K / PCE / Neo Geo): native refuses
                        // before spawning any threads. Surface a clear popup
                        // instead of a hang/crash.
                        if (effectiveSystem.contains("ZX Spectrum", ignoreCase = true) ||
                            effectiveSystem.contains("PC Engine", ignoreCase = true) ||
                            effectiveSystem.contains("SuperGrafx", ignoreCase = true) ||
                            effectiveSystem.contains("Neo Geo", ignoreCase = true)) {
                            _unsupportedSystem.value = effectiveSystem
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error opening ROM FD: ${e.message}")
            }
        }
    }

    fun dismissUnsupportedSystem() { _unsupportedSystem.value = null }

    fun loadSecondaryRom(context: Context, systemName: String, rom: RomFile) {
        viewModelScope.launch(Dispatchers.IO) {
            Log.d("Phobos", "loadSecondaryRom starting: system='$systemName', name='${rom.name}'")
            try {
                context.contentResolver.openFileDescriptor(rom.uri, "r")?.use { pfd ->
                    PhobosCore.setSecondaryRomFd(pfd.detachFd())
                    val success = PhobosCore.loadSecondaryRom(systemName, rom.uri.toString())
                    if (!success) {
                        Log.e("Phobos", "Native loadSecondaryRom failed for $systemName")
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error opening Secondary ROM FD: ${e.message}")
            }
        }
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
        // resolved and pushed at load time in loadRom().
        val savesDir = File(context.filesDir, "saves")
        if (!savesDir.exists()) savesDir.mkdirs()
        PhobosCore.setSavesPath(savesDir.absolutePath)
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
            val docId = if (DocumentsContract.isTreeUri(uri)) {
                DocumentsContract.getTreeDocumentId(uri)
            } else {
                DocumentsContract.getDocumentId(uri)
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
    val parentUri: Uri? = null
)
