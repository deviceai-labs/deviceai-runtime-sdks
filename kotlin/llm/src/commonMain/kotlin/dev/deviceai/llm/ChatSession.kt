package dev.deviceai.llm

import dev.deviceai.core.InternalDeviceAiApi
import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.core.telemetry.TelemetryEvent.Companion.ERR_UNKNOWN
import dev.deviceai.core.telemetry.TelemetryReporter
import dev.deviceai.models.currentTimeMillis
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.onEach
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

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
@OptIn(InternalDeviceAiApi::class, ExperimentalUuidApi::class)
class ChatSession internal constructor(
    modelPath: String,
    private val config: ChatConfig,
) {
    /** `true` if the model loaded successfully and the session is ready for inference. */
    val isReady: Boolean = LlmCppBridge.initLlm(modelPath, config.toInitConfig())

    // Model filename without path or extension — used as telemetry model ID.
    private val modelId: String = modelPath.substringAfterLast('/').substringBeforeLast('.')

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
        _history.add(LlmMessage(LlmRole.USER, text))

        val messages = buildList {
            add(LlmMessage(LlmRole.SYSTEM, config.systemPrompt))
            addAll(_history)
        }

        val genConfig = (overrideConfig ?: config).toGenConfig()
        val reply = StringBuilder()

        val startMs = currentTimeMillis()
        val genId = Uuid.random().toString()
        // outputPieces: count llama.cpp piece callbacks — each callback is one decoded piece,
        // which is the closest proxy to token count available without exposing the tokenizer.
        var outputPieces = 0

        return LlmCppBridge.generateStream(messages, genConfig)
            .onEach { token ->
                reply.append(token)
                outputPieces++
            }
            .onCompletion { error ->
                if (error == null && reply.isNotEmpty()) {
                    _history.add(LlmMessage(LlmRole.ASSISTANT, reply.toString()))
                } else if (error != null) {
                    _history.removeLastOrNull() // roll back user message for clean retry
                }
                val totalMs = currentTimeMillis() - startMs
                TelemetryReporter.record(TelemetryEvent.LlmInference(
                    generationId          = genId,
                    modelId               = modelId,
                    inputTokens           = estimateInputTokens(messages),
                    inputTokensEstimated  = true,   // exact count needs C++ tokenizer — Phase 2
                    outputPieces          = outputPieces,
                    outputTokens          = null,   // exact count needs C++ bridge — Phase 2
                    totalMs               = totalMs,
                    tokensPerSecond       = if (totalMs > 0) outputPieces * 1000f / totalMs else 0f,
                    success               = error == null,
                    errorCode             = error?.toErrorCode(),
                    timestampMs           = currentTimeMillis(),
                ))
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
        _history.add(LlmMessage(LlmRole.USER, text))

        val messages = buildList {
            add(LlmMessage(LlmRole.SYSTEM, config.systemPrompt))
            addAll(_history)
        }

        val genConfig = (overrideConfig ?: config).toGenConfig()
        val startMs = currentTimeMillis()

        return try {
            val result = LlmCppBridge.generate(messages, genConfig)
            _history.add(LlmMessage(LlmRole.ASSISTANT, result.text))
            TelemetryReporter.record(TelemetryEvent.LlmInference(
                generationId          = Uuid.random().toString(),
                modelId               = modelId,
                inputTokens           = estimateInputTokens(messages),
                inputTokensEstimated  = true,
                outputPieces          = result.tokenCount, // word-approx for blocking — no piece callbacks
                outputTokens          = null,
                totalMs               = result.generationTimeMs,
                tokensPerSecond       = if (result.generationTimeMs > 0)
                    result.tokenCount * 1000f / result.generationTimeMs else 0f,
                success               = true,
                errorCode             = null,
                timestampMs           = currentTimeMillis(),
            ))
            result.text
        } catch (e: Exception) {
            _history.removeLastOrNull() // roll back user message for clean retry
            TelemetryReporter.record(TelemetryEvent.LlmInference(
                generationId          = Uuid.random().toString(),
                modelId               = modelId,
                inputTokens           = estimateInputTokens(messages),
                inputTokensEstimated  = true,
                outputPieces          = 0,
                outputTokens          = null,
                totalMs               = currentTimeMillis() - startMs,
                tokensPerSecond       = 0f,
                success               = false,
                errorCode             = e.toErrorCode(),
                timestampMs           = currentTimeMillis(),
            ))
            throw e
        }
    }

    /** Abort any in-progress [send] or [sendBlocking] call. */
    fun cancel() = LlmCppBridge.cancelGeneration()

    /** Clear conversation history. The model stays loaded and the session remains usable. */
    fun clearHistory() = _history.clear()

    /** Unload the model and release all engine resources. Do not use the session after this. */
    fun close() = LlmCppBridge.shutdown()

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** Rough token count estimate — 1 token ≈ 4 chars for English text. */
    private fun estimateInputTokens(messages: List<LlmMessage>): Int =
        messages.sumOf { it.content.length } / 4

    /**
     * Maps an exception to an opaque error code bucket.
     * Raw exception messages are never included in telemetry.
     */
    private fun Throwable.toErrorCode(): String {
        val msg = message?.lowercase() ?: return ERR_UNKNOWN
        return when {
            "oom" in msg || "memory" in msg || "outofmemory" in msg -> TelemetryEvent.ERR_OOM
            "decode" in msg                                          -> TelemetryEvent.ERR_DECODE_FAILED
            "tokeniz" in msg                                         -> TelemetryEvent.ERR_TOKENIZE_FAILED
            "cancel" in msg                                          -> TelemetryEvent.ERR_CANCELLED
            else                                                     -> ERR_UNKNOWN
        }
    }
}
