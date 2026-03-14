package dev.deviceai.llm

/**
 * Configuration for a [ChatSession].
 *
 * Passed as a DSL block to [dev.deviceai.core.LlmModule.chat]:
 * ```kotlin
 * val session = DeviceAI.llm.chat(modelPath) {
 *     systemPrompt  = "You are a helpful assistant."
 *     temperature   = 0.8f
 *     maxTokens     = 512
 *     gpuLayers     = 99    // 0 = CPU only, 99 = all layers on GPU
 * }
 * ```
 *
 * All fields have sensible defaults — a zero-config session works out of the box.
 */
class ChatConfig {

    // ── Conversation ─────────────────────────────────────────────────────────

    /**
     * System prompt applied to every request in this session.
     * Shapes the model's persona, tone, and behaviour.
     */
    var systemPrompt: String = "You are a helpful assistant."

    // ── Generation ───────────────────────────────────────────────────────────

    /** Maximum number of tokens to generate per response. */
    var maxTokens: Int = 512

    /**
     * Sampling temperature. Higher = more creative / random.
     * Lower = more deterministic / focused.
     * Range: 0.0–2.0. Default: 0.7.
     */
    var temperature: Float = 0.7f

    /**
     * Nucleus sampling threshold. Tokens whose cumulative probability
     * exceeds this value are discarded.
     * Range: 0.0–1.0. Default: 0.9.
     */
    var topP: Float = 0.9f

    /**
     * Top-k sampling. Considers only the k most probable tokens at each step.
     * Set to 0 to disable. Default: 40.
     */
    var topK: Int = 40

    /**
     * Repetition penalty. Values > 1.0 discourage repeating tokens.
     * Range: 1.0–2.0. Default: 1.1.
     */
    var repeatPenalty: Float = 1.1f

    // ── Engine (init-time) ────────────────────────────────────────────────────

    /**
     * KV-cache context window size in tokens.
     * Larger = longer memory, higher RAM usage.
     * Default: 4096.
     */
    var contextSize: Int = 4096

    /**
     * Number of CPU threads for inference.
     * Default: 4.
     */
    var threads: Int = 4

    /**
     * Number of model layers to offload to the GPU.
     * `0` = CPU-only. `99` = all layers on GPU (recommended when available).
     * Default: 99.
     */
    var gpuLayers: Int = 99

    // ── Internal helpers ──────────────────────────────────────────────────────

    internal fun toInitConfig() = LlmInitConfig(
        contextSize = contextSize,
        maxThreads  = threads,
        useGpu      = gpuLayers > 0,
    )

    internal fun toGenConfig() = LlmGenConfig(
        maxTokens     = maxTokens,
        temperature   = temperature,
        topP          = topP,
        topK          = topK,
        repeatPenalty = repeatPenalty,
    )
}
