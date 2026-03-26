package dev.deviceai.core.telemetry

import kotlinx.cinterop.ExperimentalForeignApi
import platform.Foundation.NSLock

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalForeignApi::class)
internal actual class TelemetryLock {
    private val nsLock = NSLock()
    actual fun <T> withLock(action: () -> T): T {
        nsLock.lock()
        return try { action() } finally { nsLock.unlock() }
    }
}
