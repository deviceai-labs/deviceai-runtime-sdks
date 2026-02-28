#ifndef LLM_IOS_H
#define LLM_IOS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════
//                         LIFECYCLE
// ═══════════════════════════════════════════════════════════════

/**
 * Initialize the LLM engine with a GGUF model file.
 *
 * @param model_path Absolute path to .gguf model file
 * @param context_size Maximum context window in tokens
 * @param max_threads CPU threads for inference
 * @param use_gpu Use GPU acceleration (Metal on iOS)
 * @return true if initialization succeeded
 */
bool llm_init(const char *model_path, int context_size, int max_threads, bool use_gpu);

/**
 * Release all LLM resources and unload the model.
 */
void llm_shutdown(void);

// ═══════════════════════════════════════════════════════════════
//                         GENERATION
// ═══════════════════════════════════════════════════════════════

/**
 * Generate a response for the given prompt (blocking).
 *
 * @param prompt Input text prompt
 * @param system_prompt System prompt (empty string for none)
 * @param max_tokens Maximum tokens to generate
 * @param temperature Sampling temperature
 * @param top_p Nucleus sampling threshold
 * @param top_k Top-K sampling limit
 * @param repeat_penalty Repetition penalty
 * @return Generated text (caller must free with llm_free_string)
 */
char *llm_generate(
    const char *prompt,
    const char *system_prompt,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty
);

// Streaming callbacks
typedef void (*llm_on_token)(const char *token, void *user);
typedef void (*llm_on_complete)(const char *full_text, int token_count, void *user);
typedef void (*llm_on_error)(const char *message, void *user);

/**
 * Stream a response token-by-token.
 *
 * @param prompt Input text prompt
 * @param system_prompt System prompt (empty string for none)
 * @param max_tokens Maximum tokens to generate
 * @param temperature Sampling temperature
 * @param top_p Nucleus sampling threshold
 * @param top_k Top-K sampling limit
 * @param repeat_penalty Repetition penalty
 * @param on_token Callback for each generated token piece
 * @param on_complete Callback when generation finishes
 * @param on_error Callback for errors
 * @param user User data passed to all callbacks
 */
void llm_generate_stream(
    const char *prompt,
    const char *system_prompt,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty,
    llm_on_token on_token,
    llm_on_complete on_complete,
    llm_on_error on_error,
    void *user
);

/**
 * Cancel an in-progress generation.
 */
void llm_cancel(void);

// ═══════════════════════════════════════════════════════════════
//                         UTILITIES
// ═══════════════════════════════════════════════════════════════

/**
 * Free a string returned by llm_generate.
 */
void llm_free_string(char *ptr);

#ifdef __cplusplus
}
#endif

#endif // LLM_IOS_H
