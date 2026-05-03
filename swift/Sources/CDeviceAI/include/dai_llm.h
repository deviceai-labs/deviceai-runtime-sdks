/**
 * dai_llm.h — Platform-agnostic LLM inference engine.
 *
 * Wraps llama.cpp with chat-template prompt formatting, sampler chain,
 * and streaming decode loop. Called by both JNI (Android) and Swift (iOS).
 *
 * Global state: one model loaded at a time. Thread-safe cancel via atomic flag.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load a GGUF model.
 *
 * @param model_path  Absolute path to .gguf file.
 * @param max_threads CPU threads for inference.
 * @param use_gpu     true → offload all layers to GPU (Metal/Vulkan).
 * @return            true on success.
 */
bool dai_llm_init(const char* model_path, int max_threads, bool use_gpu);

/** Unload model and free all resources. */
void dai_llm_shutdown(void);

/**
 * Generate a complete response (blocking).
 *
 * @param roles         Array of role strings ("system", "user", "assistant").
 * @param contents      Array of content strings (parallel to roles).
 * @param n_messages    Number of messages.
 * @param max_tokens    Maximum tokens to generate.
 * @param temperature   Sampling temperature (0 = deterministic).
 * @param top_p         Nucleus sampling threshold.
 * @param top_k         Top-K sampling pool size.
 * @param repeat_penalty Repetition penalty (1.0 = none).
 * @return              Generated text. Caller must free with dai_llm_free_string().
 *                      Returns NULL on failure.
 */
char* dai_llm_generate(
    const char** roles,
    const char** contents,
    int n_messages,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty
);

/** Callback for streaming token generation. Return false from on_token to stop. */
typedef void (*dai_llm_on_token_fn)(const char* token, void* ctx);
typedef void (*dai_llm_on_error_fn)(const char* message, void* ctx);

/**
 * Generate a response with per-token streaming callback.
 *
 * @param on_token  Called for each generated token. May be called from any thread.
 * @param on_error  Called on error. May be NULL.
 * @param ctx       Opaque pointer passed to callbacks.
 */
void dai_llm_generate_stream(
    const char** roles,
    const char** contents,
    int n_messages,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty,
    dai_llm_on_token_fn on_token,
    dai_llm_on_error_fn on_error,
    void* ctx
);

/** Cancel any in-progress generation. Thread-safe. */
void dai_llm_cancel(void);

/** Free a string returned by dai_llm_generate(). */
void dai_llm_free_string(char* str);

#ifdef __cplusplus
}
#endif
