package dev.deviceai.llm

/**
 * Result of a completed LLM generation.
 *
 * @param text Generated text
 * @param tokenCount Number of tokens generated
 * @param promptTokenCount Number of tokens in the input prompt
 * @param finishReason Why generation stopped
 * @param generationTimeMs Wall-clock time for generation in milliseconds
 */
data class LlmResult(
    val text: String,
    val tokenCount: Int,
    val promptTokenCount: Int,
    val finishReason: FinishReason,
    val generationTimeMs: Long
)

enum class FinishReason {
    /** Model produced an end-of-sequence token */
    STOP,
    /** maxTokens limit was reached */
    MAX_TOKENS,
    /** Generation was cancelled by the caller */
    CANCELLED,
    /** An error occurred during generation */
    ERROR
}
