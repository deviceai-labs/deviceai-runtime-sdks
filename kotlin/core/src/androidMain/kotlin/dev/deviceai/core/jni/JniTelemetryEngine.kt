package dev.deviceai.core.jni

import dev.deviceai.core.telemetry.NetworkPolicy
import dev.deviceai.core.telemetry.TelemetryEngine
import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.core.telemetry.TelemetryLevel
import dev.deviceai.models.currentTimeMillis

/**
 * JNI-backed [TelemetryEngine]. Delegates buffering, batching, backoff, and HTTP delivery
 * to the C++ [dai_telemetry_engine_t] via [CoreJniBridge].
 *
 * The C++ engine has its own worker thread and OkHttp executor — no Kotlin coroutines
 * involved in the delivery path.
 */
internal class JniTelemetryEngine(
    level: TelemetryLevel,
    policy: NetworkPolicy,
    baseUrl: String,
    flushThreshold: Int = 0,
) : TelemetryEngine {

    private val handle: Long = CoreJniBridge.createEngine(
        level               = level.ordinal,
        hasWifiChecker      = policy.isOnWifi != null,
        hasDataSaver        = policy.isDataSaver != null,
        dataSaverMultiplier = policy.dataSaverThresholdMultiplier,
        baseUrl             = baseUrl,
        flushThreshold      = flushThreshold,
    )

    override fun setSession(deviceToken: String, sessionId: String) {
        CoreJniBridge.setSession(handle, deviceToken, sessionId)
    }

    override fun record(event: TelemetryEvent) {
        val ts = currentTimeMillis()
        when (event) {
            is TelemetryEvent.ModelLoad -> CoreJniBridge.recordModelLoad(
                handle, ts, event.module, event.modelId, event.durationMs,
            )
            is TelemetryEvent.ModelUnload -> CoreJniBridge.recordModelUnload(
                handle, ts, event.module, event.modelId,
            )
            is TelemetryEvent.InferenceComplete -> CoreJniBridge.recordInferenceComplete(
                handle        = handle,
                timestampMs   = ts,
                module        = event.module,
                modelId       = event.modelId,
                latencyMs     = event.latencyMs,
                ttftMs        = event.ttftMs ?: 0L,       hasTtft          = event.ttftMs != null,
                tokensPerSec  = event.tokensPerSec ?: 0f, hasTps           = event.tokensPerSec != null,
                inputTokenCount  = event.inputTokenCount  ?: 0, hasInputTokens  = event.inputTokenCount != null,
                outputTokenCount = event.outputTokenCount ?: 0, hasOutputTokens = event.outputTokenCount != null,
                inputLengthMs    = event.inputLengthMs    ?: 0, hasInputLength  = event.inputLengthMs != null,
                outputChars      = event.outputChars      ?: 0, hasOutputChars  = event.outputChars != null,
                finishReason  = event.finishReason ?: "",
            )
            is TelemetryEvent.ManifestSync -> CoreJniBridge.recordManifestSync(
                handle, ts, event.success, event.modelCount, event.errorCode ?: "",
            )
            is TelemetryEvent.ControlPlaneAlert -> CoreJniBridge.recordControlPlaneAlert(
                handle, ts, event.alertType, event.modelId ?: "", event.rolloutId ?: "",
            )
            is TelemetryEvent.OtaDownload -> { /* OtaDownload forwarded via manifest sync path */ }
        }
    }

    override suspend fun flush() {
        CoreJniBridge.flushEngine(handle)
    }

    override fun close() {
        CoreJniBridge.destroyEngine(handle)
    }
}
