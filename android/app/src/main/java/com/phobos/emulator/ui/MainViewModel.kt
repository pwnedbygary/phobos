package com.phobos.emulator.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
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
    fun setN64Renderer(mode: Int) = viewModelScope.launch {
        settingsStore.setN64Renderer(mode)
        PhobosCore.setN64Renderer(mode)
    }
    fun setCustomDriverPath(path: String) = viewModelScope.launch {
        settingsStore.setCustomDriverPath(path)
        PhobosCore.setCustomDriverPath(path)
    }
    fun setPs1AnalogMode(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setPs1AnalogMode(enabled)
        PhobosCore.setPs1AnalogMode(enabled)
    }
    fun setOrientationMode(vertical: Boolean) = viewModelScope.launch {
        settingsStore.setOrientationMode(vertical)
        PhobosCore.setOrientationMode(vertical)
    }
    fun setN64ExpansionPak(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setN64ExpansionPak(enabled)
        PhobosCore.setN64ExpansionPak(enabled)
    }
    fun setFastForwardSpeed(speed: Float) = viewModelScope.launch {
        settingsStore.setFastForwardSpeed(speed)
        PhobosCore.setFastForwardSpeed(speed)
    }
    fun setFullScreenMode(enabled: Boolean) = viewModelScope.launch { settingsStore.setFullScreenMode(enabled) }
    fun setShowTouchControls(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowTouchControls(enabled) }
    fun setShowPerformanceMonitor(enabled: Boolean) = viewModelScope.launch { settingsStore.setShowPerformanceMonitor(enabled) }
    fun setLogVerbosity(level: LogLevel) = viewModelScope.launch {
        settingsStore.setLogVerbosity(level)
        PhobosCore.setLogLevel(level.ordinal)
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

    fun setShaderPath(path: String) = viewModelScope.launch {
        settingsStore.setShaderPath(path)
        PhobosCore.setShader(path)
    }

    fun setAspectRatioMode(mode: AspectRatioMode) = viewModelScope.launch {
        settingsStore.setAspectRatioMode(mode)
    }

    fun unloadSystem() {
        PhobosCore.unloadSystem()
        _isLoaded.value = false
        _isPaused.value = false
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
                // 2. Copy from local path to the user's selected SAF path
                try {
                    val baseUriString = settings.value.statesPath
                    if (baseUriString.isNotEmpty()) {
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
                    }
                } catch (e: Exception) {
                    Log.e("Phobos", "Failed to sync state to SAF: ${e.message}")
                    withContext(Dispatchers.Main) {
                        Toast.makeText(context, "Save Failed!", Toast.LENGTH_SHORT).show()
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
                val baseUriString = settings.value.statesPath
                if (baseUriString.isNotEmpty()) {
                    val baseUri = Uri.parse(baseUriString)
                    val rootDir = DocumentFile.fromTreeUri(context, baseUri)
                    val systemDir = rootDir?.findFile(sanitizedName)
                    val stateFile = systemDir?.findFile(fileName)

                    if (stateFile != null && stateFile.exists()) {
                        val copyStart = System.currentTimeMillis()
                        context.contentResolver.openInputStream(stateFile.uri)?.use { input ->
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
                        
                        val nativeStart = System.currentTimeMillis()
                        val nativeSuccess = PhobosCore.loadState(tempFile.absolutePath)
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
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error during loadState: ${e.message}")
            }
        }
    }

    private val _systems = MutableStateFlow<List<String>>(emptyList())
    val systems: List<String> get() = _systems.value

    val visibleSystems: StateFlow<List<String>> = combine(settings, _systems) { s, sys ->
        sys.filter { it !in s.hiddenSystems }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    private val _roms = MutableStateFlow<List<RomFile>>(emptyList())
    val roms: StateFlow<List<RomFile>> = _roms

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    val logs: StateFlow<List<LogEntry>> = _logs

    private val _isPaused = MutableStateFlow(false)
    val isPaused: StateFlow<Boolean> = _isPaused

    private val _isLoaded = MutableStateFlow(false)
    val isLoaded: StateFlow<Boolean> = _isLoaded

    private val _perfStats = MutableStateFlow(PerformanceStats(0.0, 0.0, 0))
    val perfStats: StateFlow<PerformanceStats> = _perfStats

    private val _currentSlot = MutableStateFlow(0)
    val currentSlot: StateFlow<Int> = _currentSlot

    fun incrementSlot() {
        _currentSlot.value = (_currentSlot.value + 1) % 10
    }

    fun decrementSlot() {
        _currentSlot.value = if (_currentSlot.value == 0) 9 else _currentSlot.value - 1
    }

    fun resetSystem() {
        viewModelScope.launch(Dispatchers.IO) {
            PhobosCore.resetSystem()
        }
    }

    fun setN64Recompiler(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setN64Recompiler(enabled)
        PhobosCore.setN64Recompiler(enabled)
    }

    fun setSkipBootRom(enabled: Boolean) = viewModelScope.launch {
        settingsStore.setSkipBootRom(enabled)
        PhobosCore.setSkipBootRom(enabled)
    }

    fun takeScreenshot(systemName: String, romName: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val baseDir = if (settings.value.screenshotsPath.isNotEmpty()) {
                Uri.parse(settings.value.screenshotsPath).path ?: context.filesDir.absolutePath
            } else {
                context.filesDir.absolutePath
            }
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
                    }
                    
                    delay(500)
                }
            }
        }
    }

    fun clearLogs() {
        _logs.value = emptyList()
    }

    fun installCustomDriver(context: Context, uri: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val driverDir = File(context.filesDir, "gpu_drivers")
                if (!driverDir.exists()) driverDir.mkdirs()

                context.contentResolver.openInputStream(uri)?.use { input ->
                    ZipInputStream(input).use { zip ->
                        var entry = zip.nextEntry
                        while (entry != null) {
                            if (!entry.isDirectory && entry.name.endsWith(".so")) {
                                // Extract the .so file
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
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Failed to install custom driver: ${e.message}")
            }
        }
    }

    private val biosMap = mapOf(
        "scph5501.bin" to "fw_psx_us",
        "scph5500.bin" to "fw_psx_jp",
        "scph5502.bin" to "fw_psx_eu",
        "scph101.bin" to "fw_psx_us_v45",
        "st-v.bin" to "fw_saturn_st_v",
        "saturn_bios.bin" to "fw_saturn_us",
        "mpr-17933.bin" to "fw_saturn_jp",
        "mpr-18811.bin" to "fw_saturn_eu",
        "neogeo.zip" to "fw_ng_bios",
        "aes.zip" to "fw_ng_aes",
        "neocdz.zip" to "fw_ng_cd",
        "ngp.zip" to "fw_ngp",
        "ngpc.zip" to "fw_ngpc",
        "64dd_ipl.bin" to "fw_n64dd_jp",
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
        "syscard3us.pce" to "fw_pce_cd_3_us",
        "gexpress.pce" to "fw_pce_cd_ge_jp",
        "disksys.rom" to "fw_fds",
        "coleco.rom" to "fw_cv",
        "gba_bios.bin" to "fw_gba",
        "dmg_boot.bin" to "fw_gb_boot",
        "cgb_boot.bin" to "fw_gbc_boot",
        "sgb_boot.bin" to "fw_sgb_boot"
    )

    private val biosCrcMap = mapOf(
        "1105ca35" to "fw_psx_us", // SCPH-5501
        "ff3d245b" to "fw_psx_jp", // SCPH-5500
        "3273398d" to "fw_psx_eu", // SCPH-5502
        "74360e22" to "fw_n64dd_jp", // N64DD IPL (JP)
        "af5828fd" to "fw_saturn_us", // Saturn v1.01 US
        "1cd513f5" to "fw_saturn_jp", // Saturn v1.01 JP
        "5ec82be9" to "fw_n64_pif_sm5", // N64 PIF SM5
        "4353387a" to "fw_n64_pif_ntsc", // N64 PIF NTSC
        "59b859e7" to "fw_n64_pif_pal",   // N64 PIF PAL
        "32fce34a" to "fw_gb_boot",      // DMG Boot ROM
        "ebf565e8" to "fw_gbc_boot",     // CGB Boot ROM
        "e03ee2d7" to "fw_sgb_boot"      // SGB Boot ROM
    )

    fun scanFirmware(context: Context) {
        val firmwareUriString = settings.value.firmwarePath
        if (firmwareUriString.isEmpty()) return

        viewModelScope.launch(Dispatchers.IO) {
            try {
                val rootUri = Uri.parse(firmwareUriString)
                val rootDir = DocumentFile.fromTreeUri(context, rootUri) ?: return@launch
                
                rootDir.listFiles().forEach { file ->
                    val name = file.name?.lowercase() ?: ""
                    
                    // 1. Check by filename
                    val keyByName = biosMap[name]
                    if (keyByName != null) {
                        settingsStore.setSystemFirmwarePath(keyByName, file.uri.toString())
                        return@forEach
                    }

                    // 2. Check by CRC32
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
                                settingsStore.setSystemFirmwarePath(keyByCrc, file.uri.toString())
                            }
                        }
                    } catch (e: Exception) {
                        Log.e("Phobos", "Error calculating CRC for ${file.name}: ${e.message}")
                    }
                }
                Log.d("Phobos", "Firmware scan complete")
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
            Log.d("Phobos", "loadRom starting: system='$systemName', name='${rom.name}'")

            // Sync current settings to native before loading
            val currentSettings = settings.value
            
            // For Arcade/Neo Geo, try to copy neogeo.zip from ROM folder to mia_temp
            if (systemName == "Arcade" || systemName.contains("Neo Geo")) {
                rom.parentUri?.let { pUri ->
                    val parentDir = DocumentFile.fromTreeUri(context, pUri)
                    parentDir?.findFile("neogeo.zip")?.let { biosFile ->
                        try {
                            val miaTempPath = File(context.cacheDir, "mia_temp")
                            if (!miaTempPath.exists()) miaTempPath.mkdirs()
                            val destFile = File(miaTempPath, "neogeo.zip")
                            context.contentResolver.openInputStream(biosFile.uri)?.use { input ->
                                destFile.outputStream().use { output ->
                                    input.copyTo(output)
                                }
                            }
                            Log.i("Phobos", "Auto-copied neogeo.zip for Arcade core")
                        } catch (e: Exception) {
                            Log.e("Phobos", "Failed to auto-copy neogeo.zip: ${e.message}")
                        }
                    }
                }
            }
            Log.d("Phobos", "Syncing settings to native: Driver='${currentSettings.customDriverPath}', Recompiler=${currentSettings.n64Recompiler}")
            
            PhobosCore.setFastBoot(currentSettings.fastBoot)
            PhobosCore.setSkipBootRom(currentSettings.skipBootRom)
            PhobosCore.setN64Recompiler(currentSettings.n64Recompiler)
            PhobosCore.setRegion(currentSettings.regionPreference.ordinal)
            PhobosCore.setFastForwardSpeed(currentSettings.fastForwardSpeed)
            PhobosCore.setCustomDriverPath(currentSettings.customDriverPath)
            PhobosCore.setPs1AnalogMode(currentSettings.ps1AnalogMode)
            PhobosCore.setN64ExpansionPak(currentSettings.n64ExpansionPak)
            _isLoaded.value = false
            _isPaused.value = true
            PhobosCore.setPause(true)

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
                            context.contentResolver.openInputStream(uri)?.use { input ->
                                tempFile.outputStream().use { output ->
                                    input.copyTo(output)
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
                    val success = PhobosCore.loadRom(systemName, rom.uri.toString())
                    if (success) {
                        _isLoaded.value = true
                    } else {
                        Log.e("Phobos", "Native loadRom failed for $systemName")
                    }
                }
            } catch (e: Exception) {
                Log.e("Phobos", "Error opening ROM FD: ${e.message}")
            }
        }
    }

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
