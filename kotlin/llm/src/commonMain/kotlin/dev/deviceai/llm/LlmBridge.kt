package dev.deviceai.llm

import kotlinx.coroutines.flow.Flow

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
expect object LlmBridge {

    // ══════════════════════════════════════════════════════════════
    //                        LIFECYCLE
    // ══════════════════════════════════════════════════════════════

    /**
     * Initialize the LLM engine with a GGUF model file.
     *
     * @param modelPath Absolute path to .gguf model file
     * @param config Engine initialization parameters
     * @return true if initialization succeeded
     */
    fun initLlm(modelPath: String, config: LlmInitConfig = LlmInitConfig()): Boolean

    /**
     * Release all LLM resources and unload the model.
     */
    fun shutdown()

    // ══════════════════════════════════════════════════════════════
    //                        GENERATION
    // ══════════════════════════════════════════════════════════════

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
     * Each emission is one llama token piece.
     *
     * @param messages Conversation history including the new user message
     * @param config Per-request generation parameters
     * @return [Flow] of token strings in generation order
     */
    fun generateStream(messages: List<LlmMessage>, config: LlmGenConfig = LlmGenConfig()): Flow<String>

    /**
     * Cancel an in-progress generation.
     */
    fun cancelGeneration()
}
