package dev.deviceai.core.telemetry

import dev.deviceai.core.CoreSDKLogger
import dev.deviceai.core.DeviceAI
import dev.deviceai.core.InternalDeviceAiApi
import dev.deviceai.core.TelemetryMode
import dev.deviceai.models.currentTimeMillis
import kotlin.random.Random

/**
 * Buffers [TelemetryEvent]s and flushes them to the DeviceAI backend.
 *
 * | Mode              | Collects | Logs locally | Uploads |
 * |-------------------|----------|--------------|---------|
 * | OFF (default)     | ✗        | ✗            | ✗       |
 * | LOCAL             | ✓        | ✓            | ✗       |
 * | MANAGED_BASIC     | ✓        | ✓            | ✓ (Phase 2) |
 * | MANAGED_FULL      | ✓        | ✓            | ✓ (Phase 2) |
 *
 * Backpressure: events are dropped (not queued) when the per-minute cap is reached.
 * Buffer is in-memory only. Events are lost on process kill until Phase 2 disk queue.
 *
 * What is **never** recorded: prompt text, response text, audio, raw exception messages.
 */
@InternalDeviceAiApi
object TelemetryReporter {

    private const val TAG = "Telemetry"
    private const val MAX_BUFFER = 100

    private val buffer = mutableListOf<TelemetryEvent>()

    // ── Rate limiting ─────────────────────────────────────────────────────────
    private var eventsThisMinute = 0
    private var minuteWindowStart = 0L

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Record one telemetry event.
     * No-op when mode is [TelemetryMode.OFF] or backpressure limits are reached.
     */
    fun record(event: TelemetryEvent) {
        val config = DeviceAI.cloudConfig ?: return
        if (config.telemetry == TelemetryMode.OFF) return
        if (!passesSampling(config.telemetrySamplingRate)) return
        if (!passesRateLimit(config.telemetryMaxPerMinute)) return

        if (buffer.size >= MAX_BUFFER) buffer.removeAt(0)
        buffer.add(event)

        CoreSDKLogger.debug(TAG, formatEvent(event))

        // TODO Phase 2: trigger async flush when mode is MANAGED_* and
        //               buffer reaches threshold or Wi-Fi + charging.
    }

    /**
     * Flush buffered events to the backend.
     *
     * - [TelemetryMode.OFF] / [TelemetryMode.LOCAL]: no-op (never uploads).
     * - [TelemetryMode.MANAGED_BASIC] / [TelemetryMode.MANAGED_FULL]: POST to backend.
     *   Currently a stub — backend endpoint wired in Phase 2.
     *
     * TODO Phase 2: disk queue for durability across app kills (bounded, max 500 events).
     */
    fun flush() {
        val config = DeviceAI.cloudConfig ?: return
        if (config.telemetry == TelemetryMode.OFF || config.telemetry == TelemetryMode.LOCAL) return
        if (buffer.isEmpty()) return

        val snapshot = buffer.toList()
        buffer.clear()

        CoreSDKLogger.debug(TAG,
            "flush ${snapshot.size} events — " +
            "id=${DeviceProfile.installIdHash} " +
            "os=${DeviceProfile.osName}/${DeviceProfile.osVersion} " +
            "ram=${DeviceProfile.totalMemoryMb}MB " +
            "schema=${TelemetryEvent.SCHEMA_VERSION} sdk=${TelemetryEvent.SDK_BUILD} " +
            "— backend not yet connected"
        )

        // TODO Phase 2:
        // POST ${config.baseUrl}/api/v1/sdk/telemetry
        // body: { schemaVersion, sdkBuild, installIdHash, deviceProfile, events: snapshot }
        // Headers: Authorization: Bearer ${config.apiKey}
        // Retry: exponential back-off, max 3 attempts
        // On 429: respect Retry-After header, cap future samplingRate client-side
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun passesSampling(rate: Float): Boolean =
        rate >= 1f || Random.nextFloat() <= rate

    private fun passesRateLimit(maxPerMinute: Int): Boolean {
        val now = currentTimeMillis()
        if (now - minuteWindowStart > 60_000L) {
            minuteWindowStart = now
            eventsThisMinute = 0
        }
        if (eventsThisMinute >= maxPerMinute) {
            CoreSDKLogger.debug(TAG, "rate cap hit ($maxPerMinute/min) — event dropped")
            return false
        }
        eventsThisMinute++
        return true
    }

    private fun formatEvent(event: TelemetryEvent): String = when (event) {
        is TelemetryEvent.LlmInference ->
            "LLM model=${event.modelId} " +
            "in=${event.inputTokens}tok(${if (event.inputTokensEstimated) "est" else "exact"}) " +
            "out_pieces=${event.outputPieces} out_tokens=${event.outputTokens ?: "?"} " +
            "%.1ftok/s ${event.totalMs}ms".format(event.tokensPerSecond) +
            " success=${event.success}" +
            (event.errorCode?.let { " err=$it" } ?: "")
        is TelemetryEvent.SttTranscription ->
            "STT model=${event.modelId} audio=${event.audioDurationMs}ms " +
            "rtf=%.2f".format(event.realTimeFactor) +
            " success=${event.success}"
        is TelemetryEvent.TtsSynthesis ->
            "TTS model=${event.modelId} chars=${event.inputChars} " +
            "audio=${event.outputAudioMs}ms ${event.totalMs}ms " +
            "success=${event.success}"
        is TelemetryEvent.VlmInference ->
            "VLM model=${event.modelId} out_pieces=${event.outputPieces} " +
            "${event.totalMs}ms success=${event.success}"
    }
}
