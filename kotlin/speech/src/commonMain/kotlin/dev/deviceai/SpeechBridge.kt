package dev.deviceai

import androidx.compose.runtime.Composable

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
expect object SpeechBridge {

    // ══════════════════════════════════════════════════════════════
    //              VOICE ACTIVITY DETECTION (VAD)
    // ══════════════════════════════════════════════════════════════

    /**
     * Load a Silero VAD model. Optional but recommended — enables neural VAD
     * for both standalone voice detection and pre-processing inside STT.
     *
     * Model file (~1MB) is downloaded separately via ModelRegistry.
     *
     * @param modelPath Absolute path to silero_vad.onnx
     * @param config    Threshold and sample rate configuration
     * @return true if initialization succeeded
     */
    fun initVad(modelPath: String, config: VadConfig = VadConfig()): Boolean

    /**
     * Determine whether a PCM buffer contains speech.
     *
     * @param samples Float32 audio at the sample rate from VadConfig
     * @return true if speech detected above the configured threshold
     */
    fun isSpeech(samples: FloatArray): Boolean

    /**
     * Process an audio chunk and fire speech start/end callbacks.
     *
     * Call repeatedly with microphone chunks (512 samples = 32ms at 16kHz)
     * to detect when the user starts and stops speaking in real time.
     *
     * @param samples  Audio chunk (typically 512 samples per call)
     * @param callback [VadCallback.onSpeechStart] / [VadCallback.onSpeechEnd]
     */
    fun processVadStream(samples: FloatArray, callback: VadCallback)

    /**
     * Reset internal LSTM state. Call between independent recording sessions.
     */
    fun resetVad()

    /**
     * Unload the Silero model and release VAD resources.
     */
    fun shutdownVad()

    // ══════════════════════════════════════════════════════════════
    //                    SPEECH-TO-TEXT (STT)
    // ══════════════════════════════════════════════════════════════

    /**
     * Initialize the STT engine with a Whisper model.
     *
     * @param modelPath Absolute path to .bin model file (ggml format)
     * @param config Optional configuration parameters
     * @return true if initialization succeeded
     */
    fun initStt(modelPath: String, config: SttConfig = SttConfig()): Boolean

    /**
     * Transcribe an audio file to text.
     *
     * @param audioPath Path to WAV file (16kHz, mono, 16-bit PCM)
     * @return Transcribed text
     */
    fun transcribe(audioPath: String): String

    /**
     * Transcribe with detailed results including timestamps.
     *
     * @param audioPath Path to WAV file
     * @return TranscriptionResult with segments and timing
     */
    fun transcribeDetailed(audioPath: String): TranscriptionResult

    /**
     * Transcribe raw PCM audio samples.
     *
     * @param samples Float array of audio samples (16kHz, mono, normalized -1.0 to 1.0)
     * @return Transcribed text
     */
    fun transcribeAudio(samples: FloatArray): String

    /**
     * Stream transcription with real-time callbacks.
     *
     * @param samples Audio samples to transcribe
     * @param callback Callbacks for partial/final results
     */
    fun transcribeStream(samples: FloatArray, callback: SttStream)

    /**
     * Cancel ongoing transcription.
     */
    fun cancelStt()

    /**
     * Release STT resources and unload model.
     */
    fun shutdownStt()

    // ══════════════════════════════════════════════════════════════
    //                    TEXT-TO-SPEECH (TTS)
    // ══════════════════════════════════════════════════════════════

    /**
     * Initialize the TTS engine with a sherpa-onnx voice model.
     *
     * Supports two model families — auto-detected from [TtsConfig.voicesPath]:
     *   - VITS (piper-based or lexicon-based): set voicesPath = null
     *   - Kokoro (StyleTTS2, best quality):    set voicesPath = path to voices.bin
     *
     * @param modelPath  Absolute path to .onnx voice model
     * @param tokensPath Absolute path to tokens.txt
     * @param config     Optional configuration parameters
     * @return true if initialization succeeded
     */
    fun initTts(
        modelPath: String,
        tokensPath: String,
        config: TtsConfig = TtsConfig()
    ): Boolean

    /**
     * Synthesize text to audio samples.
     *
     * @param text Text to synthesize
     * @return PCM audio samples (16-bit signed, 22050Hz, mono)
     */
    fun synthesize(text: String): ShortArray

    /**
     * Synthesize text directly to a WAV file.
     *
     * @param text Text to synthesize
     * @param outputPath Path for output WAV file
     * @return true if file was written successfully
     */
    fun synthesizeToFile(text: String, outputPath: String): Boolean

    /**
     * Stream synthesis with audio chunk callbacks.
     *
     * @param text Text to synthesize
     * @param callback Callbacks for audio chunks
     */
    fun synthesizeStream(text: String, callback: TtsStream)

    /**
     * Cancel ongoing synthesis.
     */
    fun cancelTts()

    /**
     * Release TTS resources and unload model.
     */
    fun shutdownTts()

    // ══════════════════════════════════════════════════════════════
    //                         UTILITIES
    // ══════════════════════════════════════════════════════════════

    /**
     * Get model path, extracting from assets on Android if needed.
     *
     * @param modelFileName Name of model file
     * @return Absolute path to model file
     */
    @Composable
    fun getModelPath(modelFileName: String): String

    /**
     * Shutdown VAD, STT, and TTS — releasing all resources.
     */
    fun shutdown()
}
