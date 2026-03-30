package dev.deviceai.llm

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.onEach

/**
 * A stateful LLM conversation session.
 *
 * Create via [dev.deviceai.core.DeviceAI.llm]:
 * ```kotlin
 * val session = DeviceAI.llm.chat("/path/to/model.gguf") {
 *     systemPrompt = "You are a helpful assistant."
 *     temperature  = 0.8f
 * }
 * ```
 *
 * ## Streaming (recommended for UI)
 * ```kotlin
 * session.send("What is Kotlin?").collect { token -> print(token) }
 * ```
 *
 * ## Blocking (scripts / tests)
 * ```kotlin
 * val reply = session.sendBlocking("What is Kotlin?")
 * ```
 *
 * ## Multi-turn — history is automatic
 * ```kotlin
 * session.send("What is Kotlin?").collect { print(it) }
 * session.send("Give me an example.").collect { print(it) }  // remembers context
 * ```
 *
 * ## Lifecycle
 * ```kotlin
 * session.cancel()        // abort in-progress generation
 * session.clearHistory()  // start a fresh conversation, keep the model loaded
 * session.close()         // unload the model and free all resources
 * ```
 */
class ChatSession internal constructor(
    modelPath: String,
    private val config: ChatConfig,
) {
    /** `true` if the model loaded successfully and the session is ready for inference. */
    val isReady: Boolean = LlmCppBridge.initLlm(modelPath, config.toInitConfig())

    private val _history = mutableListOf<LlmMessage>()

    /**
     * Read-only conversation history as clean [ChatTurn] pairs.
     * Only includes completed exchanges (user + assistant).
     * Excludes in-progress streaming turns.
     */
    val history: List<ChatTurn>
        get() {
            val turns = mutableListOf<ChatTurn>()
            var i = 0
            while (i + 1 < _history.size) {
                val msg = _history[i]
                val reply = _history[i + 1]
                if (msg.role == LlmRole.USER && reply.role == LlmRole.ASSISTANT) {
                    turns.add(ChatTurn(user = msg.content, assistant = reply.content))
                }
                i += 2
            }
            return turns
        }

    /**
     * Send a user message and receive a streaming response token by token.
     *
     * Conversation history is updated automatically:
     * - The user message is added immediately.
     * - The assistant reply is appended once the flow completes.
     * - If the flow errors, the user message is rolled back so the caller can retry.
     *
     * Override [config] for this request only without affecting the session default:
     * ```kotlin
     * session.send("Tell me a joke.", overrideConfig = ChatConfig().apply { temperature = 1.2f })
     * ```
     *
     * @param text           The user's message.
     * @param overrideConfig Per-request config override. Null = use session default.
     * @return [Flow] emitting token strings as they are generated.
     */
    fun send(text: String, overrideConfig: ChatConfig? = null): Flow<String> {
        require(text.isNotBlank()) { "Message text must not be blank" }

        _history.add(LlmMessage(LlmRole.USER, text))

        val messages = buildList {
            add(LlmMessage(LlmRole.SYSTEM, config.systemPrompt))
            addAll(_history)
        }

        val genConfig = (overrideConfig ?: config).toGenConfig()
        val reply = StringBuilder()

        return LlmCppBridge.generateStream(messages, genConfig)
            .onEach { token -> reply.append(token) }
            .onCompletion { error ->
                if (error == null) {
                    if (reply.isNotEmpty()) {
                        _history.add(LlmMessage(LlmRole.ASSISTANT, reply.toString()))
                    } else {
                        // Empty completion — roll back user message to keep history consistent
                        _history.removeLastOrNull()
                    }
                } else {
                    _history.removeLastOrNull() // roll back user message for clean retry
                }
            }
    }

    /**
     * Send a user message and block until the full response is available.
     *
     * Prefer [send] for streaming UIs. Use this for scripts or tests.
     *
     * @param text           The user's message.
     * @param overrideConfig Per-request config override. Null = use session default.
     * @return The complete assistant response.
     */
    fun sendBlocking(text: String, overrideConfig: ChatConfig? = null): String {
        require(text.isNotBlank()) { "Message text must not be blank" }
        _history.add(LlmMessage(LlmRole.USER, text))

        val messages = buildList {
            add(LlmMessage(LlmRole.SYSTEM, config.systemPrompt))
            addAll(_history)
        }

        val genConfig = (overrideConfig ?: config).toGenConfig()
        return try {
            val result = LlmCppBridge.generate(messages, genConfig)
            _history.add(LlmMessage(LlmRole.ASSISTANT, result.text))
            result.text
        } catch (e: Exception) {
            _history.removeLastOrNull() // roll back user message for clean retry
            throw e
        }
    }

    /** Abort any in-progress [send] or [sendBlocking] call. */
    fun cancel() = LlmCppBridge.cancelGeneration()

    /** Clear conversation history. The model stays loaded and the session remains usable. */
    fun clearHistory() = _history.clear()

    /** Unload the model and release all engine resources. Do not use the session after this. */
    fun close() = LlmCppBridge.shutdown()
}
