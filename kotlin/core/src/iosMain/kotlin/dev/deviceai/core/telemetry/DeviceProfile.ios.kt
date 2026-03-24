package dev.deviceai.core.telemetry

import dev.deviceai.core.InternalDeviceAiApi
import kotlinx.cinterop.ExperimentalForeignApi
import platform.Foundation.NSProcessInfo
import platform.Foundation.NSUserDefaults
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

@InternalDeviceAiApi
@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalForeignApi::class, ExperimentalUuidApi::class)
actual object DeviceProfile {
    actual val osName: String = "ios"
    actual val osVersion: String = NSProcessInfo.processInfo.operatingSystemVersionString
    actual val totalMemoryMb: Long =
        (NSProcessInfo.processInfo.physicalMemory / 1024u / 1024u).toLong()

    actual val installIdHash: String by lazy {
        try {
            val key = "dev.deviceai.install_id"
            val defaults = NSUserDefaults.standardUserDefaults
            val uuid = defaults.stringForKey(key)
                ?: Uuid.random().toString().also { defaults.setObject(it, key) }
            fnv1a32(uuid + currentMonthKey())
        } catch (_: Exception) { "anonymous" }
    }
}
