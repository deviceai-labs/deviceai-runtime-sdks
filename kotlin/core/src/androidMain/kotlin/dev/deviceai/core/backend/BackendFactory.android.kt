package dev.deviceai.core.backend

import dev.deviceai.core.jni.CoreJniBridge
import dev.deviceai.core.jni.JniBackendClient
import dev.deviceai.core.jni.JniHttpExecutor
import dev.deviceai.core.jni.JniStorageExecutor
import dev.deviceai.core.jni.JniTelemetryEngine
import dev.deviceai.core.telemetry.NetworkPolicy
import dev.deviceai.core.telemetry.TelemetryEngine
import dev.deviceai.core.telemetry.TelemetrySink
import dev.deviceai.core.telemetry.TelemetryLevel

private val jniInit = lazy {
    CoreJniBridge.init(JniHttpExecutor(), JniStorageExecutor())
}

internal actual fun createBackendClient(
    baseUrl: String,
    apiKey: String,
): BackendClient {
    jniInit.value
    return JniBackendClient(baseUrl, apiKey)
}

internal actual fun createTelemetryEngine(
    level: TelemetryLevel,
    policy: NetworkPolicy,
    baseUrl: String,
    sessionId: String,
    customSink: TelemetrySink?,
    clientForSink: BackendClientImpl?,
    sessionProvider: (() -> DeviceSession?)?,
): TelemetryEngine {
    jniInit.value
    // JNI engine handles delivery internally — customSink and clientForSink not used on this path.
    return JniTelemetryEngine(level, policy, baseUrl)
}
