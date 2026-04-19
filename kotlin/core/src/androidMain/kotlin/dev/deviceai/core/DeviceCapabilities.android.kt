package dev.deviceai.core

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.os.Environment
import android.os.StatFs

actual fun detectCapabilities(context: Any?): DeviceCapabilities {
    val ctx = context as? Context

    // RAM
    val ramGb = if (ctx != null) {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memInfo = ActivityManager.MemoryInfo()
        am.getMemoryInfo(memInfo)
        memInfo.totalMem.toDouble() / (1024.0 * 1024.0 * 1024.0)
    } else {
        val runtime = Runtime.getRuntime()
        runtime.maxMemory().toDouble() / (1024.0 * 1024.0 * 1024.0)
    }

    // CPU cores
    val cpuCores = Runtime.getRuntime().availableProcessors()

    // NPU — NNAPI available on Android 8.1+ (API 27)
    val hasNpu = Build.VERSION.SDK_INT >= 27

    // SoC model — available on Android 12+ (API 31)
    val socModel = if (Build.VERSION.SDK_INT >= 31) {
        Build.SOC_MODEL.takeIf { it.isNotBlank() && it != "unknown" }
    } else {
        Build.HARDWARE.takeIf { it.isNotBlank() }
    }

    // GPU — requires EGL context which we may not have at init time.
    // Skip for now; can be populated later if an OpenGL context is available.
    val gpuRenderer: String? = null

    // Available storage
    val storageAvailableMb = try {
        val stat = StatFs(android.os.Environment.getDataDirectory().path)
        stat.availableBytes / (1024L * 1024L)
    } catch (_: Exception) {
        null
    }

    return DeviceCapabilities(
        ramGb              = "%.1f".format(ramGb).toDouble(),
        cpuCores           = cpuCores,
        hasNpu             = hasNpu,
        socModel           = socModel,
        gpuRenderer        = gpuRenderer,
        storageAvailableMb = storageAvailableMb,
    )
}
