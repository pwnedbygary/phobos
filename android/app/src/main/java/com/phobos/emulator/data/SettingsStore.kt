package com.phobos.emulator.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import com.phobos.emulator.LogLevel
import com.phobos.emulator.ui.touch.AnalogMode
import com.phobos.emulator.ui.touch.DpadMode
import com.phobos.emulator.ui.touch.HapticLevel
import com.phobos.emulator.ui.touch.TouchPrefs
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "settings")

/** Parses a persisted enum name, falling back to [default] for unknown or renamed values. */
private inline fun <reified T : Enum<T>> enumOrDefault(name: String, default: T): T =
    enumValues<T>().firstOrNull { it.name == name } ?: default

enum class ThemeMode {
    LIGHT, DARK, AUTO
}

enum class RegionPreference(val label: String) {
    NTSC_U_NTSC_J_PAL("NTSC-U -> NTSC-J -> PAL"),
    NTSC_U_PAL_NTSC_J("NTSC-U -> PAL -> NTSC-J"),
    NTSC_J_NTSC_U_PAL("NTSC-J -> NTSC-U -> PAL"),
    NTSC_J_PAL_NTSC_U("NTSC-J -> PAL -> NTSC-U"),
    PAL_NTSC_U_NTSC_J("PAL -> NTSC-U -> NTSC-J"),
    PAL_NTSC_J_NTSC_U("PAL -> NTSC-J -> NTSC-U")
}

enum class AspectRatioMode(val label: String) {
    STRETCHED("Stretched"),
    CORE_PROVIDED("Core Provided"),
    INTEGER_SCALED("Integer Scaled")
}

data class EmulatorSettings(
    val themeMode: ThemeMode = ThemeMode.AUTO,
    val regionPreference: RegionPreference = RegionPreference.NTSC_U_NTSC_J_PAL,
    val fastBoot: Boolean = false,
    val muteAudio: Boolean = false,
    val colorEmulation: Boolean = true,
    val interframeBlending: Boolean = true,
    val overscan: Boolean = true,
    val runAhead: Boolean = false,
    val autoSaveState: Boolean = true,
    val autoLoadState: Boolean = false,
    val fullScreenMode: Boolean = false,
    val showTouchControls: Boolean = true,
    val touch: TouchPrefs = TouchPrefs(),
    // "<familyKey>_<land|port>" -> TouchLayoutCodec-encoded per-element overrides.
    val touchLayouts: Map<String, String> = emptyMap(),
    val showPerformanceMonitor: Boolean = false,
    val perfShowFps: Boolean = true,
    val perfShowFrameTime: Boolean = true,
    val perfShowRam: Boolean = true,
    val perfShowCore: Boolean = true,
    val perfShowShaderFails: Boolean = false,
    val perfOverlayScale: Float = 1.0f,
    val perfOverlayPosX: Float = 1.0f,  // 0=left, 1=right
    val perfOverlayPosY: Float = 0.0f,  // 0=top, 1=bottom
    val zxKeyboardOpacity: Float = 1.0f,  // on-screen ZX keyboard alpha (0-1)
    val zxTapeMuted: Boolean = true,     // mute the loud ZX tape-loading screech (default ON — only silences the tape stream, not game audio)
    val logVerbosity: LogLevel = LogLevel.INFO,
    val fastForwardSpeed: Float = 2.0f,
    val n64Upscale: Int = 1,
    val n64Recompiler: Boolean = true,
    val skipBootRom: Boolean = false,
    val customDriverPath: String = "",
    val driverUpdateNotifications: Boolean = true,
    val driverUpdateCheckTime: Long = 0L,
    val ps1AnalogMode: Boolean = true,
    val n64ExpansionPak: Boolean = true,
    val n64DisableVIProcessing: Boolean = false,
    val n64WeaveDeinterlacing: Boolean = false,
    val n64SupersampleScanout: Boolean = false,
    val n64ViOverclock: Int = 100,
    val n64UseDefaultCountPerOp: Boolean = true,
    val n64CountPerOp: Int = 2,
    val n64UseDefaultCpuOverclock: Boolean = true,
    val n64CpuOverclock: Int = 0,
    // Asynchronous RDP (Mupen64Plus "SynchronousRDP" off): faster, less accurate. Off by default.
    val n64AsyncRdp: Boolean = false,
    val n64Pak: String = "None",
    val n64DebugLogging: Boolean = false,
    val orientationVertical: Boolean = false,
    val firmwarePath: String = "",
    val savesPath: String = "",
    val statesPath: String = "",
    val screenshotsPath: String = "",
    val vulkanCachePath: String = "",
    val arcadeRomsPath: String = "",
    val shaderPath: String = "",
    val aspectRatioMode: AspectRatioMode = AspectRatioMode.CORE_PROVIDED,
    // Map<AresBit, StringBinding> where StringBinding is "k:KEYCODE" or "a:AXIS:POS(1/0)"
    val inputMappings: Map<Int, String> = emptyMap(),
    val hiddenSystems: Set<String> = emptySet(),
    val hotkeys: Map<String, List<Int>> = emptyMap(),
    val systemRomPaths: Map<String, Set<String>> = emptyMap(),
    val systemFirmwarePaths: Map<String, String> = emptyMap(),
    // ZX per-core control scheme (0=Kempston,1=QAOP,2=ZXZX,3=ELITE,4=CUSTOM)
    val zxControlScheme: Map<String, Int> = emptyMap(),
    // ZX per-core key rebinds: system -> {keyboardLabel -> gamepadBit}
    val zxKeyBindings: Map<String, Map<String, Int>> = emptyMap(),
    val zxStickToKeys: Map<String, Boolean> = emptyMap(),
    val zxReversePitch: Map<String, Boolean> = emptyMap(),
    val hasDefaultsInitialized: Boolean = false
)

