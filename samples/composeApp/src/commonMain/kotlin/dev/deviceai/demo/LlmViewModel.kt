package dev.deviceai.demo

import dev.deviceai.core.DeviceAI
import dev.deviceai.llm.ChatSession
import dev.deviceai.llm.llm
import dev.deviceai.models.currentTimeMillis
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
    object NotAvailable : LlmState()
    object Loading : LlmState()
    object Ready : LlmState()
    data class Error(val msg: String) : LlmState()
}

// ── Chat message model ────────────────────────────────────────────────────────

enum class Role { USER, ASSISTANT }

data class ChatMessage(
    val role: Role,
    val text: String,
    val isStreaming: Boolean = false,
    val id: Long = 0L,
    val timestampMs: Long = 0L,
    val tokenCount: Int = 0
)

// ── ViewModel ─────────────────────────────────────────────────────────────────

class LlmViewModel {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _state = MutableStateFlow<LlmState>(LlmState.NotAvailable)
    val state: StateFlow<LlmState> = _state.asStateFlow()

    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages.asStateFlow()

    private val _isGenerating = MutableStateFlow(false)
    val isGenerating: StateFlow<Boolean> = _isGenerating.asStateFlow()

    private val _tokensPerSec = MutableStateFlow(0f)
    val tokensPerSec: StateFlow<Float> = _tokensPerSec.asStateFlow()

    private val _latencyMs = MutableStateFlow(0L)
    val latencyMs: StateFlow<Long> = _latencyMs.asStateFlow()

    private var session: ChatSession? = null
    private var nextId = 0L
    private fun nextMessageId() = nextId++

    // ── Public API ────────────────────────────────────────────────────────────

    fun initialize(modelPath: String?) {
        if (modelPath == null) {
            _state.value = LlmState.NotAvailable
            return
        }
        if (_state.value is LlmState.Loading || _state.value is LlmState.Ready) return
        loadModel(modelPath)
    }

    fun loadModel(path: String) {
        scope.launch {
            _state.value = LlmState.Loading
            val newSession = runCatching {
                withContext(Dispatchers.IO) { DeviceAI.llm.chat(path) }
            }.getOrNull()

            if (newSession?.isReady == true) {
                session?.close()
                session = newSession
                _state.value = LlmState.Ready
            } else {
                newSession?.close()
                _state.value = LlmState.Error("Failed to load model. Check the file path.")
            }
        }
    }

    fun sendMessage(text: String) {
        val activeSession = session ?: return
        if (_isGenerating.value || text.isBlank()) return

        val nowMs = currentTimeMillis()
        val userMsg = ChatMessage(
            role = Role.USER, text = text,
            id = nextMessageId(), timestampMs = nowMs
        )
        val assistantMsg = ChatMessage(
            role = Role.ASSISTANT, text = "", isStreaming = true,
            id = nextMessageId(), timestampMs = nowMs
        )
        _messages.value = _messages.value + userMsg + assistantMsg
        _isGenerating.value = true
        _tokensPerSec.value = 0f
        _latencyMs.value = 0L

        val genStartMs = currentTimeMillis()
        var tokenCount = 0
        var firstTokenMs = -1L

        activeSession.send(text)
            .onEach { token ->
                tokenCount++
                val elapsedMs = currentTimeMillis() - genStartMs

                if (firstTokenMs < 0L) {
                    firstTokenMs = elapsedMs
                    _latencyMs.value = firstTokenMs
                }

                val elapsedSec = elapsedMs / 1000f
                if (elapsedSec > 0f) _tokensPerSec.value = tokenCount / elapsedSec

                val current = _messages.value.toMutableList()
                val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
                if (idx >= 0) {
                    current[idx] = current[idx].copy(
                        text = current[idx].text + token,
                        tokenCount = tokenCount
                    )
                    _messages.value = current
                }
            }
            .onCompletion { markStreamingComplete() }
            .catch { e -> markError(e.message ?: "Unknown error") }
            .launchIn(scope)
    }

    fun cancelGeneration() {
        session?.cancel()
        val current = _messages.value.toMutableList()
        val idx = current.indexOfLast { it.role == Role.ASSISTANT && it.isStreaming }
        if (idx >= 0) {
            current[idx] = current[idx].copy(isStreaming = false)
            _messages.value = current
        }
        _isGenerating.value = false
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
