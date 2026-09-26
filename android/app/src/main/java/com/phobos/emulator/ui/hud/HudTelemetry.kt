package com.phobos.emulator.ui.hud

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.system.Os
import android.system.OsConstants
import java.io.File
import kotlin.math.abs

/**
 * Host metrics for the performance HUD. A null field means this device doesn't expose the
 * value to apps (sysfs nodes are often blocked by SELinux), and the HUD hides it.
 */
data class HostSample(
    /** Emulation thread CPU time as % of one core (100 = a core fully busy). */
    val emuThreadCpuPercent: Float? = null,
    /** Whole-process CPU time as % of one core. */
    val processCpuPercent: Float? = null,
    val emuCoreFreqMhz: Int? = null,
    val cpuTempC: Float? = null,
    val gpuBusyPercent: Int? = null,
    val gpuFreqMhz: Int? = null,
    val gpuTempC: Float? = null,
    val processRssMb: Int? = null,
    val systemUsedMb: Long? = null,
    val systemTotalMb: Long? = null,
    val batteryPercent: Int? = null,
    val batteryCharging: Boolean = false,
    val batteryPowerW: Float? = null,
    val batteryTempC: Float? = null,
    /** PowerManager.THERMAL_STATUS_* (API 29+). */
    val thermalStatus: Int? = null,
    /** PowerManager.getThermalHeadroom(0): 1.0 means the throttling threshold (API 30+). */
    val thermalHeadroom: Float? = null,
)

/** Pure parsers for /proc and sysfs text; separate from the sampler so they are unit-testable. */
internal object HudParsers {
    /** utime + stime clock ticks from a `/proc/<pid>[/task/<tid>]/stat` line. */
    fun statCpuTicks(stat: String): Long? {
        // comm (field 2) is parenthesised and may contain spaces; parse after the last ')'.
        val close = stat.lastIndexOf(')')
        if (close < 0) return null
        val fields = stat.substring(close + 1).trim().split(' ')
        if (fields.size < 13) return null
        val utime = fields[11].toLongOrNull() ?: return null
        val stime = fields[12].toLongOrNull() ?: return null
        return utime + stime
    }

    fun vmRssKb(status: String): Long? =
        status.lineSequence().firstOrNull { it.startsWith("VmRSS:") }
            ?.substringAfter(':')?.trim()?.substringBefore(' ')?.toLongOrNull()

    /** kgsl `gpu_busy_percentage` ("37 %") or `gpubusy` ("busy total"). */
    fun gpuBusyPercent(text: String): Int? {
        val t = text.trim()
        if (t.isEmpty()) return null
        if (t.endsWith("%")) return t.removeSuffix("%").trim().toIntOrNull()?.coerceIn(0, 100)
        val parts = t.split(Regex("\\s+"))
        if (parts.size >= 2) {
            val busy = parts[0].toLongOrNull() ?: return null
            val total = parts[1].toLongOrNull() ?: return null
            if (total <= 0) return null
            return ((busy * 100) / total).toInt().coerceIn(0, 100)
        }
        return t.toIntOrNull()?.coerceIn(0, 100)
    }

    /** Thermal nodes report milli-°C on most kernels and whole °C on some. */
    fun celsius(text: String): Float? {
        val v = text.trim().toLongOrNull() ?: return null
        val c = if (abs(v) >= 1000) v / 1000f else v.toFloat()
        return c.takeIf { it > -40f && it < 150f }
    }

    /** BATTERY_PROPERTY_CURRENT_NOW is µA on most devices and mA on some; returns |µA|. */
    fun batteryCurrentMicroAmps(raw: Int): Long? {
        if (raw == Int.MIN_VALUE || raw == 0) return null
        val magnitude = abs(raw.toLong())
        return if (magnitude < 20_000) magnitude * 1000 else magnitude
    }

    /** Battery power in W, or null when implausible for a handheld. */
    fun watts(microAmps: Long, milliVolts: Int): Float? {
        if (milliVolts <= 0) return null
        val w = microAmps / 1e6f * (milliVolts / 1000f)
        return w.takeIf { it in 0.05f..60f }
    }
}

/** Samples host metrics for the HUD. Call from a background thread about once a second. */
class HudTelemetrySampler(context: Context) {
    private val app = context.applicationContext
    private val activityManager = app.getSystemService(ActivityManager::class.java)
    private val batteryManager = app.getSystemService(BatteryManager::class.java)
    private val powerManager = app.getSystemService(PowerManager::class.java)
    private val clockTicks = runCatching { Os.sysconf(OsConstants._SC_CLK_TCK) }.getOrDefault(100L).coerceAtLeast(1L)

    /** A sysfs/procfs node that stops being read after the first failure (missing or denied). */
    private class Node(private val path: String) {
        private var dead = false
        fun read(): String? {
            if (dead) return null
            return try {
                File(path).readText()
            } catch (_: Exception) {
                dead = true
                null
            }
        }
    }

    private val gpuBusyNodes = listOf(
        Node("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage"),
        Node("/sys/class/kgsl/kgsl-3d0/gpubusy"),
    )
    private val gpuFreqNodes = listOf(
        Node("/sys/class/kgsl/kgsl-3d0/gpuclk"),
        Node("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq"),
    )
    private val cpuFreqNodes = HashMap<Int, Node>()
    private val cpuTempNodes: List<Node> by lazy { thermalZones { it.contains("cpu") && !it.contains("gpu") } }
    private val gpuTempNodes: List<Node> by lazy {
        listOf(Node("/sys/class/kgsl/kgsl-3d0/temp")) + thermalZones { it.contains("gpu") }
    }

    private var lastWallNs = 0L
    private var lastProcessTicks = -1L
    private var lastThreadTicks = -1L
    private var lastThreadId = 0

