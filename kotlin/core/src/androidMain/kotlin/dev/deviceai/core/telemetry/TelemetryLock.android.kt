package dev.deviceai.core.telemetry

import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
internal actual class TelemetryLock {
    private val delegate = ReentrantLock()
    actual fun <T> withLock(action: () -> T): T = delegate.withLock(action)
}
