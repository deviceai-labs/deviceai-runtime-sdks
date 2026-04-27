package dev.deviceai

import dev.deviceai.core.DeviceAI
import dev.deviceai.core.InternalDeviceAiApi
import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.models.currentTimeMillis

@OptIn(InternalDeviceAiApi::class)
@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
actual object SpeechBridge {

    init {
        System.loadLibrary("speech_jni")
    }

    // Track current model IDs for telemetry
    private var sttModelId: String = ""
    private var ttsModelId: String = ""

    // ══════════════════════════════════════════════════════════════
    //                    SPEECH-TO-TEXT (STT)
    // ══════════════════════════════════════════════════════════════

    actual fun initStt(modelPath: String, config: SttConfig): Boolean {
        sttModelId = modelPath.substringAfterLast("/")
        val startMs = currentTimeMillis()
        val ok = nativeInitStt(
            modelPath,
            config.language,
            config.translateToEnglish,
            config.maxThreads,
            config.useGpu,
            config.useVad,
            config.singleSegment,
            config.noContext
        )
        if (ok) {
            DeviceAI.recordEvent(TelemetryEvent.ModelLoad(
                timestampMs = currentTimeMillis(),
                module      = "stt",
                modelId     = sttModelId,
                durationMs  = currentTimeMillis() - startMs,
            ))
        }
        return ok
    }

    actual fun transcribe(audioPath: String): String {
        val startMs = currentTimeMillis()
        val result = nativeTranscribe(audioPath)
        DeviceAI.recordEvent(TelemetryEvent.InferenceComplete(
            timestampMs    = currentTimeMillis(),
            module         = "stt",
            modelId        = sttModelId,
            latencyMs      = currentTimeMillis() - startMs,
            finishReason   = if (result.isNotEmpty()) "stop" else "empty",
        ))
        return result
    }

    actual fun transcribeDetailed(audioPath: String): TranscriptionResult {
        val startMs = currentTimeMillis()
        val result = nativeTranscribeDetailed(audioPath)
        DeviceAI.recordEvent(TelemetryEvent.InferenceComplete(
            timestampMs    = currentTimeMillis(),
            module         = "stt",
            modelId        = sttModelId,
            latencyMs      = currentTimeMillis() - startMs,
            inputLengthMs  = result.durationMs.toInt(),
            finishReason   = "stop",
        ))
        return result
    }

    actual fun transcribeAudio(samples: FloatArray): String {
        val startMs = currentTimeMillis()
        val audioDurationMs = (samples.size / 16000f * 1000f).toInt() // 16kHz mono
        val result = nativeTranscribeAudio(samples)
        DeviceAI.recordEvent(TelemetryEvent.InferenceComplete(
            timestampMs    = currentTimeMillis(),
            module         = "stt",
            modelId        = sttModelId,
            latencyMs      = currentTimeMillis() - startMs,
            inputLengthMs  = audioDurationMs,
            finishReason   = if (result.isNotEmpty()) "stop" else "empty",
        ))
        return result
    }

    actual fun transcribeStream(samples: FloatArray, callback: SttStream) =
        nativeTranscribeStream(samples, callback)

    actual fun cancelStt() = nativeCancelStt()

    actual fun shutdownStt() {
        DeviceAI.recordEvent(TelemetryEvent.ModelUnload(
            timestampMs = currentTimeMillis(),
            module      = "stt",
            modelId     = sttModelId,
        ))
        nativeShutdownStt()
    }

    // ══════════════════════════════════════════════════════════════
    //                    TEXT-TO-SPEECH (TTS)
    // ══════════════════════════════════════════════════════════════

    actual fun initTts(modelPath: String, tokensPath: String, config: TtsConfig): Boolean {
        ttsModelId = modelPath.substringAfterLast("/")
        val startMs = currentTimeMillis()
        val ok = nativeInitTts(
            modelPath,
            tokensPath,
            config.dataDir,
            config.voicesPath,
            config.speakerId ?: -1,
            config.speechRate
        )
        if (ok) {
            DeviceAI.recordEvent(TelemetryEvent.ModelLoad(
                timestampMs = currentTimeMillis(),
                module      = "tts",
                modelId     = ttsModelId,
                durationMs  = currentTimeMillis() - startMs,
            ))
        }
        return ok
    }

    actual fun synthesize(text: String): ShortArray {
        val startMs = currentTimeMillis()
        val result = nativeSynthesize(text)
        val audioDurationMs = (result.size / 22050f * 1000f).toInt() // ~22050 Hz mono
        DeviceAI.recordEvent(TelemetryEvent.InferenceComplete(
            timestampMs    = currentTimeMillis(),
            module         = "tts",
            modelId        = ttsModelId,
            latencyMs      = currentTimeMillis() - startMs,
            outputChars    = text.length,
            finishReason   = if (result.isNotEmpty()) "stop" else "empty",
        ))
        return result
    }

    actual fun synthesizeToFile(text: String, outputPath: String): Boolean {
        val startMs = currentTimeMillis()
        val ok = nativeSynthesizeToFile(text, outputPath)
        DeviceAI.recordEvent(TelemetryEvent.InferenceComplete(
            timestampMs    = currentTimeMillis(),
            module         = "tts",
            modelId        = ttsModelId,
            latencyMs      = currentTimeMillis() - startMs,
            outputChars    = text.length,
            finishReason   = if (ok) "stop" else "error",
        ))
        return ok
    }

    actual fun synthesizeStream(text: String, callback: TtsStream) =
        nativeSynthesizeStream(text, callback)

    actual fun cancelTts() = nativeCancelTts()

    actual fun shutdownTts() {
        DeviceAI.recordEvent(TelemetryEvent.ModelUnload(
            timestampMs = currentTimeMillis(),
            module      = "tts",
            modelId     = ttsModelId,
        ))
        nativeShutdownTts()
    }

    // ══════════════════════════════════════════════════════════════
    //                         UTILITIES
    // ══════════════════════════════════════════════════════════════

    actual fun shutdown() {
        shutdownStt()
        shutdownTts()
    }

    // ══════════════════════════════════════════════════════════════
    //                    NATIVE DECLARATIONS
    // ══════════════════════════════════════════════════════════════

    // STT
    private external fun nativeInitStt(
        modelPath: String,
        language: String,
        translate: Boolean,
        maxThreads: Int,
        useGpu: Boolean,
        useVad: Boolean,
        singleSegment: Boolean,
        noContext: Boolean
    ): Boolean

    private external fun nativeTranscribe(audioPath: String): String
    private external fun nativeTranscribeDetailed(audioPath: String): TranscriptionResult
    private external fun nativeTranscribeAudio(samples: FloatArray): String
    private external fun nativeTranscribeStream(samples: FloatArray, callback: SttStream)
    private external fun nativeCancelStt()
    private external fun nativeShutdownStt()

    // TTS
    private external fun nativeInitTts(
        modelPath: String,
        tokensPath: String,
        dataDir: String,
        voicesPath: String,
        speakerId: Int,
        speechRate: Float
    ): Boolean

    private external fun nativeSynthesize(text: String): ShortArray
    private external fun nativeSynthesizeToFile(text: String, outputPath: String): Boolean
    private external fun nativeSynthesizeStream(text: String, callback: TtsStream)
    private external fun nativeCancelTts()
    private external fun nativeShutdownTts()
}
