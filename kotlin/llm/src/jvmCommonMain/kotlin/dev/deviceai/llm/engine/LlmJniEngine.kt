package dev.deviceai.llm.engine

import dev.deviceai.llm.FinishReason
import dev.deviceai.llm.LlmEngine
import dev.deviceai.llm.LlmGenConfig
import dev.deviceai.llm.LlmInitConfig
import dev.deviceai.llm.LlmMessage
import dev.deviceai.llm.LlmResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.flowOn
import kotlin.system.measureTimeMillis

/**
 * JNI-backed LLM engine shared by Android and JVM desktop.
 * Handles all native library concerns — loading, JNI declarations, and timing.
 */
internal object LlmJniEngine : LlmEngine {

    init {
        System.loadLibrary("deviceai_llm_jni")
    }

    override fun init(modelPath: String, config: LlmInitConfig): Boolean =
        nativeInit(modelPath, config.maxThreads, config.useGpu)

    override fun shutdown() = nativeShutdown()

    override fun generate(messages: List<LlmMessage>, config: LlmGenConfig): LlmResult {
        val roles = messages.map { it.role.name.lowercase() }.toTypedArray()
        val contents = messages.map { it.content }.toTypedArray()
        var text = ""
        val ms = measureTimeMillis {
            text = nativeGenerate(
                roles, contents,
                config.maxTokens, config.temperature,
                config.topP, config.topK, config.repeatPenalty
            )
        }
        return LlmResult(
            text = text,
            tokenCount = if (text.isBlank()) 0 else text.trim().split(Regex("\\s+")).size,
            promptTokenCount = 0,
            finishReason = FinishReason.STOP,
            generationTimeMs = ms
        )
    }

    override fun generateStream(messages: List<LlmMessage>, config: LlmGenConfig): Flow<String> =
        channelFlow {
            val roles = messages.map { it.role.name.lowercase() }.toTypedArray()
            val contents = messages.map { it.content }.toTypedArray()
            nativeGenerateStream(
                roles, contents,
                config.maxTokens, config.temperature,
                config.topP, config.topK, config.repeatPenalty,
                object : LlmStreamInternal {
                    override fun onToken(token: String) { trySend(token) }
                    override fun onError(message: String) { close(RuntimeException(message)) }
                }
            )
        }.flowOn(Dispatchers.IO)

    override fun cancelGeneration() = nativeCancel()

    // ──────────────────────────────────────────────────────────────
    //                    NATIVE DECLARATIONS
    // ──────────────────────────────────────────────────────────────

    private external fun nativeInit(
        modelPath: String, maxThreads: Int, useGpu: Boolean
    ): Boolean

    private external fun nativeShutdown()

    private external fun nativeGenerate(
        roles: Array<String>, contents: Array<String>,
        maxTokens: Int, temperature: Float,
        topP: Float, topK: Int, repeatPenalty: Float
    ): String

    private external fun nativeGenerateStream(
        roles: Array<String>, contents: Array<String>,
        maxTokens: Int, temperature: Float,
        topP: Float, topK: Int, repeatPenalty: Float,
        callback: LlmStreamInternal
    )

    private external fun nativeCancel()
}
