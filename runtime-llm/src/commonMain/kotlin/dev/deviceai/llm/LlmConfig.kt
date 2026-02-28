package dev.deviceai.llm

/**
 * Configuration for LLM initialization and generation.
 *
 * @param contextSize Maximum context window in tokens (default 2048)
 * @param maxTokens Maximum tokens to generate per request (default 512)
 * @param temperature Sampling temperature — higher = more creative (default 0.7)
 * @param topP Nucleus sampling probability threshold (default 0.9)
 * @param topK Top-K sampling limit (default 40)
 * @param repeatPenalty Penalty for repeating tokens (default 1.1)
 * @param systemPrompt System prompt prepended to every request (default empty)
 * @param maxThreads CPU threads for inference (default 4)
 * @param useGpu Use GPU acceleration — Metal on iOS, Vulkan on Android (default true)
 */
data class LlmConfig(
    val contextSize: Int = 2048,
    val maxTokens: Int = 512,
    val temperature: Float = 0.7f,
    val topP: Float = 0.9f,
    val topK: Int = 40,
    val repeatPenalty: Float = 1.1f,
    val systemPrompt: String = "",
    val maxThreads: Int = 4,
    val useGpu: Boolean = true
)
