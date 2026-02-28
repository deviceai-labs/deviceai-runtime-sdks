package dev.deviceai.llm.models

/**
 * Describes a GGUF model available for download from HuggingFace.
 *
 * @param id Unique identifier (e.g. "llama-3.2-1b-instruct-q4_k_m")
 * @param name Human-readable name (e.g. "Llama 3.2 1B Instruct Q4_K_M")
 * @param repoId HuggingFace repo (e.g. "bartowski/Llama-3.2-1B-Instruct-GGUF")
 * @param filename GGUF filename in the repo
 * @param sizeBytes Approximate file size in bytes
 * @param quantization Quantization level (e.g. Q4_K_M, Q5_K_M, Q8_0)
 * @param parameters Parameter count label (e.g. "1B", "3B", "7B")
 * @param description Short description of the model
 */
data class LlmModelInfo(
    val id: String,
    val name: String,
    val repoId: String,
    val filename: String,
    val sizeBytes: Long,
    val quantization: String,
    val parameters: String,
    val description: String
)

/**
 * A GGUF model that has been downloaded to local storage.
 *
 * @param id Model identifier matching [LlmModelInfo.id]
 * @param modelPath Absolute path to the .gguf file on disk
 * @param sizeBytes File size in bytes
 */
data class LocalLlmModel(
    val id: String,
    val modelPath: String,
    val sizeBytes: Long
)
