package dev.deviceai.core.backend

import dev.deviceai.core.telemetry.NetworkPolicy
import dev.deviceai.core.telemetry.TelemetryEngine
import dev.deviceai.core.telemetry.TelemetrySink
import dev.deviceai.core.telemetry.TelemetryLevel

internal expect fun createBackendClient(
    baseUrl: String,
    apiKey: String,
): BackendClient

internal expect fun createTelemetryEngine(
    level: TelemetryLevel,
    policy: NetworkPolicy,
    baseUrl: String,
    sessionId: String,
    customSink: TelemetrySink?,
    clientForSink: BackendClientImpl?,
    sessionProvider: (() -> DeviceSession?)?,
): TelemetryEngine
