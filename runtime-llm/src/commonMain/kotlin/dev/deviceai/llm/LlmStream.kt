package dev.deviceai.llm

/**
 * Streaming callbacks for token-by-token LLM generation.
 */
interface LlmStream {
    /**
     * Called for each generated token piece.
     * Tokens arrive in order — concatenate them to build the full response.
     *
     * @param token The token text (may be a word, sub-word, or single character)
     */
    fun onToken(token: String)

    /**
     * Called when generation is complete.
     *
     * @param result Final result with full text and metadata
     */
    fun onComplete(result: LlmResult)

    /**
     * Called if an error occurs during generation.
     *
     * @param message Human-readable error description
     */
    fun onError(message: String)
}
