package dev.deviceai.models

private const val HF = "https://huggingface.co"

/**
 * Hardcoded catalog of sherpa-onnx TTS voices available for download.
 *
 * All models are hosted on HuggingFace and can be downloaded as individual files
 * (no tar.bz2 extraction required).
 *
 * Kokoro models: model.onnx + tokens.txt + voices.bin
 * VITS models:   model.onnx + tokens.txt
 *
 * New voices can be added here without any other changes — the [TtsDownloadStrategy]
 * and [ModelRegistry.getTtsVoices] will automatically pick them up.
 */
internal object TtsCatalog {

    fun getVoices(): List<TtsVoiceInfo> = voices

    /** Voices distributed as sherpa-onnx tarballs — the loadable-by-construction path. */
    fun getTarballVoices(languageCode: String? = null): List<TtsTarballInfo> =
        if (languageCode == null) tarballVoices else tarballVoices.filter { it.languageCode == languageCode }

    private const val SHERPA_TTS = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models"

    // Sizes and hashes measured 2026-09-10 from the release assets.
    // Pi 4 @ 4 threads: Piper medium voices run at RTF ~0.36 (sherpa docs) —
    // ~3x faster than real time on edge CPUs, 61 MB. Kokoro (330 MB, RTF 2.77)
    // is deliberately not offered here.
    private val tarballVoices: List<TtsTarballInfo> = listOf(
        TtsTarballInfo(
            id            = "vits-piper-en_US-lessac-medium",
            displayName   = "Piper Lessac (US English, medium)",
            sizeBytes     = 67_230_653L,
            languageCode  = "en",
            modelType     = TtsModelType.VITS,
            archiveUrl    = "$SHERPA_TTS/vits-piper-en_US-lessac-medium.tar.bz2",
            archiveSha256 = "9e3febfacf0abf4270172d2958bcec246032b7e88efc2720840cc80c93de334e",
            topDir        = "vits-piper-en_US-lessac-medium",
            modelFile     = "en_US-lessac-medium.onnx",
            sampleRate    = 22_050,
        ),
        TtsTarballInfo(
            id            = "vits-piper-en_GB-alan-medium",
            displayName   = "Piper Alan (British English, medium)",
            sizeBytes     = 67_220_121L,
            languageCode  = "en",
            modelType     = TtsModelType.VITS,
            archiveUrl    = "$SHERPA_TTS/vits-piper-en_GB-alan-medium.tar.bz2",
            archiveSha256 = "a48d4017da0f77668b27bed63fe6e04dd64c6397e1fadad4f460efb0ef7c9012",
            topDir        = "vits-piper-en_GB-alan-medium",
            modelFile     = "en_GB-alan-medium.onnx",
            sampleRate    = 22_050,
        ),
    )

    fun getVoices(languageCode: String): List<TtsVoiceInfo> =
        voices.filter { it.languageCode == languageCode }

    private val voices: List<TtsVoiceInfo> = listOf(

        // ── Kokoro English (multi-speaker, 54 voices) ──────────────────────
        TtsVoiceInfo(
            id           = "kokoro-en-v0_19",
            displayName  = "Kokoro English (54 voices)",
            sizeBytes    = 305_000_000L,
            languageCode = "en",
            modelType    = TtsModelType.KOKORO,
            modelUrl     = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/kokoro-en-v0_19.onnx",
            tokensUrl    = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/tokens.txt",
            voicesUrl    = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/voices.bin",
            numSpeakers  = 54
        ),

        // ── Kokoro English int8 (smaller, faster) ──────────────────────────
        TtsVoiceInfo(
            id           = "kokoro-en-v0_19-int8",
            displayName  = "Kokoro English int8 (54 voices, faster)",
            sizeBytes    = 92_000_000L,
            languageCode = "en",
            modelType    = TtsModelType.KOKORO,
            modelUrl     = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/kokoro-en-v0_19.int8.onnx",
            tokensUrl    = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/tokens.txt",
            voicesUrl    = "$HF/csukuangfj/kokoro-en-v0_19/resolve/main/voices.bin",
            numSpeakers  = 54
        ),

        // ── VITS Chinese (no espeak-ng needed) ────────────────────────────
        TtsVoiceInfo(
            id           = "vits-zh-aishell3",
            displayName  = "VITS Chinese AiShell3 (174 speakers)",
            sizeBytes    = 117_000_000L,
            languageCode = "zh",
            modelType    = TtsModelType.VITS,
            modelUrl     = "$HF/csukuangfj/vits-zh-aishell3/resolve/main/vits-aishell3.onnx",
            tokensUrl    = "$HF/csukuangfj/vits-zh-aishell3/resolve/main/tokens.txt",
            numSpeakers  = 174
        ),
    )
}