class SettingsStore(private val context: Context) {
    companion object {
        val THEME_MODE = stringPreferencesKey("theme_mode")
        val REGION_PREFERENCE = stringPreferencesKey("region_preference")
        val FAST_BOOT = booleanPreferencesKey("fast_boot")
        val MUTE_AUDIO = booleanPreferencesKey("mute_audio")
        val COLOR_EMULATION = booleanPreferencesKey("color_emulation")
        val INTERFRAME_BLENDING = booleanPreferencesKey("interframe_blending")
        val OVERSCAN = booleanPreferencesKey("overscan")
        val RUN_AHEAD = booleanPreferencesKey("run_ahead")
        val AUTO_SAVE_MEMORY = booleanPreferencesKey("auto_save_memory")
        val AUTO_LOAD_MEMORY = booleanPreferencesKey("auto_load_memory")
        val FULL_SCREEN_MODE = booleanPreferencesKey("full_screen_mode")
        val SHOW_TOUCH_CONTROLS = booleanPreferencesKey("show_touch_controls")
        val TOUCH_OPACITY = floatPreferencesKey("touch_opacity")
        val TOUCH_OPACITY_PORTRAIT = floatPreferencesKey("touch_opacity_portrait")
        val TOUCH_SCALE = floatPreferencesKey("touch_scale")
        val TOUCH_HAPTICS = stringPreferencesKey("touch_haptics")
        val TOUCH_SLIDE = booleanPreferencesKey("touch_slide_between_buttons")
        val TOUCH_AUTO_HOLD = booleanPreferencesKey("touch_auto_hold")
        val TOUCH_SWAP_HANDS = booleanPreferencesKey("touch_swap_hands")
        val TOUCH_IDLE_FADE = booleanPreferencesKey("touch_idle_fade")
        val TOUCH_DPAD_MODE = stringPreferencesKey("touch_dpad_mode")
        val TOUCH_DPAD_DIAGONAL = floatPreferencesKey("touch_dpad_diagonal")
        val TOUCH_ANALOG_MODE = stringPreferencesKey("touch_analog_mode")
        val TOUCH_ANALOG_DEADZONE = floatPreferencesKey("touch_analog_deadzone")
        val TOUCH_ANALOG_SENSITIVITY = floatPreferencesKey("touch_analog_sensitivity")
        val TOUCH_HIDE_ON_CONTROLLER = booleanPreferencesKey("touch_hide_on_controller")
        val TOUCH_SHOW_MENU = booleanPreferencesKey("touch_show_menu_button")
        val TOUCH_SHOW_FAST_FORWARD = booleanPreferencesKey("touch_show_fast_forward_button")
        const val TOUCH_LAYOUT_PREFIX = "touch_layout_"
        val SHOW_PERFORMANCE_MONITOR = booleanPreferencesKey("show_performance_monitor")
        val PERF_SHOW_FPS = booleanPreferencesKey("perf_show_fps")
        val PERF_SHOW_FRAMETIME = booleanPreferencesKey("perf_show_frametime")
        val PERF_SHOW_RAM = booleanPreferencesKey("perf_show_ram")
        val PERF_SHOW_CORE = booleanPreferencesKey("perf_show_core")
        val PERF_SHOW_SHADER_FAILS = booleanPreferencesKey("perf_show_shader_fails")
        val PERF_OVERLAY_SCALE = floatPreferencesKey("perf_overlay_scale")
        val PERF_OVERLAY_POS_X = floatPreferencesKey("perf_overlay_pos_x")
        val PERF_OVERLAY_POS_Y = floatPreferencesKey("perf_overlay_pos_y")
        val ZX_KEYBOARD_OPACITY = floatPreferencesKey("zx_keyboard_opacity")
        val ZX_TAPE_MUTED = booleanPreferencesKey("zx_tape_muted")
        val LOG_VERBOSITY = stringPreferencesKey("log_verbosity")
        val FAST_FORWARD_SPEED = floatPreferencesKey("fast_forward_speed")
        val N64_UPSCALE = intPreferencesKey("n64_upscale")
        val N64_RECOMPILER = booleanPreferencesKey("n64_recompiler")
        val SKIP_BOOT_ROM = booleanPreferencesKey("skip_boot_rom")
        val CUSTOM_DRIVER_PATH = stringPreferencesKey("custom_driver_path")
        val DRIVER_UPDATE_NOTIFICATIONS = booleanPreferencesKey("driver_update_notifications")
        val DRIVER_UPDATE_CHECK_TIME = longPreferencesKey("driver_update_check_time")
        val PS1_ANALOG_MODE = booleanPreferencesKey("ps1_analog_mode")
        val N64_EXPANSION_PAK = booleanPreferencesKey("n64_expansion_pak")
        val N64_DISABLE_VI_PROCESSING = booleanPreferencesKey("n64_disable_vi_processing")
        val N64_WEAVE_DEINTERLACING = booleanPreferencesKey("n64_weave_deinterlacing")
        val N64_SUPERSAMPLE_SCANOUT = booleanPreferencesKey("n64_supersample_scanout")
        val N64_VI_OVERCLOCK = intPreferencesKey("n64_vi_overclock")
        val N64_USE_DEFAULT_COUNT_PER_OP = booleanPreferencesKey("n64_use_default_count_per_op")
        val N64_COUNT_PER_OP = intPreferencesKey("n64_count_per_op")
        val N64_USE_DEFAULT_CPU_OVERCLOCK = booleanPreferencesKey("n64_use_default_cpu_overclock")
        val N64_CPU_OVERCLOCK = intPreferencesKey("n64_cpu_overclock")
        val N64_ASYNC_RDP = booleanPreferencesKey("n64_async_rdp")
        val N64_PAK = stringPreferencesKey("n64_pak")
        val N64_DEBUG_LOGGING = booleanPreferencesKey("n64_debug_logging")
        val ORIENTATION_VERTICAL = booleanPreferencesKey("orientation_vertical")
        val FIRMWARE_PATH = stringPreferencesKey("path_firmware")
        val SAVES_PATH = stringPreferencesKey("path_saves")
        val STATES_PATH = stringPreferencesKey("path_states")
        val SCREENSHOTS_PATH = stringPreferencesKey("path_screenshots")
        val VULKAN_CACHE_PATH = stringPreferencesKey("path_vulkan_cache")
        val ARCADE_ROMS_PATH = stringPreferencesKey("path_arcade_roms")
        val SHADER_PATH = stringPreferencesKey("path_shader")
        val ASPECT_RATIO_MODE = stringPreferencesKey("aspect_ratio_mode")
        val HIDDEN_SYSTEMS = stringSetPreferencesKey("hidden_systems")
        val HAS_DEFAULTS_INITIALIZED = booleanPreferencesKey("has_defaults_initialized")
        val HOTKEYS_PREFIX = "hotkey_combo_"
        // ZX per-core preferences. Encoded as: zx_scheme_<system> = int;
        // zx_bind_<system> = "label=bit,label=bit,..." (labels contain spaces
        // so a single string per system is cleanest); zx_stick_<system> /
        // zx_reverse_<system> = bool.
        val ZX_SCHEME_PREFIX = "zx_scheme_"
        val ZX_BIND_PREFIX = "zx_bind_"
        val ZX_STICK_PREFIX = "zx_stick_"
        val ZX_REVERSE_PREFIX = "zx_reverse_"
    }

