package dev.deviceai.models

import kotlinx.serialization.Serializable

/**
 * Whisper model size tiers.
 */
enum class WhisperSize(val label: String) {
    TINY("tiny"),
    BASE("base"),
    SMALL("small"),
    MEDIUM("medium"),
    LARGE_V1("large-v1"),
    LARGE_V2("large-v2"),
    LARGE_V3("large-v3"),
    LARGE_V3_TURBO("large-v3-turbo");

    companion object {
        fun fromString(s: String): WhisperSize? = entries.find { it.label == s }
    }
}

/**
 * Quantization formats available for Whisper models.
 */
enum class Quantization(val label: String) {
    Q5_1("q5_1"),
    Q8_0("q8_0");

    companion object {
        fun fromString(s: String): Quantization? = entries.find { it.label == s }
    }
}

/**
 * Piper voice quality tiers.
 */
enum class PiperQuality(val label: String) {
    X_LOW("x_low"),
    LOW("low"),
    MEDIUM("medium"),
    HIGH("high");

    companion object {
        fun fromString(s: String): PiperQuality? = entries.find { it.label == s }
    }
}

/**
 * Language metadata for Piper voices.
 */
@Serializable
data class LanguageInfo(
    val code: String,
    val family: String,
    val region: String,
    val nameNative: String,
    val nameEnglish: String,
    val countryEnglish: String
)

/**
 * Whisper GGML model available on HuggingFace.
 * Implements [ModelInfo] from runtime-core.
 */
data class WhisperModelInfo(
    override val id: String,
    override val displayName: String,
    override val sizeBytes: Long,
    val size: WhisperSize,
    val isEnglishOnly: Boolean,
    val quantization: Quantization?,
    val downloadUrl: String
) : ModelInfo

/**
 * Piper ONNX voice model available on HuggingFace.
 * Implements [ModelInfo] from runtime-core.
 */
data class PiperVoiceInfo(
    override val id: String,
    override val displayName: String,
    override val sizeBytes: Long,
    val language: LanguageInfo,
    val quality: PiperQuality,
    val numSpeakers: Int,
    val speakerIdMap: Map<String, Int>,
    val modelUrl: String,
    val configUrl: String
) : ModelInfo

/**
 * sherpa-onnx TTS voice model (VITS or Kokoro).
 *
 * Kokoro models need only [modelUrl] + [tokensUrl] + [voicesUrl] (no espeak-ng-data).
 * VITS models that use espeak phonemization also need [dataDirZipUrl] for the espeak-ng-data directory.
 */
data class TtsVoiceInfo(
    override val id: String,
    override val displayName: String,
    override val sizeBytes: Long,
    /** ISO 639-1 language code, e.g. "en", "zh". */
    val languageCode: String,
    val modelType: TtsModelType,
    /** Direct URL to the ONNX model file. */
    val modelUrl: String,
    /** Direct URL to tokens.txt. */
    val tokensUrl: String,
    /** URL to voices.bin — required for Kokoro, null for VITS. */
    val voicesUrl: String? = null,
    /** Number of speaker IDs supported by this voice model. */
    val numSpeakers: Int = 1
) : ModelInfo

/**
 * A sherpa-onnx TTS voice distributed as a single .tar.bz2 from the sherpa-onnx
 * `tts-models` release. The tarball is the artifact the engine is built and
 * tested against: it carries the ONNX model *with* the metadata sherpa's loader
 * reads (sample_rate, n_speakers…), `tokens.txt`, and `espeak-ng-data/`.
 * Individual upstream files (e.g. rhasspy/piper-voices) lack all three.
 *
 * @param archiveUrl   The .tar.bz2 URL.
 * @param archiveSha256 Lowercase hex SHA-256 of the archive; verified before extraction.
 * @param topDir       Directory name at the root of the tarball (e.g. "vits-piper-en_US-lessac-medium").
 * @param modelFile    Model filename inside [topDir].
 * @param sampleRate   Output sample rate the voice produces, for playback configuration.
 */
data class TtsTarballInfo(
    override val id: String,
    override val displayName: String,
    override val sizeBytes: Long,
    val languageCode: String,
    val modelType: TtsModelType,
    val archiveUrl: String,
    val archiveSha256: String,
    val topDir: String,
    val modelFile: String,
    val sampleRate: Int,
    val numSpeakers: Int = 1,
) : ModelInfo

/**
 * Distinguishes sherpa-onnx TTS model architectures.
 * - [KOKORO]: needs model.onnx + tokens.txt + voices.bin; no espeak-ng-data required.
 * - [VITS]: needs model.onnx + tokens.txt; may also need espeak-ng-data for English phonemization.
 */
enum class TtsModelType { VITS, KOKORO }

/**
 * Returns the absolute path to voices.bin for a downloaded Kokoro TTS model,
 * or an empty string if this is a VITS model that does not need voices.bin.
 *
 * Usage:
 * ```kotlin
 * val local = ModelRegistry.download(voice).getOrThrow()
 * SpeechBridge.initTts(
 *     modelPath  = local.modelPath,
 *     tokensPath = local.configPath!!,
 *     config     = TtsConfig(voicesPath = local.ttsVoicesPath())
 * )
 * ```
 */
fun LocalModel.ttsVoicesPath(): String {
    if (modelType != LocalModelType.TTS) return ""
    val candidate = "${modelPath.substringBeforeLast('/')}/voices.bin"
    // Only return a path if voices.bin was actually downloaded (Kokoro models).
    // VITS models never download voices.bin; returning a non-empty path would
    // make the native layer treat them as Kokoro and fail at init.
    return if (PlatformStorage.fileExists(candidate)) candidate else ""
}

/**
 * Absolute path to the espeak-ng-data directory for a downloaded VITS/Piper
 * voice, or "" if the voice has none (Kokoro, or a voice that ships its own
 * lexicon). Pass to [TtsConfig.dataDir]. Derived by convention, like
 * [ttsVoicesPath]: the directory sits beside the model file.
 */
fun LocalModel.ttsDataDir(): String {
    if (modelType != LocalModelType.TTS) return ""
    val candidate = "${modelPath.substringBeforeLast('/')}/espeak-ng-data"
    return if (PlatformStorage.fileExists(candidate)) candidate else ""
}

// LocalModelType constants for runtime-speech model types.
// Defined here as companion extensions so runtime-core never needs to be modified
// when new modules are added — Open/Closed Principle.
val LocalModelType.Companion.WHISPER get() = LocalModelType("WHISPER")
val LocalModelType.Companion.PIPER   get() = LocalModelType("PIPER")
val LocalModelType.Companion.TTS     get() = LocalModelType("TTS")
