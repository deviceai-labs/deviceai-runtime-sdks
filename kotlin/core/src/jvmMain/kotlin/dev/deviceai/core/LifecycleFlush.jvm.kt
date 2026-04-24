package dev.deviceai.core

internal actual fun DeviceAI.registerLifecycleFlush(context: Any?) {
    // No lifecycle observer on JVM — telemetry flushed on shutdown() only.
}
