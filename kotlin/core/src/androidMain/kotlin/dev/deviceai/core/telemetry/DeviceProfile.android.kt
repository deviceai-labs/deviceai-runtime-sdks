package dev.deviceai.core.telemetry

import android.os.Build
import dev.deviceai.core.InternalDeviceAiApi
import dev.deviceai.models.PlatformStorage
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

@InternalDeviceAiApi
@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalUuidApi::class)
actual object DeviceProfile {
    actual val osName: String = "android"
    actual val osVersion: String = Build.VERSION.RELEASE

    actual val totalMemoryMb: Long by lazy {
        try {
            java.io.File("/proc/meminfo")
                .readLines()
                .firstOrNull { it.startsWith("MemTotal:") }
                ?.split("\\s+".toRegex())
                ?.getOrNull(1)
                ?.toLongOrNull()
                ?.let { it / 1024 } // kB → MB
                ?: -1L
        } catch (_: Exception) { -1L }
    }

    actual val installIdHash: String by lazy {
        try {
            val filesDir = PlatformStorage.tryGetFilesDir()
                ?: return@lazy "anonymous"
            val idFile = java.io.File(filesDir, "dai_install_id")
            val uuid = if (idFile.exists()) idFile.readText().trim()
                       else Uuid.random().toString().also { idFile.writeText(it) }
            fnv1a32(uuid + currentMonthKey())
        } catch (_: Exception) { "anonymous" }
    }
}
