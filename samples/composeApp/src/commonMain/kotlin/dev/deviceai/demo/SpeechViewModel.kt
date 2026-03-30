package dev.deviceai.demo

import dev.deviceai.SpeechBridge
import dev.deviceai.SttConfig
import dev.deviceai.SttStream
import dev.deviceai.TranscriptionResult
import dev.deviceai.models.DownloadProgress
import dev.deviceai.models.ModelRegistry
import dev.deviceai.models.WhisperSize
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.IO
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private const val TARGET_MODEL_ID = "ggml-tiny.en.bin"

// ── Loading / init state ──────────────────────────────────────────────────────

sealed class LoadingState {
    object Initializing : LoadingState()
    data class Downloading(val progress: DownloadProgress) : LoadingState()
    object Ready : LoadingState()
    data class Error(val message: String) : LoadingState()
}

// ── Recording / transcription state ──────────────────────────────────────────

sealed class RecordingState {
    object Idle : RecordingState()
    object Recording : RecordingState()
    object Transcribing : RecordingState()
    data class Result(val text: String) : RecordingState()
    data class Error(val message: String) : RecordingState()
}

// ── ViewModel (Koin single — survives across screens) ─────────────────────────

class SpeechViewModel(private val audioRecorder: AudioRecorder) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _loadingState = MutableStateFlow<LoadingState>(LoadingState.Initializing)
    val loadingState: StateFlow<LoadingState> = _loadingState.asStateFlow()

    private val _recordingState = MutableStateFlow<RecordingState>(RecordingState.Idle)
    val recordingState: StateFlow<RecordingState> = _recordingState.asStateFlow()

    /**
     * Kick off model discovery + download then initialize the STT engine.
     * Safe to call multiple times — no-ops if already Ready.
     * [platformInit] runs first on IO; Android uses it for PlatformStorage.initialize().
     */
    fun initialize(platformInit: () -> Unit = {}) {
        if (_loadingState.value is LoadingState.Ready) return

        scope.launch {
            withContext(Dispatchers.IO) { platformInit() }

            ModelRegistry.initialize()

            // Already downloaded?
            val existing = ModelRegistry.getLocalModel(TARGET_MODEL_ID)
            if (existing != null) {
                onModelReady(existing.modelPath)
                return@launch
            }

            // Discover from HuggingFace catalog
            val models = try {
                ModelRegistry.getWhisperModels()
            } catch (e: Exception) {
                _loadingState.value = LoadingState.Error("Cannot reach model server: ${e.message}")
                return@launch
            }

            val target = models.firstOrNull { it.id == TARGET_MODEL_ID }
                ?: models.firstOrNull { it.size == WhisperSize.TINY && it.isEnglishOnly }

            if (target == null) {
                _loadingState.value = LoadingState.Error("Whisper Tiny model not found in catalog.")
                return@launch
            }

            val result = ModelRegistry.download(target) { progress ->
                _loadingState.value = LoadingState.Downloading(progress)
            }

            result.fold(
                onSuccess = { local -> onModelReady(local.modelPath) },
                onFailure = { e -> _loadingState.value = LoadingState.Error("Download failed: ${e.message}") }
            )
        }
    }

    fun retryInitialize(platformInit: () -> Unit = {}) {
        _loadingState.value = LoadingState.Initializing
        initialize(platformInit)
    }

    // ── Recording ─────────────────────────────────────────────────────────────

    fun onMicButtonClicked() {
        when (_recordingState.value) {
            is RecordingState.Idle,
            is RecordingState.Result,
            is RecordingState.Error -> startRecording()
            is RecordingState.Recording -> stopAndTranscribe()
            is RecordingState.Transcribing -> Unit // ignore taps while processing
        }
    }

    fun resetRecording() {
        _recordingState.value = RecordingState.Idle
    }

    private fun startRecording() {
        _recordingState.value = RecordingState.Recording
        audioRecorder.startRecording()
    }

    private fun stopAndTranscribe() {
        scope.launch {
            val samples = withContext(Dispatchers.IO) { audioRecorder.stopRecording() }

            if (samples.isEmpty()) {
                _recordingState.value = RecordingState.Error("No audio captured — check microphone permission.")
                return@launch
            }
            _recordingState.value = RecordingState.Transcribing

            withContext(Dispatchers.IO) {
                SpeechBridge.transcribeStream(samples, object : SttStream {
                    override fun onPartialResult(text: String) {}
                    override fun onFinalResult(result: TranscriptionResult) {
                        val incoming = result.text.trim()
                        _recordingState.value = if (incoming.isNotBlank()) RecordingState.Result(incoming)
                                                else RecordingState.Result("(no speech detected)")
                    }
                    override fun onError(message: String) {
                        _recordingState.value = RecordingState.Error(message)
                    }
                })
            }
        }
    }

    // ── Private ───────────────────────────────────────────────────────────────

    private fun onModelReady(modelPath: String) {
        val ok = SpeechBridge.initStt(modelPath, SttConfig(language = "en"))
        _loadingState.value = if (ok) LoadingState.Ready
                              else LoadingState.Error("Failed to initialize STT engine.")
    }

    fun dispose() {
        scope.cancel()
        SpeechBridge.shutdownStt()
    }
}
