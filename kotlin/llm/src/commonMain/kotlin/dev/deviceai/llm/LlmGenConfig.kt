package dev.deviceai.llm

/**
 * Per-request generation parameters.
 * System prompt is passed as [LlmMessage] with role [LlmRole.SYSTEM] instead.
 *
 * @param maxTokens Maximum tokens to generate per request (default 512)
 * @param temperature Sampling temperature — higher = more creative (default 0.7)
 * @param topP Nucleus sampling probability threshold (default 0.9)
 * @param topK Top-K sampling limit (default 40)
 * @param repeatPenalty Penalty for repeating tokens (default 1.1)
 */
data class LlmGenConfig(
    val maxTokens: Int = 512,
    val temperature: Float = 0.7f,
    val topP: Float = 0.9f,
    val topK: Int = 40,
    val repeatPenalty: Float = 1.1f
)
