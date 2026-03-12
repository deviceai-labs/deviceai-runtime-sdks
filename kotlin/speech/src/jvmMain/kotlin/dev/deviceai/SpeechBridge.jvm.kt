package dev.deviceai

import androidx.compose.runtime.Composable

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
actual object SpeechBridge {

    init {
        System.loadLibrary("speech_jni")
        println("[SpeechKMP] Loaded native library 'speech_jni'")
    }

    // ══════════════════════════════════════════════════════════════
    //              VOICE ACTIVITY DETECTION (VAD)
    // ══════════════════════════════════════════════════════════════

    actual fun initVad(modelPath: String, config: VadConfig): Boolean =
        nativeInitVad(modelPath, config.threshold, config.sampleRate)

    actual fun isSpeech(samples: FloatArray): Boolean =
        nativeIsSpeech(samples)

    actual fun processVadStream(samples: FloatArray, callback: VadCallback) =
        nativeProcessVadStream(samples, callback)

    actual fun resetVad() = nativeResetVad()

    actual fun shutdownVad() = nativeShutdownVad()

    // ══════════════════════════════════════════════════════════════
    //                    SPEECH-TO-TEXT (STT)
    // ══════════════════════════════════════════════════════════════

    actual fun initStt(modelPath: String, config: SttConfig): Boolean =
        nativeInitStt(
            modelPath,
            config.language,
            config.translateToEnglish,
            config.maxThreads,
            config.useGpu,
            config.useVad,
            config.singleSegment,
            config.noContext
        )

    actual fun transcribe(audioPath: String): String =
        nativeTranscribe(audioPath)

    actual fun transcribeDetailed(audioPath: String): TranscriptionResult =
        nativeTranscribeDetailed(audioPath)

    actual fun transcribeAudio(samples: FloatArray): String =
        nativeTranscribeAudio(samples)

    actual fun transcribeStream(samples: FloatArray, callback: SttStream) =
        nativeTranscribeStream(samples, callback)

    actual fun cancelStt() = nativeCancelStt()

    actual fun shutdownStt() = nativeShutdownStt()

    // ══════════════════════════════════════════════════════════════
    //                    TEXT-TO-SPEECH (TTS)
    // ══════════════════════════════════════════════════════════════

    actual fun initTts(modelPath: String, tokensPath: String, config: TtsConfig): Boolean =
        nativeInitTts(
            modelPath,
            tokensPath,
            config.espeakDataPath ?: "",
            config.voicesPath ?: "",
            config.speakerId ?: 0,
            config.speechRate,
            config.sampleRate,
            config.sentenceSilence
        )

    actual fun synthesize(text: String): ShortArray =
        nativeSynthesize(text)

    actual fun synthesizeToFile(text: String, outputPath: String): Boolean =
        nativeSynthesizeToFile(text, outputPath)

    actual fun synthesizeStream(text: String, callback: TtsStream) =
        nativeSynthesizeStream(text, callback)

    actual fun cancelTts() = nativeCancelTts()

    actual fun shutdownTts() = nativeShutdownTts()

    // ══════════════════════════════════════════════════════════════
    //                         UTILITIES
    // ══════════════════════════════════════════════════════════════

    @Composable
    actual fun getModelPath(modelFileName: String): String {
        // On desktop, assume model file is already an absolute path or in current directory
        return modelFileName
    }

    actual fun shutdown() {
        shutdownVad()
        shutdownStt()
        shutdownTts()
    }

    // ══════════════════════════════════════════════════════════════
    //                    NATIVE DECLARATIONS
    // ══════════════════════════════════════════════════════════════

    // VAD
    private external fun nativeInitVad(modelPath: String, threshold: Float, sampleRate: Int): Boolean
    private external fun nativeIsSpeech(samples: FloatArray): Boolean
    private external fun nativeProcessVadStream(samples: FloatArray, callback: VadCallback)
    private external fun nativeResetVad()
    private external fun nativeShutdownVad()

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
        speechRate: Float,
        sampleRate: Int,
        sentenceSilence: Float
    ): Boolean

    private external fun nativeSynthesize(text: String): ShortArray
    private external fun nativeSynthesizeToFile(text: String, outputPath: String): Boolean
    private external fun nativeSynthesizeStream(text: String, callback: TtsStream)
    private external fun nativeCancelTts()
    private external fun nativeShutdownTts()
}
