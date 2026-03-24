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
 * | Mode              | API key required | Collects | Logs locally | Uploads     |
 * |-------------------|-----------------|----------|--------------|-------------|
 * | OFF (default)     | —               | ✗        | ✗            | ✗           |
 * | LOCAL             | no              | ✓        | ✓            | ✗           |
 * | MANAGED_BASIC     | **yes**         | ✓        | ✓            | ✓ (Phase 2) |
 * | MANAGED_FULL      | **yes**         | ✓        | ✓            | ✓ (Phase 2) |
 *
 * If [TelemetryMode.MANAGED_BASIC] or [TelemetryMode.MANAGED_FULL] is requested but no
 * API key is configured, the reporter silently degrades to [TelemetryMode.LOCAL] behaviour
 * (collect + log, never upload).
 *
 * Thread safety: [record] and [flush] may be called concurrently from different
 * coroutine dispatchers. All shared mutable state is protected by [lock].
 * Logging happens outside the lock to avoid contention.
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

    private val lock = Any()

    // ── Protected by lock ─────────────────────────────────────────────────────
    private val buffer = mutableListOf<TelemetryEvent>()
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

        val accepted: Boolean
        val rateLimitHit: Boolean

        synchronized(lock) {
            rateLimitHit = !passesRateLimitLocked(config.telemetryMaxPerMinute)
            if (rateLimitHit) {
                accepted = false
            } else {
                if (buffer.size >= MAX_BUFFER) buffer.removeAt(0)
                buffer.add(event)
                accepted = true
            }
        }

        // Logging outside lock — no contention on I/O.
        if (rateLimitHit) {
            CoreSDKLogger.debug(TAG, "rate cap hit (${config.telemetryMaxPerMinute}/min) — event dropped")
        } else if (accepted) {
            CoreSDKLogger.debug(TAG, formatEvent(event))
        }

        // TODO Phase 2: trigger async flush when mode is MANAGED_* and
        //               buffer reaches threshold or Wi-Fi + charging.
    }

    /**
     * Flush buffered events to the backend.
     *
     * - [TelemetryMode.OFF] / [TelemetryMode.LOCAL]: no-op (never uploads).
     * - [TelemetryMode.MANAGED_BASIC] / [TelemetryMode.MANAGED_FULL]: POST to backend.
     *   Degrades to LOCAL behaviour silently when no API key is present.
     *   Currently a stub — backend endpoint wired in Phase 2.
     *
     * TODO Phase 2: disk queue for durability across app kills (bounded, max 500 events).
     */
    fun flush() {
        val config = DeviceAI.cloudConfig ?: return
        val effectiveMode = resolveEffectiveMode(config.telemetry, config.apiKey)
        if (effectiveMode == TelemetryMode.OFF || effectiveMode == TelemetryMode.LOCAL) return

        val snapshot: List<TelemetryEvent>
        synchronized(lock) {
            if (buffer.isEmpty()) return
            snapshot = buffer.toList()
            buffer.clear()
        }

        // Logging and network I/O outside lock.
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

    /**
     * Resolve the mode that will actually execute.
     * MANAGED_* without an API key degrades to LOCAL — no upload attempted.
     */
    private fun resolveEffectiveMode(requested: TelemetryMode, apiKey: String?): TelemetryMode =
        if (requested == TelemetryMode.MANAGED_BASIC || requested == TelemetryMode.MANAGED_FULL) {
            if (apiKey.isNullOrBlank()) {
                CoreSDKLogger.warn(TAG,
                    "$requested requires an API key — degrading to LOCAL (collect + log only). " +
                    "Set apiKey in DeviceAI.initialize() to enable uploads."
                )
                TelemetryMode.LOCAL
            } else {
                requested
            }
        } else {
            requested
        }

    private fun passesSampling(rate: Float): Boolean =
        rate >= 1f || Random.nextFloat() <= rate

    /** Must be called inside [synchronized(lock)]. */
    private fun passesRateLimitLocked(maxPerMinute: Int): Boolean {
        val now = currentTimeMillis()
        if (now - minuteWindowStart > 60_000L) {
            minuteWindowStart = now
            eventsThisMinute = 0
        }
        if (eventsThisMinute >= maxPerMinute) return false
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
