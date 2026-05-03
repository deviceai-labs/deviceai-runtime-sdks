/**
 * llm_engine.cpp — Platform-agnostic LLM inference via llama.cpp.
 *
 * Extracted from deviceai_llm_jni.cpp. Contains all inference logic:
 * - Model loading and context creation
 * - Chat-template prompt formatting (Jinja via llama_chat_apply_template)
 * - Sampler chain construction (top_k → top_p → temp → penalties → dist)
 * - Decode loop with per-token streaming and cancellation
 *
 * No JNI, no platform-specific code. Called by bridges/jni/ and bridges/ios/.
 */

#include "dai_llm.h"
#include "llama.h"

#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ═══════════════════════════════════════════════════════════════
//                         Global state
// ═══════════════════════════════════════════════════════════════

static llama_model   *g_model   = nullptr;
static llama_context *g_ctx     = nullptr;
static std::atomic<bool> g_cancel{false};

// ═══════════════════════════════════════════════════════════════
//                         Logging
// ═══════════════════════════════════════════════════════════════

#ifdef ANDROID
#include <android/log.h>
#define LOG_TAG "LlmEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// ═══════════════════════════════════════════════════════════════
//                         Helpers
// ═══════════════════════════════════════════════════════════════

static void cleanup() {
    if (g_ctx)   { llama_free(g_ctx);         g_ctx   = nullptr; }
    if (g_model) { llama_model_free(g_model);  g_model = nullptr; }
}

/**
 * Build a formatted prompt from role/content arrays using the model's
 * embedded Jinja chat template (ChatML, Llama 3, Gemma, Mistral, etc.).
 *
 * CRITICAL: storage vector MUST be pre-reserved before the loop.
 * Any reallocation invalidates .c_str() pointers stored in msgs (dangling pointer UB).
 */
static std::string build_prompt(const char** roles, const char** contents, int n_messages) {
    if (!g_model || n_messages <= 0) return "";

    std::vector<std::string>        storage;
    std::vector<llama_chat_message> msgs;
    storage.reserve(n_messages * 2);

    for (int i = 0; i < n_messages; i++) {
        storage.push_back(roles[i] ? roles[i] : "");
        storage.push_back(contents[i] ? contents[i] : "");
        msgs.push_back({ storage[storage.size()-2].c_str(), storage.back().c_str() });
    }

    const char *tmpl = llama_model_chat_template(g_model, nullptr);
    int32_t sz = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, nullptr, 0);
    if (sz <= 0) return "";

    std::string out(sz, '\0');
    llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, &out[0], sz);
    return out;
}

/**
 * Build a sampler chain: top_k → top_p → temp → penalties → dist.
 */
static llama_sampler* build_sampler(float temperature, float top_p, int top_k, float repeat_penalty) {
    auto *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(
        64,             // last_n penalty window
        repeat_penalty,
        0.0f,           // freq penalty
        0.0f            // presence penalty
    ));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

/**
 * Core decode loop. Tokenizes prompt, runs inference, calls on_token for each piece.
 * Returns the full generated string.
 */
static std::string do_generate(
    const std::string &full_prompt,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty,
    std::function<bool(const std::string &)> on_token
) {
    if (!g_model || !g_ctx) return "";

    llama_memory_clear(llama_get_memory(g_ctx), false);
    llama_perf_context_reset(g_ctx);

    const llama_vocab *vocab = llama_model_get_vocab(g_model);

    // Tokenize
    int n_prompt_max = llama_n_ctx(g_ctx);
    std::vector<llama_token> tokens(n_prompt_max);
    int n_tokens = llama_tokenize(
        vocab, full_prompt.c_str(), (int)full_prompt.size(),
        tokens.data(), n_prompt_max, true, true
    );
    if (n_tokens < 0) {
        LOGE("Tokenization failed");
        return "";
    }
    tokens.resize(n_tokens);

    // Decode prompt in one batch
    llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
    if (llama_decode(g_ctx, batch)) {
        LOGE("llama_decode prompt failed");
        return "";
    }

    // Build sampler
    auto *sampler = build_sampler(temperature, top_p, top_k, repeat_penalty);

    std::string result;
    char piece_buf[256];
    int n_generated = 0;

    while (n_generated < max_tokens && !g_cancel.load()) {
        llama_token token = llama_sampler_sample(sampler, g_ctx, -1);
        llama_sampler_accept(sampler, token);

        if (llama_vocab_is_eog(vocab, token)) break;

        int n = llama_token_to_piece(vocab, token, piece_buf, sizeof(piece_buf), 0, true);
        if (n < 0) break;

        std::string piece(piece_buf, n);
        result += piece;
        n_generated++;

        if (on_token && !on_token(piece)) break;

        llama_batch next = llama_batch_get_one(&token, 1);
        if (llama_decode(g_ctx, next)) break;
    }

    llama_sampler_free(sampler);
    return result;
}

// ═══════════════════════════════════════════════════════════════
//                    Public C API (dai_llm.h)
// ═══════════════════════════════════════════════════════════════

extern "C" {

bool dai_llm_init(const char* model_path, int max_threads, bool use_gpu) {
    cleanup();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = use_gpu ? 99 : 0;

    g_model = llama_model_load_from_file(model_path, mparams);
    if (!g_model) {
        LOGE("Failed to load model from %s", model_path);
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = 0; // use model's native context size
    cparams.n_threads = max_threads;

    g_ctx = llama_init_from_model(g_model, cparams);
    if (!g_ctx) {
        LOGE("Failed to create llama context");
        llama_model_free(g_model);
        g_model = nullptr;
        return false;
    }

    LOGI("LLM initialized: %s (ctx=%d, threads=%d, gpu=%d)",
         model_path, llama_n_ctx(g_ctx), max_threads, use_gpu);
    return true;
}

void dai_llm_shutdown(void) {
    cleanup();
}

char* dai_llm_generate(
    const char** roles,
    const char** contents,
    int n_messages,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty
) {
    g_cancel = false;
    std::string prompt = build_prompt(roles, contents, n_messages);

    std::string result = do_generate(
        prompt, max_tokens, temperature, top_p, top_k, repeat_penalty,
        nullptr // no streaming callback
    );

    return result.empty() ? nullptr : strdup(result.c_str());
}

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
) {
    g_cancel = false;
    std::string prompt = build_prompt(roles, contents, n_messages);

    if (prompt.empty()) {
        if (on_error) on_error("Failed to build prompt", ctx);
        return;
    }

    do_generate(
        prompt, max_tokens, temperature, top_p, top_k, repeat_penalty,
        [&](const std::string &piece) -> bool {
            if (on_token) on_token(piece.c_str(), ctx);
            return !g_cancel.load();
        }
    );
}

void dai_llm_cancel(void) {
    g_cancel = true;
}

void dai_llm_free_string(char* str) {
    free(str);
}

} // extern "C"
