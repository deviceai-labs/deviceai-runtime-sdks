package dev.deviceai.core.telemetry

internal interface TelemetryEngine {
    fun record(event: TelemetryEvent)
    suspend fun flush()
    fun close()

    /**
     * Called after device registration so the engine can authenticate telemetry batches.
     * Default no-op — the Kotlin/Ktor engine gets its session via [ManagedTelemetrySink]'s
     * sessionProvider lambda. The JNI engine needs this explicit call.
     */
    fun setSession(deviceToken: String, sessionId: String) = Unit
}