    val settings: Flow<EmulatorSettings> = context.dataStore.data.map { preferences ->
        val romPaths = mutableMapOf<String, MutableSet<String>>()
        val fwPaths = mutableMapOf<String, String>()
        val mappings = mutableMapOf<Int, String>()
        val hotkeys = mutableMapOf<String, List<Int>>()
        val zxSchemes = mutableMapOf<String, Int>()
        val zxBinds = mutableMapOf<String, Map<String, Int>>()
        val zxSticks = mutableMapOf<String, Boolean>()
        val zxReverses = mutableMapOf<String, Boolean>()
        val touchLayouts = mutableMapOf<String, String>()

        preferences.asMap().forEach { (key, value) ->
            val name = key.name
            if (name.startsWith(TOUCH_LAYOUT_PREFIX) && value is String) {
                touchLayouts[name.removePrefix(TOUCH_LAYOUT_PREFIX)] = value
            } else if (name.startsWith("rom_path_") && value is String) {
                val system = name.removePrefix("rom_path_")
                romPaths.getOrPut(system) { mutableSetOf() }.add(value)
            } else if (name.startsWith("rom_paths_") && value is Set<*>) {
                val system = name.removePrefix("rom_paths_")
                @Suppress("UNCHECKED_CAST")
                romPaths.getOrPut(system) { mutableSetOf() }.addAll(value as Set<String>)
            } else if (name.startsWith("fw_path_") && value is String) {
                fwPaths[name.removePrefix("fw_path_")] = value
            } else if (name.startsWith("mapping_")) {
                val bit = name.removePrefix("mapping_").toIntOrNull()
                if (bit != null) {
                    mappings[bit] = value.toString()
                }
            } else if (name.startsWith(HOTKEYS_PREFIX) && value is String) {
                val action = name.removePrefix(HOTKEYS_PREFIX)
                val combo = value.split(",").mapNotNull { it.trim().toIntOrNull() }
                // Record the explicit state EVEN when empty: an empty combo means
                // "user unbind this hotkey" and must override the default below.
                hotkeys[action] = combo
            } else if (name.startsWith(ZX_SCHEME_PREFIX) && value is Int) {
                zxSchemes[name.removePrefix(ZX_SCHEME_PREFIX)] = value
            } else if (name.startsWith(ZX_BIND_PREFIX) && value is String) {
                // "label=bit,label=bit,..." — labels contain spaces (e.g. "SPACE BREAK")
                val system = name.removePrefix(ZX_BIND_PREFIX)
                val binds = mutableMapOf<String, Int>()
                value.split(",").forEach { pair ->
                    val eq = pair.indexOf('=')
                    if (eq > 0) {
                        val label = pair.substring(0, eq).trim()
                        val bit = pair.substring(eq + 1).trim().toIntOrNull()
                        if (label.isNotEmpty() && bit != null) binds[label] = bit
                    }
                }
                zxBinds[system] = binds
            } else if (name.startsWith(ZX_STICK_PREFIX) && value is Boolean) {
                zxSticks[name.removePrefix(ZX_STICK_PREFIX)] = value
            } else if (name.startsWith(ZX_REVERSE_PREFIX) && value is Boolean) {
                zxReverses[name.removePrefix(ZX_REVERSE_PREFIX)] = value
            }
        }

        val defaultHotkeys = mapOf<String, List<Int>>(
            // Keycodes: 96=A 97=B 98=C 99=X 100=Y 101=Z 102=L1 103=R1 104=L2
            // 105=R2 106=THUMBL 107=THUMBR 108=START 109=SELECT 21/22=DPAD L/R
            // Defaults mirror the developer's device config (Z-button based).
            "ff_hold" to emptyList(),              // unbound
            "save" to listOf(101, 103),            // Z + R1
            "load" to listOf(101, 102),            // Z + L1
            "inc_slot" to listOf(101, 22),         // Z + DPAD_RIGHT
            "dec_slot" to listOf(101, 21),         // Z + DPAD_LEFT
            "pause" to listOf(101, 96),            // Z + A
            "reset" to listOf(101, 109, 108),      // Z + SELECT + START
            "reload" to listOf(101, 99),           // Z + X
            "quit" to listOf(101, 97),             // Z + B
            "screenshot" to listOf(101, 107),      // Z + THUMBR
            "mute" to listOf(101, 100),            // Z + Y
            "ff_toggle" to listOf(101, 105),       // Z + R2
            "analog_toggle" to listOf(98),         // C
            "keyboard" to listOf(101, 104),        // Z + L2
            "library" to listOf(101, 98)           // Z + C: swap to library (pause) / back to game (resume)
        )
        val finalHotkeys = defaultHotkeys.toMutableMap()
        // Explicit user bindings (or explicit unbind = empty combo) take
        // precedence over defaults. An empty combo REMOVES the default so the
        // hotkey is genuinely unbound.
        hotkeys.forEach { (action, combo) ->
            if (combo.isEmpty()) finalHotkeys.remove(action)
            else finalHotkeys[action] = combo
        }

        val rawMap = preferences.asMap()
        
        fun <T> safeGet(key: Preferences.Key<T>, default: T): T {
            val value = rawMap[key]
            return if (value != null && value::class.java == default!!::class.java) {
                @Suppress("UNCHECKED_CAST")
                value as T
            } else {
                default
            }
        }

        fun safeGetString(key: Preferences.Key<String>, default: String): String {
            return rawMap[key]?.toString() ?: default
        }

        EmulatorSettings(
            themeMode = enumOrDefault(safeGetString(THEME_MODE, ThemeMode.AUTO.name), ThemeMode.AUTO),
            regionPreference = enumOrDefault(safeGetString(REGION_PREFERENCE, ""), RegionPreference.NTSC_U_NTSC_J_PAL),
            fastBoot = safeGet(FAST_BOOT, false),
            muteAudio = safeGet(MUTE_AUDIO, false),
            colorEmulation = safeGet(COLOR_EMULATION, true),
            interframeBlending = safeGet(INTERFRAME_BLENDING, true),
            overscan = safeGet(OVERSCAN, true),
            runAhead = safeGet(RUN_AHEAD, false),
            // Keys are legacy-named (auto_save_memory) for DataStore persistence
            // compatibility — they toggle save STATES, not cart/flash saves.
            autoSaveState = safeGet(AUTO_SAVE_MEMORY, true),
            autoLoadState = safeGet(AUTO_LOAD_MEMORY, false),
            fullScreenMode = safeGet(FULL_SCREEN_MODE, false),
            showTouchControls = safeGet(SHOW_TOUCH_CONTROLS, true),
            touch = preferences.touchPrefs(),
            touchLayouts = touchLayouts,
            showPerformanceMonitor = safeGet(SHOW_PERFORMANCE_MONITOR, false),
            perfShowFps = safeGet(PERF_SHOW_FPS, true),
            perfShowFrameTime = safeGet(PERF_SHOW_FRAMETIME, true),
            perfShowRam = safeGet(PERF_SHOW_RAM, true),
            perfShowCore = safeGet(PERF_SHOW_CORE, true),
            perfShowShaderFails = safeGet(PERF_SHOW_SHADER_FAILS, false),
            perfOverlayScale = safeGet(PERF_OVERLAY_SCALE, 1.0f),
            perfOverlayPosX = safeGet(PERF_OVERLAY_POS_X, 1.0f),
            perfOverlayPosY = safeGet(PERF_OVERLAY_POS_Y, 0.0f),
            zxKeyboardOpacity = safeGet(ZX_KEYBOARD_OPACITY, 1.0f),
            zxTapeMuted = safeGet(ZX_TAPE_MUTED, true),  // default ON — only silences the tape stream, not game audio
            logVerbosity = enumOrDefault(safeGetString(LOG_VERBOSITY, ""), LogLevel.INFO),
            fastForwardSpeed = safeGet(FAST_FORWARD_SPEED, 2.0f),
            n64Upscale = safeGet(N64_UPSCALE, 1),
            n64Recompiler = safeGet(N64_RECOMPILER, true),
            skipBootRom = safeGet(SKIP_BOOT_ROM, false),
            customDriverPath = safeGetString(CUSTOM_DRIVER_PATH, ""),
            driverUpdateNotifications = safeGet(DRIVER_UPDATE_NOTIFICATIONS, true),
            driverUpdateCheckTime = safeGet(DRIVER_UPDATE_CHECK_TIME, 0L),
            ps1AnalogMode = safeGet(PS1_ANALOG_MODE, true),
            n64ExpansionPak = safeGet(N64_EXPANSION_PAK, true),
            n64DisableVIProcessing = safeGet(N64_DISABLE_VI_PROCESSING, false),
            n64WeaveDeinterlacing = safeGet(N64_WEAVE_DEINTERLACING, false),
            n64SupersampleScanout = safeGet(N64_SUPERSAMPLE_SCANOUT, false),
            n64ViOverclock = safeGet(N64_VI_OVERCLOCK, 100),
            n64UseDefaultCountPerOp = safeGet(N64_USE_DEFAULT_COUNT_PER_OP, true),
            n64CountPerOp = safeGet(N64_COUNT_PER_OP, 2),
            n64UseDefaultCpuOverclock = safeGet(N64_USE_DEFAULT_CPU_OVERCLOCK, true),
            n64CpuOverclock = safeGet(N64_CPU_OVERCLOCK, 0),
            n64AsyncRdp = safeGet(N64_ASYNC_RDP, false),
            n64Pak = safeGetString(N64_PAK, "None"),
            n64DebugLogging = safeGet(N64_DEBUG_LOGGING, false),
            orientationVertical = safeGet(ORIENTATION_VERTICAL, false),
            firmwarePath = safeGetString(FIRMWARE_PATH, ""),
            savesPath = safeGetString(SAVES_PATH, ""),
            statesPath = safeGetString(STATES_PATH, ""),
            screenshotsPath = safeGetString(SCREENSHOTS_PATH, ""),
            vulkanCachePath = safeGetString(VULKAN_CACHE_PATH, ""),
            arcadeRomsPath = safeGetString(ARCADE_ROMS_PATH, ""),
            shaderPath = safeGetString(SHADER_PATH, ""),
            aspectRatioMode = enumOrDefault(safeGetString(ASPECT_RATIO_MODE, ""), AspectRatioMode.CORE_PROVIDED),
            hiddenSystems = preferences[HIDDEN_SYSTEMS] ?: emptySet(),
            hotkeys = finalHotkeys,
            inputMappings = mappings,
            systemRomPaths = romPaths,
            systemFirmwarePaths = fwPaths,
            zxControlScheme = zxSchemes,
            zxKeyBindings = zxBinds,
            zxStickToKeys = zxSticks,
            zxReversePitch = zxReverses,
            hasDefaultsInitialized = safeGet(HAS_DEFAULTS_INITIALIZED, false)
        )
    }

