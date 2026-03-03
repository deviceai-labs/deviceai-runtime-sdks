package dev.deviceai.demo

import dev.deviceai.llm.LlmBridge
import dev.deviceai.llm.LlmGenConfig
import dev.deviceai.llm.LlmInitConfig
import dev.deviceai.llm.LlmMessage
import dev.deviceai.llm.LlmRole
import dev.deviceai.llm.models.LlmCatalog
import dev.deviceai.llm.models.LlmModelInfo
import dev.deviceai.models.DownloadProgress
import dev.deviceai.models.LocalModelType
import dev.deviceai.models.ModelRegistry
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.IO
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ── LLM loading state ─────────────────────────────────────────────────────────

sealed class LlmState {
    object NotAvailable : LlmState()                          // native library not compiled yet
    object Idle : LlmState()                                  // ready to start download / load
    data class Downloading(val progress: DownloadProgress) : LlmState()  // downloading model file
    object Loading : LlmState()                               // LlmBridge.initLlm() in progress
    object Ready : LlmState()                                 // model loaded, chat works
    data class Error(val msg: String) : LlmState()
}

// ── Chat message model ────────────────────────────────────────────────────────

enum class Role { USER, ASSISTANT }

data class ChatMessage(
    val role: Role,
    val text: String,
    val isStreaming: Boolean = false,
    val id: Long = 0L
)

// ── ViewModel ─────────────────────────────────────────────────────────────────

class LlmViewModel {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    // Check native availability once at init — catches UnsatisfiedLinkError (extends Throwable)
    private val nativeAvailable: Boolean =
        runCatching { LlmBridge.toString(); true }.getOrElse { false }

    private val _state = MutableStateFlow<LlmState>(
        if (nativeAvailable) LlmState.Idle else LlmState.NotAvailable
    )
    val state: StateFlow<LlmState> = _state.asStateFlow()

    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages.asStateFlow()

    private val _isGenerating = MutableStateFlow(false)
    val isGenerating: StateFlow<Boolean> = _isGenerating.asStateFlow()

    val suggestedModel: LlmModelInfo = LlmCatalog.SMOLLM2_360M_INSTRUCT_Q4

    private var nextId = 0L
    private fun nextMessageId() = nextId++

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Kick off model discovery → download (if needed) → load.
     * Mirrors how SpeechViewModel.initialize() works for Whisper.
     * Safe to call multiple times — no-ops unless in Idle state.
     */
    fun initialize() {
        if (_state.value !is LlmState.Idle) return

        scope.launch {
            withContext(Dispatchers.IO) { ModelRegistry.initialize() }

            // Already downloaded?
            val existing = ModelRegistry.getLocalModel(suggestedModel.id)
            if (existing != null) {
                loadModel(existing.modelPath)
                return@launch
            }

            // Download from HuggingFace
            val url = "https://huggingface.co/${suggestedModel.repoId}/resolve/main/${suggestedModel.filename}"
            val result = ModelRegistry.downloadRawFile(
                modelId   = suggestedModel.id,
                url       = url,
                modelType = LocalModelType("LLM"),
                onProgress = { _state.value = LlmState.Downloading(it) }
            )

            result.fold(
                onSuccess = { local -> loadModel(local.modelPath) },
                onFailure = { e   -> _state.value = LlmState.Error("Download failed: ${e.message}") }
            )
        }
    }

    fun loadModel(path: String) {
        scope.launch {
            _state.value = LlmState.Loading
            val ok = runCatching {
                withContext(Dispatchers.IO) { LlmBridge.initLlm(path, LlmInitConfig()) }
            }.getOrElse { false }
            _state.value = if (ok) LlmState.Ready
                           else LlmState.Error("Failed to load model. Check the file path.")
        }
    }

    fun sendMessage(text: String) {
        if (_isGenerating.value || text.isBlank()) return

        val userMsg = ChatMessage(role = Role.USER, text = text, id = nextMessageId())
        val assistantMsg = ChatMessage(
            role = Role.ASSISTANT, text = "", isStreaming = true, id = nextMessageId()
        )
        _messages.value = _messages.value + userMsg + assistantMsg
        _isGenerating.value = true

        // Build a clean LlmMessage list from conversation history up to and including
        // the new user message. No encoding hacks — native side receives structured arrays.
        val llmMessages = _messages.value
            .filter { it.id <= userMsg.id }
            .map { msg ->
                LlmMessage(
                    role = if (msg.role == Role.USER) LlmRole.USER else LlmRole.ASSISTANT,
                    content = msg.text
                )
            }

        LlmBridge.generateStream(llmMessages, LlmGenConfig())
            .onEach { token ->
                val current = _messages.value.toMutableList()
                val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
                if (idx >= 0) {
                    current[idx] = current[idx].copy(text = current[idx].text + token)
                    _messages.value = current
                }
            }
            .onCompletion { markStreamingComplete() }
            .catch { e -> markError(e.message ?: "Unknown error") }
            .launchIn(scope)
    }

    fun cancelGeneration() {
        runCatching { LlmBridge.cancelGeneration() }
        val current = _messages.value.toMutableList()
        val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
        if (idx >= 0) {
            current[idx] = current[idx].copy(isStreaming = false)
            _messages.value = current
        }
        _isGenerating.value = false
    }

    fun retry() {
        _state.value = LlmState.Idle
        initialize()
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    private fun markStreamingComplete() {
        val current = _messages.value.toMutableList()
        val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
        if (idx >= 0) {
            current[idx] = current[idx].copy(isStreaming = false)
            _messages.value = current
        }
        _isGenerating.value = false
    }

    private fun markError(message: String) {
        val current = _messages.value.toMutableList()
        val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
        if (idx >= 0) {
            current[idx] = current[idx].copy(text = "Error: $message", isStreaming = false)
            _messages.value = current
        }
        _isGenerating.value = false
    }
}
