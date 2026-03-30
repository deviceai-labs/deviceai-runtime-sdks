package dev.deviceai

/**
 * Configuration for speech-to-text.
 */
data class SttConfig(
    /**
     * Language code (ISO 639-1). Examples: "en", "es", "fr", "de", "zh"
     * Use "auto" for automatic language detection.
     */
    val language: String = "en",

    /**
     * If true, translate non-English speech to English.
     */
    val translateToEnglish: Boolean = false,

    /**
     * Number of CPU threads for inference.
     */
    val maxThreads: Int = 4,

    /**
     * Use GPU acceleration if available (Metal on iOS/macOS).
     */
    val useGpu: Boolean = true,

    /**
     * Enable voice activity detection to skip silence.
     */
    val useVad: Boolean = true,

    /**
     * Force output into a single segment, skipping subtitle-style timestamp
     * boundary detection. Set to true for interactive voice commands.
     * Set to false if you need timestamped segments (e.g. podcast transcription).
     *
     * Requires VAD (useVad = true) to trim trailing silence first — otherwise
     * the decoder loops on silence and repeats the transcription.
     */
    val singleSegment: Boolean = true,

    /**
     * Do not use the previous transcription as a prompt for the decoder.
     * Set to true for isolated voice commands (each recording is independent).
     * Set to false for continuous transcription of a long audio stream.
     */
    val noContext: Boolean = true
) {
    init {
        require(language.matches(Regex("[a-z]{2,3}|auto"))) {
            "language must be an ISO 639-1/2 code (e.g. \"en\", \"es\") or \"auto\", got: \"$language\""
        }
    }
}
