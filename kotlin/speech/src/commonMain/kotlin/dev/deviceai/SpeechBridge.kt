package dev.deviceai

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
expect object SpeechBridge {

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
     * @param modelPath Absolute path to .onnx model file
     * @param tokensPath Absolute path to tokens.txt
     * @param config Optional configuration parameters
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
     * Output sample rate of the loaded TTS voice in Hz — 22050 for Piper/VITS,
     * 24000 for Kokoro. Configure AudioTrack from this; assuming a rate plays
     * the audio pitched and sped by the ratio. Returns 0 if no voice is loaded.
     */
    fun ttsSampleRate(): Int

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
     * Shutdown both STT and TTS, releasing all resources.
     */
    fun shutdown()
}
