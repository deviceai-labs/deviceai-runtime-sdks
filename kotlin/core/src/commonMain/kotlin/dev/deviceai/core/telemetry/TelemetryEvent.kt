package dev.deviceai.core.telemetry

import kotlinx.serialization.json.*

/**
 * A single structured telemetry event emitted by the DeviceAI SDK.
 *
 * Events are buffered on-device via [TelemetryEngine] and flushed according to
 * their [priority] and the active [NetworkPolicy]:
 * - [TelemetryPriority.Normal] — batched, any network, exponential backoff on failure
 * - [TelemetryPriority.WifiPreferred] — deferred until Wi-Fi when checker is provided
 * - [TelemetryPriority.Critical] — immediate, any network, bypasses batching and data-saver
 */
sealed class TelemetryEvent {
    abstract val type: String
    abstract val timestampMs: Long
    abstract val priority: TelemetryPriority

    /**
     * A model was loaded into memory. Emitted for LLM, STT, and TTS modules.
     * Collected at [TelemetryLevel.Minimal] and above.
     */
    data class ModelLoad(
        override val timestampMs: Long,
        /** Module name: "llm", "stt", "tts" */
        val module: String,
        val modelId: String,
        val durationMs: Long,
        /** RAM increase in MB at load time. Null if measurement unavailable. */
        val ramDeltaMb: Float? = null,
    ) : TelemetryEvent() {
        override val type: String get() = "model_load"
        override val priority: TelemetryPriority get() = TelemetryPriority.Normal
    }

    /**
     * A model was unloaded / released from memory.
     * Collected at [TelemetryLevel.Minimal] and above.
     */
    data class ModelUnload(
        override val timestampMs: Long,
        val module: String,
        val modelId: String,
    ) : TelemetryEvent() {
        override val type: String get() = "model_unload"
        override val priority: TelemetryPriority get() = TelemetryPriority.Normal
    }

    /**
     * An inference request completed (or was cancelled).
     * Collected at [TelemetryLevel.Minimal] and above.
     */
    data class InferenceComplete(
        override val timestampMs: Long,
        val module: String,
        val modelId: String,
        /** Total wall-clock time from request start to last token / final output. */
        val latencyMs: Long,
        /** LLM streaming only: ms from request start to first token received. */
        val ttftMs: Long? = null,
        /** LLM only: generation throughput in output tokens/sec. */
        val tokensPerSec: Float? = null,
        /** LLM only: number of prompt (input) tokens. */
        val inputTokenCount: Int? = null,
        /** LLM only: number of generated (output) tokens. */
        val outputTokenCount: Int? = null,
        /** STT only: duration of audio input in ms. */
        val inputLengthMs: Int? = null,
        /** TTS only: number of characters synthesized. */
        val outputChars: Int? = null,
        /** How the inference ended: "stop", "max_tokens", "cancel", "error". */
        val finishReason: String? = null,
    ) : TelemetryEvent() {
        override val type: String get() = "inference_complete"
        override val priority: TelemetryPriority get() = TelemetryPriority.Normal
    }

    /**
     * An OTA model download completed or failed.
     * Collected at [TelemetryLevel.Full] only.
     */
    data class OtaDownload(
        override val timestampMs: Long,
        val modelId: String,
        val version: String,
        val sizeBytes: Long,
        val durationMs: Long,
        val success: Boolean,
        /** Short error code on failure, e.g. "network_error", "checksum_mismatch". */
        val errorCode: String? = null,
    ) : TelemetryEvent() {
        override val type: String get() = "ota_download"
        override val priority: TelemetryPriority get() = TelemetryPriority.Normal
    }

    /**
     * A manifest sync completed or failed.
     * Collected at [TelemetryLevel.Full] only.
     */
    data class ManifestSync(
        override val timestampMs: Long,
        val success: Boolean,
        val modelCount: Int = 0,
        val errorCode: String? = null,
    ) : TelemetryEvent() {
        override val type: String get() = "manifest_sync"
        override val priority: TelemetryPriority get() = TelemetryPriority.Normal
    }

    /**
     * A kill-switch or forced rollback was detected in the manifest.
     *
     * Always [TelemetryPriority.Critical] — sent immediately on any network.
     * Collected at all [TelemetryLevel]s above [TelemetryLevel.Off].
     */
    data class ControlPlaneAlert(
        override val timestampMs: Long,
        /** "kill_switch" | "forced_rollback" | "model_revoked" */
        val alertType: String,
        val modelId: String? = null,
        val rolloutId: String? = null,
    ) : TelemetryEvent() {
        override val type: String get() = "control_plane_alert"
        override val priority: TelemetryPriority get() = TelemetryPriority.Critical
    }

    /** Converts this event to a [JsonObject] for the telemetry batch payload. */
    fun toJsonObject(): JsonObject = buildJsonObject {
        put("type", type)
        put("timestamp_ms", timestampMs)
        when (this@TelemetryEvent) {
            is ModelLoad -> {
                put("module", module)
                put("model_id", modelId)
                put("duration_ms", durationMs)
                ramDeltaMb?.let { put("ram_delta_mb", it) }
            }
            is ModelUnload -> {
                put("module", module)
                put("model_id", modelId)
            }
            is InferenceComplete -> {
                put("module", module)
                put("model_id", modelId)
                put("latency_ms", latencyMs)
                ttftMs?.let { put("ttft_ms", it) }
                tokensPerSec?.let { put("tokens_per_sec", it) }
                inputTokenCount?.let { put("input_token_count", it) }
                outputTokenCount?.let { put("output_token_count", it) }
                inputLengthMs?.let { put("input_length_ms", it) }
                outputChars?.let { put("output_chars", it) }
                finishReason?.let { put("finish_reason", it) }
            }
            is OtaDownload -> {
                put("model_id", modelId)
                put("version", version)
                put("size_bytes", sizeBytes)
                put("duration_ms", durationMs)
                put("success", success)
                errorCode?.let { put("error_code", it) }
            }
            is ManifestSync -> {
                put("success", success)
                put("model_count", modelCount)
                errorCode?.let { put("error_code", it) }
            }
            is ControlPlaneAlert -> {
                put("alert_type", alertType)
                modelId?.let { put("model_id", it) }
                rolloutId?.let { put("rollout_id", it) }
            }
        }
    }
}
