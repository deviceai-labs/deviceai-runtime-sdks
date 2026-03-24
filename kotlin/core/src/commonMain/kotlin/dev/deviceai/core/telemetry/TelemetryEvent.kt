package dev.deviceai.core.telemetry

import dev.deviceai.core.InternalDeviceAiApi

/**
 * All telemetry events emitted by the DeviceAI SDK.
 *
 * Rules that apply to every subtype:
 * - No prompt text, response text, audio content, or speech content ever appears here.
 * - No personally identifiable information. [installIdHash] in [DeviceProfile] rotates monthly.
 * - Error detail is an [errorCode] string only — raw exception messages are never included
 *   unless the app explicitly enables [dev.deviceai.core.TelemetryMode.MANAGED_FULL].
 *
 * Schema migrations: increment [SCHEMA_VERSION] and handle old versions on the backend.
 */
@InternalDeviceAiApi
sealed class TelemetryEvent {

    abstract val generationId: String
    abstract val modelId: String
    abstract val totalMs: Long
    abstract val success: Boolean
    /** Opaque error bucket — e.g. "OOM", "DECODE_FAILED", "CANCELLED", "UNKNOWN". Never a raw message. */
    abstract val errorCode: String?
    abstract val timestampMs: Long
    abstract val schemaVersion: Int

    // ── LLM ──────────────────────────────────────────────────────────────────

    /**
     * One LLM inference call (streaming or blocking).
     *
     * @param inputTokens           Estimated or exact input token count.
     * @param inputTokensEstimated  true = chars/4 estimate; false = exact from tokenizer.
     * @param outputPieces          Number of llama.cpp token-piece callbacks received.
     *                              Always populated.
     * @param outputTokens          True decoded token count when the C++ layer exposes it.
     *                              null until that bridge is wired.
     * @param tokensPerSecond       outputPieces * 1000 / totalMs.
     */
    data class LlmInference(
        override val generationId: String,
        override val modelId: String,
        val inputTokens: Int,
        val inputTokensEstimated: Boolean,
        val outputPieces: Int,
        val outputTokens: Int?,
        override val totalMs: Long,
        val tokensPerSecond: Float,
        override val success: Boolean,
        override val errorCode: String?,
        override val timestampMs: Long,
        override val schemaVersion: Int = SCHEMA_VERSION,
    ) : TelemetryEvent()

    // ── STT ──────────────────────────────────────────────────────────────────

    /**
     * One speech-to-text transcription.
     * Contract defined now; emitted when the speech module is instrumented (Phase 2).
     *
     * @param audioDurationMs   Length of the input audio in ms.
     * @param outputWordCount   Approximate word count of the transcript.
     * @param realTimeFactor    totalMs / audioDurationMs — values < 1.0 = faster than real-time.
     */
    data class SttTranscription(
        override val generationId: String,
        override val modelId: String,
        val audioDurationMs: Long,
        val outputWordCount: Int,
        val realTimeFactor: Float,
        override val totalMs: Long,
        override val success: Boolean,
        override val errorCode: String?,
        override val timestampMs: Long,
        override val schemaVersion: Int = SCHEMA_VERSION,
    ) : TelemetryEvent()

    // ── TTS ──────────────────────────────────────────────────────────────────

    /**
     * One text-to-speech synthesis call.
     * Contract defined now; emitted when the speech module is instrumented (Phase 2).
     *
     * @param inputChars      Character count of the text to synthesise.
     * @param outputAudioMs   Duration of the generated audio clip in ms.
     */
    data class TtsSynthesis(
        override val generationId: String,
        override val modelId: String,
        val inputChars: Int,
        val outputAudioMs: Long,
        override val totalMs: Long,
        override val success: Boolean,
        override val errorCode: String?,
        override val timestampMs: Long,
        override val schemaVersion: Int = SCHEMA_VERSION,
    ) : TelemetryEvent()

    // ── VLM ──────────────────────────────────────────────────────────────────

    /**
     * One vision-language model inference call.
     * Contract defined now; emitted when the VLM module ships (Phase 3).
     */
    data class VlmInference(
        override val generationId: String,
        override val modelId: String,
        val inputTokens: Int,
        val inputTokensEstimated: Boolean,
        val outputPieces: Int,
        val outputTokens: Int?,
        override val totalMs: Long,
        val tokensPerSecond: Float,
        override val success: Boolean,
        override val errorCode: String?,
        override val timestampMs: Long,
        override val schemaVersion: Int = SCHEMA_VERSION,
    ) : TelemetryEvent()

    companion object {
        /** Increment when any field is added, renamed, or removed. */
        const val SCHEMA_VERSION = 1
        /** Set at release build time. Used by the backend to correlate SDK bugs. */
        const val SDK_BUILD = "0.2.0-alpha02"

        // ── Error codes ───────────────────────────────────────────────────────
        // Use these constants when creating events — never pass raw exception messages.

        const val ERR_OOM              = "OOM"
        const val ERR_DECODE_FAILED    = "DECODE_FAILED"
        const val ERR_TOKENIZE_FAILED  = "TOKENIZE_FAILED"
        const val ERR_CANCELLED        = "CANCELLED"
        const val ERR_UNKNOWN          = "UNKNOWN"
    }
}
