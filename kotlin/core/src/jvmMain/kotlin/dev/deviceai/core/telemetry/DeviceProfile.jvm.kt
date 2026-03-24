package dev.deviceai.core.telemetry

import dev.deviceai.core.InternalDeviceAiApi
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

@InternalDeviceAiApi
@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalUuidApi::class)
actual object DeviceProfile {
    actual val osName: String = System.getProperty("os.name")?.lowercase() ?: "jvm"
    actual val osVersion: String = System.getProperty("os.version") ?: "unknown"
    actual val totalMemoryMb: Long = Runtime.getRuntime().maxMemory() / 1024 / 1024

    actual val installIdHash: String by lazy {
        try {
            val idFile = java.io.File(
                System.getProperty("user.home") ?: return@lazy "anonymous",
                ".deviceai/install_id"
            )
            idFile.parentFile?.mkdirs()
            val uuid = if (idFile.exists()) idFile.readText().trim()
                       else Uuid.random().toString().also { idFile.writeText(it) }
            fnv1a32(uuid + currentMonthKey())
        } catch (_: Exception) { "anonymous" }
    }
}
