package dev.deviceai.llm.models

/**
 * Curated catalog of GGUF models recommended for on-device inference.
 *
 * All models are hosted on HuggingFace and use Q4_K_M quantization by default
 * for a good balance of quality and size on mobile hardware.
 *
 * Usage:
 * ```kotlin
 * val model = LlmCatalog.LLAMA_3_2_1B_INSTRUCT_Q4
 * // download via LlmRegistry, then:
 * LlmBridge.initLlm(localModel.modelPath)
 * ```
 */
object LlmCatalog {

    // ══════════════════════════════════════════════════════════════
    //                   Llama 3.2 (Meta) — recommended
    // ══════════════════════════════════════════════════════════════

    /** 1B — fits on any modern phone (~700MB), fast inference */
    val LLAMA_3_2_1B_INSTRUCT_Q4 = LlmModelInfo(
        id = "llama-3.2-1b-instruct-q4_k_m",
        name = "Llama 3.2 1B Instruct Q4_K_M",
        repoId = "bartowski/Llama-3.2-1B-Instruct-GGUF",
        filename = "Llama-3.2-1B-Instruct-Q4_K_M.gguf",
        sizeBytes = 742_000_000L,
        quantization = "Q4_K_M",
        parameters = "1B",
        description = "Smallest Llama 3.2 instruct model. Good for simple tasks on low-end devices."
    )

    /** 3B — better quality, needs ~2GB RAM */
    val LLAMA_3_2_3B_INSTRUCT_Q4 = LlmModelInfo(
        id = "llama-3.2-3b-instruct-q4_k_m",
        name = "Llama 3.2 3B Instruct Q4_K_M",
        repoId = "bartowski/Llama-3.2-3B-Instruct-GGUF",
        filename = "Llama-3.2-3B-Instruct-Q4_K_M.gguf",
        sizeBytes = 1_890_000_000L,
        quantization = "Q4_K_M",
        parameters = "3B",
        description = "Strong reasoning and instruction-following. Best balance for most phones."
    )

    // ══════════════════════════════════════════════════════════════
    //                   Gemma 2 (Google)
    // ══════════════════════════════════════════════════════════════

    /** 2B — compact, strong multilingual support */
    val GEMMA_2_2B_INSTRUCT_Q4 = LlmModelInfo(
        id = "gemma-2-2b-instruct-q4_k_m",
        name = "Gemma 2 2B Instruct Q4_K_M",
        repoId = "bartowski/gemma-2-2b-it-GGUF",
        filename = "gemma-2-2b-it-Q4_K_M.gguf",
        sizeBytes = 1_630_000_000L,
        quantization = "Q4_K_M",
        parameters = "2B",
        description = "Google's compact model. Strong multilingual and code performance."
    )

    // ══════════════════════════════════════════════════════════════
    //                   Phi-3.5 (Microsoft)
    // ══════════════════════════════════════════════════════════════

    /** 3.8B — punches above its weight, great for reasoning */
    val PHI_3_5_MINI_INSTRUCT_Q4 = LlmModelInfo(
        id = "phi-3.5-mini-instruct-q4_k_m",
        name = "Phi-3.5 Mini Instruct Q4_K_M",
        repoId = "bartowski/Phi-3.5-mini-instruct-GGUF",
        filename = "Phi-3.5-mini-instruct-Q4_K_M.gguf",
        sizeBytes = 2_390_000_000L,
        quantization = "Q4_K_M",
        parameters = "3.8B",
        description = "Microsoft's efficient model. Excellent reasoning relative to size."
    )

    // ══════════════════════════════════════════════════════════════
    //                   Convenience list
    // ══════════════════════════════════════════════════════════════

    /** All curated models, ordered by size ascending */
    val all: List<LlmModelInfo> = listOf(
        LLAMA_3_2_1B_INSTRUCT_Q4,
        GEMMA_2_2B_INSTRUCT_Q4,
        LLAMA_3_2_3B_INSTRUCT_Q4,
        PHI_3_5_MINI_INSTRUCT_Q4
    )
}