    suspend fun initializeDefaults() {
        context.dataStore.edit { preferences ->
            if (preferences[HAS_DEFAULTS_INITIALIZED] == true) return@edit
            val defaults = mapOf(
                1 shl 0  to "k:19",  1 shl 1  to "k:20",  1 shl 2  to "k:21",  1 shl 3  to "k:22",
                1 shl 4  to "k:96",  1 shl 5  to "k:97",  1 shl 6  to "k:99",  1 shl 7  to "k:100",
                1 shl 8  to "k:102", 1 shl 9  to "k:103", 1 shl 10 to "k:104", 1 shl 11 to "k:105",
                1 shl 12 to "k:106", 1 shl 13 to "k:107", 1 shl 14 to "k:109", 1 shl 15 to "k:108",
                1 shl 17 to "a:1:0",  1 shl 18 to "a:1:1",  1 shl 19 to "a:0:0",  1 shl 20 to "a:0:1",
                1 shl 21 to "a:14:0", 1 shl 22 to "a:14:1", 1 shl 23 to "a:11:0", 1 shl 24 to "a:11:1"
            )
            defaults.forEach { (bit, binding) -> preferences[stringPreferencesKey("mapping_$bit")] = binding }
            preferences[HAS_DEFAULTS_INITIALIZED] = true
        }
    }

