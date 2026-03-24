package dev.deviceai.core.telemetry

import dev.deviceai.core.InternalDeviceAiApi

/**
 * Static hardware + OS snapshot collected once per flush.
 * No personally identifiable information is included.
 *
 * [installIdHash] is an FNV-1a hash of a per-install UUID salted with the current
 * month (YYYY-MM). The same device produces a different hash each calendar month,
 * making long-term cross-session tracking impossible while still enabling monthly
 * cohort counting.
 */
@InternalDeviceAiApi
expect object DeviceProfile {
    /** Platform name: "android", "ios", or the JVM os.name lowercased. */
    val osName: String
    /** OS version string, e.g. "15.0", "14", "13.1". */
    val osVersion: String
    /** Total physical RAM in megabytes. */
    val totalMemoryMb: Long
    /**
     * Anonymous, monthly-rotating device identifier.
     * Format: 8-char hex — FNV-1a(installUuid + YYYY-MM).
     * "anonymous" if storage is unavailable at the time of the call.
     */
    val installIdHash: String
}
