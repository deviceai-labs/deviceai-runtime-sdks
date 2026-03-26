package dev.deviceai.core.telemetry

/**
 * Platform-appropriate mutual exclusion lock for telemetry state.
 *
 * - JVM / Android: backed by [java.util.concurrent.locks.ReentrantLock]
 * - iOS / Native:  backed by [platform.Foundation.NSLock]
 */
internal expect class TelemetryLock() {
    fun <T> withLock(action: () -> T): T
}