    suspend fun updateInputMapping(aresBit: Int, newBinding: String) {
        context.dataStore.edit { preferences ->
            val otherBitKey = preferences.asMap().keys.find {
                it.name.startsWith("mapping_") && preferences[it as Preferences.Key<String>] == newBinding
            }
            val bitKey = stringPreferencesKey("mapping_$aresBit")
            val oldBinding = preferences[bitKey]
            if (otherBitKey != null && oldBinding != null) {
                preferences[otherBitKey as Preferences.Key<String>] = oldBinding
                preferences[bitKey] = newBinding
            } else {
                if (otherBitKey != null) preferences.remove(otherBitKey)
                preferences[bitKey] = newBinding
            }
        }
    }

    suspend fun clearInputMapping(aresBit: Int) = context.dataStore.edit { it.remove(stringPreferencesKey("mapping_$aresBit")) }
    suspend fun clearAllMappings() = context.dataStore.edit { p ->
        p.asMap().keys.filter { it.name.startsWith("mapping_") }.forEach { p.remove(it) }
        p[HAS_DEFAULTS_INITIALIZED] = true
    }

    suspend fun setThemeMode(mode: ThemeMode) = context.dataStore.edit { it[THEME_MODE] = mode.name }
    suspend fun setRegionPreference(pref: RegionPreference) = context.dataStore.edit { it[REGION_PREFERENCE] = pref.name }
    suspend fun setFastBoot(enabled: Boolean) = context.dataStore.edit { it[FAST_BOOT] = enabled }
    suspend fun setMuteAudio(enabled: Boolean) = context.dataStore.edit { it[MUTE_AUDIO] = enabled }
    suspend fun setColorEmulation(enabled: Boolean) = context.dataStore.edit { it[COLOR_EMULATION] = enabled }
    suspend fun setInterframeBlending(enabled: Boolean) = context.dataStore.edit { it[INTERFRAME_BLENDING] = enabled }
    suspend fun setOverscan(enabled: Boolean) = context.dataStore.edit { it[OVERSCAN] = enabled }
    suspend fun setRunAhead(enabled: Boolean) = context.dataStore.edit { it[RUN_AHEAD] = enabled }
    suspend fun setAutoSaveState(enabled: Boolean) = context.dataStore.edit { it[AUTO_SAVE_MEMORY] = enabled }
    suspend fun setAutoLoadState(enabled: Boolean) = context.dataStore.edit { it[AUTO_LOAD_MEMORY] = enabled }
    suspend fun setFullScreenMode(enabled: Boolean) = context.dataStore.edit { it[FULL_SCREEN_MODE] = enabled }
    suspend fun setShowTouchControls(enabled: Boolean) = context.dataStore.edit { it[SHOW_TOUCH_CONTROLS] = enabled }
    /** Read-modify-write in one transaction, so quick successive edits are not lost. */
    suspend fun updateTouchPrefs(transform: (TouchPrefs) -> TouchPrefs) = context.dataStore.edit {
        val prefs = transform(it.touchPrefs())
        it[TOUCH_OPACITY] = prefs.opacity.coerceIn(0.1f, 1f)
        it[TOUCH_OPACITY_PORTRAIT] = prefs.opacityPortrait.coerceIn(0.1f, 1f)
        it[TOUCH_SCALE] = prefs.scale.coerceIn(0.5f, 1.8f)
        it[TOUCH_HAPTICS] = prefs.haptics.name
        it[TOUCH_SLIDE] = prefs.slideBetweenButtons
        it[TOUCH_AUTO_HOLD] = prefs.autoHold
        it[TOUCH_SWAP_HANDS] = prefs.swapHands
        it[TOUCH_IDLE_FADE] = prefs.idleFade
        it[TOUCH_DPAD_MODE] = prefs.dpadMode.name
        it[TOUCH_DPAD_DIAGONAL] = prefs.dpadDiagonal.coerceIn(0f, 1f)
        it[TOUCH_ANALOG_MODE] = prefs.analogMode.name
        it[TOUCH_ANALOG_DEADZONE] = prefs.analogDeadzone.coerceIn(0f, 0.5f)
        it[TOUCH_ANALOG_SENSITIVITY] = prefs.analogSensitivity.coerceIn(0.5f, 2f)
        it[TOUCH_HIDE_ON_CONTROLLER] = prefs.hideOnController
        it[TOUCH_SHOW_MENU] = prefs.showMenuButton
        it[TOUCH_SHOW_FAST_FORWARD] = prefs.showFastForwardButton
    }

