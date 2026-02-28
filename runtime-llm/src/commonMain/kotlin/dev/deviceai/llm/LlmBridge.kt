package dev.deviceai.llm

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
expect object LlmBridge {

    // ══════════════════════════════════════════════════════════════
    //                        LIFECYCLE
    // ══════════════════════════════════════════════════════════════

    /**
     * Initialize the LLM engine with a GGUF model file.
     *
     * @param modelPath Absolute path to .gguf model file
     * @param config Optional configuration parameters
     * @return true if initialization succeeded
     */
    fun initLlm(modelPath: String, config: LlmConfig = LlmConfig()): Boolean

    /**
     * Release all LLM resources and unload the model.
     */
    fun shutdown()

    // ══════════════════════════════════════════════════════════════
    //                        GENERATION
    // ══════════════════════════════════════════════════════════════

    /**
     * Generate a response for the given prompt (blocking).
     *
     * @param prompt Input text prompt
     * @param config Optional per-request config overrides
     * @return LlmResult with generated text and metadata
     */
    fun generate(prompt: String, config: LlmConfig = LlmConfig()): LlmResult

    /**
     * Stream a response token-by-token.
     *
     * @param prompt Input text prompt
     * @param config Optional per-request config overrides
     * @param callback Callbacks for tokens, completion, and errors
     */
    fun generateStream(prompt: String, config: LlmConfig = LlmConfig(), callback: LlmStream)

    /**
     * Cancel an in-progress generation.
     */
    fun cancelGeneration()
}
