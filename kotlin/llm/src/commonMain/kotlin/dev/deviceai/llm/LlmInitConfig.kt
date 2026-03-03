package dev.deviceai.llm

/**
 * Configuration for LLM engine initialization.
 *
 * @param contextSize Maximum context window in tokens (default 2048)
 * @param maxThreads CPU threads for inference (default 4)
 * @param useGpu Use GPU acceleration — Metal on iOS, Vulkan on Android (default true)
 */
data class LlmInitConfig(
    val contextSize: Int = 2048,
    val maxThreads: Int = 4,
    val useGpu: Boolean = true
)