    private fun Preferences.touchPrefs(): TouchPrefs {
        val raw = asMap()
        // Same type-guarded read as the settings flow: a value of the wrong type falls back.
        fun <T> get(key: Preferences.Key<T>, default: T): T {
            val value = raw[key]
            @Suppress("UNCHECKED_CAST")
            return if (value != null && value::class.java == default!!::class.java) value as T else default
        }
        fun getString(key: Preferences.Key<String>, default: String): String = raw[key]?.toString() ?: default
        val d = TouchPrefs()
        return TouchPrefs(
            opacity = get(TOUCH_OPACITY, d.opacity),
            opacityPortrait = get(TOUCH_OPACITY_PORTRAIT, d.opacityPortrait),
            scale = get(TOUCH_SCALE, d.scale),
            haptics = enumOrDefault(getString(TOUCH_HAPTICS, d.haptics.name), d.haptics),
            slideBetweenButtons = get(TOUCH_SLIDE, d.slideBetweenButtons),
            autoHold = get(TOUCH_AUTO_HOLD, d.autoHold),
            swapHands = get(TOUCH_SWAP_HANDS, d.swapHands),
            idleFade = get(TOUCH_IDLE_FADE, d.idleFade),
            dpadMode = enumOrDefault(getString(TOUCH_DPAD_MODE, d.dpadMode.name), d.dpadMode),
            dpadDiagonal = get(TOUCH_DPAD_DIAGONAL, d.dpadDiagonal),
            analogMode = enumOrDefault(getString(TOUCH_ANALOG_MODE, d.analogMode.name), d.analogMode),
            analogDeadzone = get(TOUCH_ANALOG_DEADZONE, d.analogDeadzone),
            analogSensitivity = get(TOUCH_ANALOG_SENSITIVITY, d.analogSensitivity),
            hideOnController = get(TOUCH_HIDE_ON_CONTROLLER, d.hideOnController),
            showMenuButton = get(TOUCH_SHOW_MENU, d.showMenuButton),
            showFastForwardButton = get(TOUCH_SHOW_FAST_FORWARD, d.showFastForwardButton),
        )
    }
    /** Saves one family/orientation layout; an empty [encoded] string restores the defaults. */
    suspend fun setTouchLayout(layoutKey: String, encoded: String) = context.dataStore.edit {
        val key = stringPreferencesKey(TOUCH_LAYOUT_PREFIX + layoutKey)
        if (encoded.isEmpty()) it.remove(key) else it[key] = encoded
    }
    suspend fun setShowPerformanceMonitor(enabled: Boolean) = context.dataStore.edit { it[SHOW_PERFORMANCE_MONITOR] = enabled }
    suspend fun setPerfShowFps(enabled: Boolean) = context.dataStore.edit { it[PERF_SHOW_FPS] = enabled }
    suspend fun setPerfShowFrameTime(enabled: Boolean) = context.dataStore.edit { it[PERF_SHOW_FRAMETIME] = enabled }
    suspend fun setPerfShowRam(enabled: Boolean) = context.dataStore.edit { it[PERF_SHOW_RAM] = enabled }
    suspend fun setPerfShowCore(enabled: Boolean) = context.dataStore.edit { it[PERF_SHOW_CORE] = enabled }
    suspend fun setPerfShowShaderFails(enabled: Boolean) = context.dataStore.edit { it[PERF_SHOW_SHADER_FAILS] = enabled }
    suspend fun setPerfOverlayScale(scale: Float) = context.dataStore.edit { it[PERF_OVERLAY_SCALE] = scale }
    suspend fun setPerfOverlayPosX(x: Float) = context.dataStore.edit { it[PERF_OVERLAY_POS_X] = x }
    suspend fun setPerfOverlayPosY(y: Float) = context.dataStore.edit { it[PERF_OVERLAY_POS_Y] = y }
    suspend fun setZxKeyboardOpacity(opacity: Float) = context.dataStore.edit { it[ZX_KEYBOARD_OPACITY] = opacity.coerceIn(0.2f, 1.0f) }
    suspend fun setZxTapeMuted(muted: Boolean) = context.dataStore.edit { it[ZX_TAPE_MUTED] = muted }
    suspend fun setLogVerbosity(level: LogLevel) = context.dataStore.edit { it[LOG_VERBOSITY] = level.name }
    suspend fun setFastForwardSpeed(speed: Float) = context.dataStore.edit { it[FAST_FORWARD_SPEED] = speed }
    suspend fun setN64Upscale(factor: Int) = context.dataStore.edit { it[N64_UPSCALE] = factor }
    suspend fun setN64Recompiler(enabled: Boolean) = context.dataStore.edit { it[N64_RECOMPILER] = enabled }
    suspend fun setSkipBootRom(enabled: Boolean) = context.dataStore.edit { it[SKIP_BOOT_ROM] = enabled }
    suspend fun setCustomDriverPath(path: String) = context.dataStore.edit { it[CUSTOM_DRIVER_PATH] = path }
    suspend fun setDriverUpdateNotifications(enabled: Boolean) = context.dataStore.edit { it[DRIVER_UPDATE_NOTIFICATIONS] = enabled }
    suspend fun setDriverUpdateCheckTime(time: Long) = context.dataStore.edit { it[DRIVER_UPDATE_CHECK_TIME] = time }
    suspend fun setPs1AnalogMode(enabled: Boolean) = context.dataStore.edit { it[PS1_ANALOG_MODE] = enabled }
    suspend fun setOrientationMode(vertical: Boolean) = context.dataStore.edit { it[ORIENTATION_VERTICAL] = vertical }
    suspend fun setN64ExpansionPak(enabled: Boolean) = context.dataStore.edit { it[N64_EXPANSION_PAK] = enabled }
    suspend fun setN64DisableVIProcessing(enabled: Boolean) = context.dataStore.edit { it[N64_DISABLE_VI_PROCESSING] = enabled }
    suspend fun setN64WeaveDeinterlacing(enabled: Boolean) = context.dataStore.edit { it[N64_WEAVE_DEINTERLACING] = enabled }
    suspend fun setN64SupersampleScanout(enabled: Boolean) = context.dataStore.edit { it[N64_SUPERSAMPLE_SCANOUT] = enabled }
    suspend fun setN64ViOverclock(percent: Int) = context.dataStore.edit { it[N64_VI_OVERCLOCK] = percent }
    suspend fun setN64UseDefaultCountPerOp(enabled: Boolean) = context.dataStore.edit { it[N64_USE_DEFAULT_COUNT_PER_OP] = enabled }
    suspend fun setN64CountPerOp(value: Int) = context.dataStore.edit { it[N64_COUNT_PER_OP] = value }
    suspend fun setN64UseDefaultCpuOverclock(enabled: Boolean) = context.dataStore.edit { it[N64_USE_DEFAULT_CPU_OVERCLOCK] = enabled }
    suspend fun setN64CpuOverclock(factor: Int) = context.dataStore.edit { it[N64_CPU_OVERCLOCK] = factor }
    suspend fun setN64AsyncRdp(enabled: Boolean) = context.dataStore.edit { it[N64_ASYNC_RDP] = enabled }
    suspend fun setN64Pak(pak: String) = context.dataStore.edit { it[N64_PAK] = pak }
    suspend fun setN64DebugLogging(enabled: Boolean) = context.dataStore.edit { it[N64_DEBUG_LOGGING] = enabled }
    suspend fun setGlobalPath(key: Preferences.Key<String>, path: String) = context.dataStore.edit { it[key] = path }
    suspend fun setVulkanCachePath(path: String) = context.dataStore.edit { it[VULKAN_CACHE_PATH] = path }
    suspend fun setShaderPath(path: String) = context.dataStore.edit { it[SHADER_PATH] = path }
    suspend fun setAspectRatioMode(mode: AspectRatioMode) = context.dataStore.edit { it[ASPECT_RATIO_MODE] = mode.name }

