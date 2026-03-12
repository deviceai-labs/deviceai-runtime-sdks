package dev.deviceai

import androidx.compose.runtime.Composable
import dev.deviceai.native.*
import kotlinx.cinterop.*
import platform.Foundation.*

/**
 * iOS actual implementation of [SpeechBridge].
 *
 * Calls the unified dai_stt_* / dai_tts_* C API in deviceai_speech_engine directly
 * via cinterop — no intermediate C++ wrapper file.
 */
@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalForeignApi::class)
actual object SpeechBridge {

    // ══════════════════════════════════════════════════════════════
    //              VOICE ACTIVITY DETECTION (VAD)
    // ══════════════════════════════════════════════════════════════

    actual fun initVad(modelPath: String, config: VadConfig): Boolean =
        dai_vad_init(modelPath, config.threshold, config.sampleRate) != 0

    actual fun isSpeech(samples: FloatArray): Boolean {
        memScoped {
            val nativeSamples = allocArray<FloatVar>(samples.size)
            samples.forEachIndexed { i, v -> nativeSamples[i] = v }
            return dai_vad_is_speech(nativeSamples, samples.size) != 0
        }
    }

    actual fun processVadStream(samples: FloatArray, callback: VadCallback) {
        memScoped {
            val nativeSamples = allocArray<FloatVar>(samples.size)
            samples.forEachIndexed { i, v -> nativeSamples[i] = v }

            val ref = StableRef.create(callback)

            val onStart = staticCFunction { user: COpaquePointer? ->
                user!!.asStableRef<VadCallback>().get().onSpeechStart()
            }
            val onEnd = staticCFunction { user: COpaquePointer? ->
                user!!.asStableRef<VadCallback>().get().onSpeechEnd()
            }

            dai_vad_process_stream(nativeSamples, samples.size, onStart, onEnd, ref.asCPointer())
            ref.dispose()
        }
    }

    actual fun resetVad() = dai_vad_reset()

    actual fun shutdownVad() = dai_vad_shutdown()

    // ══════════════════════════════════════════════════════════════
    //                    SPEECH-TO-TEXT (STT)
    // ══════════════════════════════════════════════════════════════

    actual fun initStt(modelPath: String, config: SttConfig): Boolean {
        return dai_stt_init(
            modelPath,
            config.language,
            if (config.translateToEnglish) 1 else 0,
            config.maxThreads,
            if (config.useGpu) 1 else 0,
            if (config.useVad) 1 else 0,
            if (config.singleSegment) 1 else 0,
            if (config.noContext) 1 else 0
        ) != 0
    }

    actual fun transcribe(audioPath: String): String {
        val result = dai_stt_transcribe_file(audioPath)
        return result?.toKString()?.also { dai_speech_free_string(result) } ?: ""
    }

    actual fun transcribeDetailed(audioPath: String): TranscriptionResult {
        val jsonResult = dai_stt_transcribe_file_detailed(audioPath)
        val jsonStr = jsonResult?.toKString()?.also { dai_speech_free_string(jsonResult) } ?: "{}"
        return TranscriptionJsonParser.parse(jsonStr)
    }

    actual fun transcribeAudio(samples: FloatArray): String {
        memScoped {
            val nativeSamples = allocArray<FloatVar>(samples.size)
            samples.forEachIndexed { index, value -> nativeSamples[index] = value }
            val result = dai_stt_transcribe(nativeSamples, samples.size)
            return result?.toKString()?.also { dai_speech_free_string(result) } ?: ""
        }
    }

    actual fun transcribeStream(samples: FloatArray, callback: SttStream) {
        memScoped {
            val nativeSamples = allocArray<FloatVar>(samples.size)
            samples.forEachIndexed { index, value -> nativeSamples[index] = value }

            val ref = StableRef.create(callback)

            val onPartial = staticCFunction { text: CPointer<ByteVar>?, userData: COpaquePointer? ->
                val cb = userData!!.asStableRef<SttStream>().get()
                cb.onPartialResult(text?.toKString() ?: "")
            }

            val onFinal = staticCFunction { jsonResult: CPointer<ByteVar>?, userData: COpaquePointer? ->
                val cb = userData!!.asStableRef<SttStream>().get()
                cb.onFinalResult(
                    TranscriptionJsonParser.parse(jsonResult?.toKString() ?: "{}")
                )
            }

            val onError = staticCFunction { message: CPointer<ByteVar>?, userData: COpaquePointer? ->
                val cb = userData!!.asStableRef<SttStream>().get()
                cb.onError(message?.toKString() ?: "Unknown error")
            }

            dai_stt_transcribe_stream(
                nativeSamples, samples.size,
                onPartial, onFinal, onError,
                ref.asCPointer()
            )

            ref.dispose()
        }
    }

    actual fun cancelStt() = dai_stt_cancel()

    actual fun shutdownStt() = dai_stt_shutdown()

    // ══════════════════════════════════════════════════════════════
    //                    TEXT-TO-SPEECH (TTS)
    // ══════════════════════════════════════════════════════════════

    actual fun initTts(modelPath: String, tokensPath: String, config: TtsConfig): Boolean {
        return dai_tts_init(
            modelPath,
            tokensPath,
            config.espeakDataPath ?: "",
            config.voicesPath ?: "",
            config.speakerId ?: 0,
            config.speechRate,
            config.sampleRate,
            config.sentenceSilence
        ) != 0
    }

    actual fun synthesize(text: String): ShortArray {
        memScoped {
            val outLength = alloc<IntVar>()
            val result = dai_tts_synthesize(text, outLength.ptr)
            if (result == null) return shortArrayOf()
            val samples = ShortArray(outLength.value) { result[it] }
            dai_speech_free_audio(result)
            return samples
        }
    }

    actual fun synthesizeToFile(text: String, outputPath: String): Boolean =
        dai_tts_synthesize_to_file(text, outputPath) != 0

    actual fun synthesizeStream(text: String, callback: TtsStream) {
        val ref = StableRef.create(callback)

        val onChunk = staticCFunction { samples: CPointer<ShortVar>?, nSamples: Int, userData: COpaquePointer? ->
            val cb = userData!!.asStableRef<TtsStream>().get()
            if (samples != null && nSamples > 0) {
                cb.onAudioChunk(ShortArray(nSamples) { samples[it] })
            }
        }

        val onComplete = staticCFunction { userData: COpaquePointer? ->
            userData!!.asStableRef<TtsStream>().get().onComplete()
        }

        val onError = staticCFunction { message: CPointer<ByteVar>?, userData: COpaquePointer? ->
            val cb = userData!!.asStableRef<TtsStream>().get()
            cb.onError(message?.toKString() ?: "Unknown error")
        }

        dai_tts_synthesize_stream(text, onChunk, onComplete, onError, ref.asCPointer())
        ref.dispose()
    }

    actual fun cancelTts() = dai_tts_cancel()

    actual fun shutdownTts() = dai_tts_shutdown()

    // ══════════════════════════════════════════════════════════════
    //                         UTILITIES
    // ══════════════════════════════════════════════════════════════

    @Composable
    actual fun getModelPath(modelFileName: String): String {
        val bundle = NSBundle.mainBundle
        val resourcePath = bundle.pathForResource(
            modelFileName.substringBeforeLast("."),
            modelFileName.substringAfterLast(".")
        )
        if (resourcePath != null) return resourcePath

        val fileManager = NSFileManager.defaultManager

        @Suppress("UNCHECKED_CAST")
        val documentsUrl = (fileManager.URLsForDirectory(NSDocumentDirectory, NSUserDomainMask) as List<NSURL>)
            .firstOrNull()
        if (documentsUrl != null) {
            val filePath = documentsUrl.path + "/$modelFileName"
            if (fileManager.fileExistsAtPath(filePath)) return filePath
        }

        @Suppress("UNCHECKED_CAST")
        val cacheUrl = (fileManager.URLsForDirectory(NSCachesDirectory, NSUserDomainMask) as List<NSURL>)
            .firstOrNull()
        if (cacheUrl != null) {
            val cachePath = cacheUrl.path + "/$modelFileName"
            if (fileManager.fileExistsAtPath(cachePath)) return cachePath
        }

        return modelFileName
    }

    actual fun shutdown() = dai_speech_shutdown_all()  // internally calls dai_vad_shutdown too
}
