package dev.deviceai.core.backend

import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.core.telemetry.TelemetrySink

/**
 * Default [TelemetrySink] for managed mode — delivers events to the DeviceAI backend
 * via [BackendClient]. Completely internal; not visible to application code.
 */
internal class ManagedTelemetrySink(
    private val client: BackendClientImpl,
    private val sessionProvider: () -> DeviceSession?,
    private val sessionId: String,
) : TelemetrySink {

    override suspend fun ingest(events: List<TelemetryEvent>) {
        val session = sessionProvider()
            ?: return // session not yet established — engine will retry
        client.ingestTelemetry(session.token, sessionId, events)
    }
}