    suspend fun addSystemRomPath(system: String, path: String) = context.dataStore.edit { p ->
        val key = stringSetPreferencesKey("rom_paths_$system")
        p[key] = (p[key] ?: emptySet()) + path
    }
    suspend fun removeSystemRomPath(system: String, path: String) = context.dataStore.edit { p ->
        val key = stringSetPreferencesKey("rom_paths_$system")
        val next = (p[key] ?: emptySet()) - path
        if (next.isEmpty()) p.remove(key) else p[key] = next
    }
    suspend fun setSystemRomPath(system: String, path: String) = addSystemRomPath(system, path)
    suspend fun setSystemFirmwarePath(system: String, path: String) = context.dataStore.edit { it[stringPreferencesKey("fw_path_$system")] = path }
    suspend fun setSystemVisibility(system: String, visible: Boolean) = context.dataStore.edit { p ->
        val current = p[HIDDEN_SYSTEMS] ?: emptySet()
        p[HIDDEN_SYSTEMS] = if (visible) current - system else current + system
    }
    suspend fun setZxControlScheme(system: String, scheme: Int) = context.dataStore.edit { it[intPreferencesKey(ZX_SCHEME_PREFIX + system)] = scheme }
    suspend fun setZxStickToKeys(system: String, enabled: Boolean) = context.dataStore.edit { it[booleanPreferencesKey(ZX_STICK_PREFIX + system)] = enabled }
    suspend fun setZxReversePitch(system: String, enabled: Boolean) = context.dataStore.edit { it[booleanPreferencesKey(ZX_REVERSE_PREFIX + system)] = enabled }
    suspend fun setZxKeyBinding(system: String, label: String, bit: Int) = context.dataStore.edit { p ->
        // Merge into the per-system "label=bit,label=bit" string.
        val key = stringPreferencesKey(ZX_BIND_PREFIX + system)
        val current = p[key] ?: ""
        val binds = mutableMapOf<String, Int>()
        current.split(",").forEach { pair ->
            val eq = pair.indexOf('=')
            if (eq > 0) {
                val l = pair.substring(0, eq).trim()
                val b = pair.substring(eq + 1).trim().toIntOrNull()
                if (l.isNotEmpty() && b != null) binds[l] = b
            }
        }
        if (bit == 0) binds.remove(label) else binds[label] = bit
        p[key] = binds.entries.joinToString(",") { (l, b) -> "$l=$b" }
    }
    suspend fun setHotkey(action: String, combo: List<Int>) = context.dataStore.edit { it[stringPreferencesKey(HOTKEYS_PREFIX + action)] = combo.joinToString(",") }
    suspend fun resetDefaultMapping() {
        context.dataStore.edit { p ->
            p.asMap().keys.filter { it.name.startsWith("mapping_") }.forEach { p.remove(it) }
            p[HAS_DEFAULTS_INITIALIZED] = false
        }
        initializeDefaults()
    }
}
