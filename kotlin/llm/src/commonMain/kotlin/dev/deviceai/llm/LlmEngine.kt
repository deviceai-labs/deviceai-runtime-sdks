package dev.deviceai.llm

import kotlinx.coroutines.flow.Flow

/**
 * Abstraction over the native LLM inference backend.
 * Callers depend on this interface rather than concrete implementations (DIP).
 */
interface LlmEngine {

    /**
     * Initialize the LLM engine with a GGUF model file.
     *
     * @param modelPath Absolute path to .gguf model file
     * @param config Engine initialization parameters
     * @return true if initialization succeeded
     */
    fun init(modelPath: String, config: LlmInitConfig = LlmInitConfig()): Boolean

    /** Release all LLM resources and unload the model. */
    fun shutdown()

    /**
     * Generate a response for the given conversation (blocking).
     *
     * @param messages Conversation history including the new user message
     * @param config Per-request generation parameters
     * @return [LlmResult] with generated text and metadata
     */
    fun generate(messages: List<LlmMessage>, config: LlmGenConfig = LlmGenConfig()): LlmResult

    /**
     * Stream a response token-by-token.
     * Each emission is one llama token piece. Collect with [kotlinx.coroutines.flow.onEach].
     *
     * @param messages Conversation history including the new user message
     * @param config Per-request generation parameters
     * @return [Flow] of token strings in generation order
     */
    fun generateStream(messages: List<LlmMessage>, config: LlmGenConfig = LlmGenConfig()): Flow<String>

    /** Cancel an in-progress generation. */
    fun cancelGeneration()
}