    fun sample(config: HudConfig, emuTid: Int, emuCore: Int): HostSample {
        val now = SystemClock.elapsedRealtimeNanos()
        val seconds = if (lastWallNs > 0) (now - lastWallNs) / 1e9 else 0.0
        lastWallNs = now

        var emuCpu: Float? = null
        var processCpu: Float? = null
        var coreFreq: Int? = null
        var cpuTemp: Float? = null
        if (config.cpu) {
            val processTicks = readText("/proc/self/stat")?.let(HudParsers::statCpuTicks)
            val threadTicks = if (emuTid > 0) readText("/proc/self/task/$emuTid/stat")?.let(HudParsers::statCpuTicks) else null
            if (seconds > 0.2) {
                if (processTicks != null && lastProcessTicks >= 0) {
                    processCpu = percentOfCore(processTicks - lastProcessTicks, seconds)
                }
                if (threadTicks != null && lastThreadTicks >= 0 && emuTid == lastThreadId) {
                    emuCpu = percentOfCore(threadTicks - lastThreadTicks, seconds)
                }
            }
            lastProcessTicks = processTicks ?: -1L
            lastThreadTicks = threadTicks ?: -1L
            lastThreadId = emuTid
            cpuTemp = cpuTempNodes.mapNotNull { it.read()?.let(HudParsers::celsius) }.maxOrNull()
        } else {
            lastProcessTicks = -1L
            lastThreadTicks = -1L
        }
        if (config.cpuDetail && emuCore >= 0) {
            coreFreq = cpuFreqNodes.getOrPut(emuCore) {
                Node("/sys/devices/system/cpu/cpu$emuCore/cpufreq/scaling_cur_freq")
            }.read()?.trim()?.toLongOrNull()?.let { (it / 1000).toInt() }
        }

        var gpuBusy: Int? = null
        var gpuFreq: Int? = null
        var gpuTemp: Float? = null
        if (config.gpu) {
            gpuBusy = gpuBusyNodes.firstNotNullOfOrNull { it.read()?.let(HudParsers::gpuBusyPercent) }
            gpuFreq = gpuFreqNodes.firstNotNullOfOrNull { node ->
                node.read()?.trim()?.toLongOrNull()?.takeIf { it > 0 }?.let { (it / 1_000_000).toInt() }
            }
            gpuTemp = gpuTempNodes.firstNotNullOfOrNull { it.read()?.let(HudParsers::celsius) }
        }

        var rssMb: Int? = null
        var usedMb: Long? = null
        var totalMb: Long? = null
        if (config.ram) {
            rssMb = readText("/proc/self/status")?.let(HudParsers::vmRssKb)?.let { (it / 1024).toInt() }
            runCatching {
                val info = ActivityManager.MemoryInfo()
                activityManager?.getMemoryInfo(info)
                if (info.totalMem > 0) {
                    totalMb = info.totalMem / (1024 * 1024)
                    usedMb = (info.totalMem - info.availMem) / (1024 * 1024)
                }
            }
        }

        var batteryPercent: Int? = null
        var charging = false
        var powerW: Float? = null
        var batteryTemp: Float? = null
        if (config.battery) {
            val intent = runCatching { app.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) }.getOrNull()
            if (intent != null) {
                val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
                val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
                if (level >= 0 && scale > 0) batteryPercent = level * 100 / scale
                val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
                charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
                val tenths = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
                if (tenths != Int.MIN_VALUE && tenths > 0) batteryTemp = tenths / 10f
                val milliVolts = intent.getIntExtra(BatteryManager.EXTRA_VOLTAGE, -1)
                if (!charging) {
                    val raw = runCatching { batteryManager?.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW) }.getOrNull()
                    val microAmps = raw?.let(HudParsers::batteryCurrentMicroAmps)
                    if (microAmps != null) powerW = HudParsers.watts(microAmps, milliVolts)
                }
            }
        }

        var thermalStatus: Int? = null
        var headroom: Float? = null
        if (config.thermal && powerManager != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) thermalStatus = powerManager.currentThermalStatus
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                headroom = runCatching { powerManager.getThermalHeadroom(0) }.getOrNull()?.takeIf { !it.isNaN() }
            }
        }

        return HostSample(
            emuThreadCpuPercent = emuCpu,
            processCpuPercent = processCpu,
            emuCoreFreqMhz = coreFreq,
            cpuTempC = cpuTemp,
            gpuBusyPercent = gpuBusy,
            gpuFreqMhz = gpuFreq,
            gpuTempC = gpuTemp,
            processRssMb = rssMb,
            systemUsedMb = usedMb,
            systemTotalMb = totalMb,
            batteryPercent = batteryPercent,
            batteryCharging = charging,
            batteryPowerW = powerW,
            batteryTempC = batteryTemp,
            thermalStatus = thermalStatus,
            thermalHeadroom = headroom,
        )
    }

    private fun percentOfCore(ticks: Long, seconds: Double): Float? =
        if (ticks < 0) null else (ticks.toDouble() / clockTicks / seconds * 100.0).toFloat()

    private fun readText(path: String): String? = try {
        File(path).readText()
    } catch (_: Exception) {
        null
    }

    private fun thermalZones(match: (String) -> Boolean): List<Node> =
        (runCatching { File("/sys/class/thermal").listFiles() }.getOrNull() ?: emptyArray())
            .filter { it.name.startsWith("thermal_zone") }
            .sortedBy { it.name.removePrefix("thermal_zone").toIntOrNull() ?: Int.MAX_VALUE }
            .mapNotNull { zone ->
                val type = runCatching { File(zone, "type").readText().trim().lowercase() }.getOrNull()
                if (type != null && match(type)) Node(File(zone, "temp").path) else null
            }
            .take(16)
}
