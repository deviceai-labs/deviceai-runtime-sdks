package dev.deviceai.core.backend

import dev.deviceai.core.telemetry.TelemetryEvent

internal interface BackendClient {
    suspend fun registerDevice(capabilityProfile: Map<String, Any?>): DeviceSession
    suspend fun fetchManifest(deviceToken: String): ManifestResponse
    suspend fun ingestTelemetry(deviceToken: String, sessionId: String, events: List<TelemetryEvent>)
    suspend fun refreshToken(session: DeviceSession): DeviceSession?
    fun close()
}
