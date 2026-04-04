package dev.deviceai.core.telemetry

/**
 * Receives batches of [TelemetryEvent]s from the SDK for delivery.
 *
 * ## Default behaviour
 * In managed mode the SDK uses an internal sink that sends events to the
 * DeviceAI backend. You do not need to implement this interface for normal use.
 *
 * ## Custom sink
 * Provide your own implementation in [dev.deviceai.core.CloudConfig.Builder.telemetrySink]
 * to route SDK events to your own analytics pipeline (e.g. Mixpanel, Amplitude, DataDog):
 *
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     telemetry = TelemetryLevel.Minimal
 *     telemetrySink = object : TelemetrySink {
 *         override suspend fun ingest(events: List<TelemetryEvent>) {
 *             myAnalytics.track(events.map { it.toMyEvent() })
 *         }
 *     }
 * }
 * ```
 *
 * A custom sink **replaces** the DeviceAI backend sink entirely — events will not
 * be sent to DeviceAI when a custom sink is set.
 *
 * ## Contract
 * - [ingest] is called from a background coroutine — do not block the main thread.
 * - Throw any exception to signal failure; [TelemetryEngine] will retry with backoff.
 * - Batches contain at most 500 events.
 */
interface TelemetrySink {
    /**
     * Deliver a batch of events. Called by the SDK engine after buffering.
     * @throws Exception on transient failure — the engine will retry with exponential backoff.
     */
    suspend fun ingest(events: List<TelemetryEvent>)
}
