#include "../c_interop/include/llm_ios.h"
#include "llama.h"

#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstring>
#include <cstdio>

// ═══════════════════════════════════════════════════════════════
//                         Global state
// ═══════════════════════════════════════════════════════════════

static llama_model   *g_model   = nullptr;
static llama_context *g_ctx     = nullptr;
static std::atomic<bool> g_cancel{false};

// ═══════════════════════════════════════════════════════════════
//                         Helpers
// ═══════════════════════════════════════════════════════════════

static void cleanup() {
    if (g_ctx)   { llama_free(g_ctx);           g_ctx   = nullptr; }
    if (g_model) { llama_model_free(g_model);   g_model = nullptr; }
}

static llama_sampler *build_sampler(float temperature, float top_p, int top_k, float repeat_penalty) {
    auto *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(
        64, repeat_penalty, 0.0f, 0.0f
    ));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

// ═══════════════════════════════════════════════════════════════
//              Chat-template prompt formatting
// Accepts role/content arrays. Uses the model's embedded Jinja template
// via llama_chat_apply_template (ChatML, Llama 3, Gemma, Mistral, etc.).
// ═══════════════════════════════════════════════════════════════

static std::string build_full_prompt(const char **roles, const char **contents, int count) {
    if (!g_model || count <= 0) return "";

    std::vector<std::string>        storage;
    std::vector<llama_chat_message> msgs;

    for (int i = 0; i < count; i++) {
        storage.push_back(roles[i]    ? roles[i]    : "");
        storage.push_back(contents[i] ? contents[i] : "");
        msgs.push_back({ storage[storage.size()-2].c_str(), storage.back().c_str() });
    }

    const char *tmpl = llama_model_chat_template(g_model, nullptr);
    int32_t sz = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, nullptr, 0);
    if (sz <= 0) return "";

    std::string out(sz, '\0');
    llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, out.data(), sz);
    return out;
}

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

    llama_memory_clear(llama_get_memory(g_ctx), /*data=*/false);
    llama_perf_context_reset(g_ctx);

    const llama_vocab *vocab = llama_model_get_vocab(g_model);
    int n_ctx_max = llama_n_ctx(g_ctx);
    std::vector<llama_token> tokens(n_ctx_max);
    int n_tokens = llama_tokenize(
        vocab,
        full_prompt.c_str(),
        (int)full_prompt.size(),
        tokens.data(),
        n_ctx_max,
        true, true
    );
    if (n_tokens < 0) return "";
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
    if (llama_decode(g_ctx, batch)) return "";

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

        if (!on_token(piece)) break;

        llama_batch next = llama_batch_get_one(&token, 1);
        if (llama_decode(g_ctx, next)) break;
    }

    llama_sampler_free(sampler);
    return result;
}

// ═══════════════════════════════════════════════════════════════
//                         C API
// ═══════════════════════════════════════════════════════════════

extern "C" {

bool llm_init(const char *model_path, int context_size, int max_threads, bool use_gpu) {
    cleanup();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = use_gpu ? 99 : 0;

    g_model = llama_model_load_from_file(model_path, mparams);
    if (!g_model) {
        fprintf(stderr, "[LlmIos] Failed to load model: %s\n", model_path);
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = context_size;
    cparams.n_threads = max_threads;

    g_ctx = llama_init_from_model(g_model, cparams);
    if (!g_ctx) {
        fprintf(stderr, "[LlmIos] Failed to create context\n");
        llama_model_free(g_model);
        g_model = nullptr;
        return false;
    }

    fprintf(stdout, "[LlmIos] Initialized: ctx=%d threads=%d gpu=%d\n",
            context_size, max_threads, use_gpu);
    return true;
}

void llm_shutdown(void) {
    cleanup();
}

char *llm_generate(
    const char **roles, const char **contents, int count,
    int max_tokens, float temperature,
    float top_p, int top_k, float repeat_penalty
) {
    g_cancel = false;
    std::string full = build_full_prompt(roles, contents, count);
    std::string result = do_generate(
        full, max_tokens, temperature, top_p, top_k, repeat_penalty,
        [](const std::string &) { return true; }
    );
    char *out = (char *)malloc(result.size() + 1);
    if (out) memcpy(out, result.c_str(), result.size() + 1);
    return out;
}

void llm_generate_stream(
    const char **roles, const char **contents, int count,
    int max_tokens, float temperature,
    float top_p, int top_k, float repeat_penalty,
    llm_on_token on_token,
    llm_on_error on_error,
    void *user
) {
    g_cancel = false;
    std::string full = build_full_prompt(roles, contents, count);

    do_generate(
        full, max_tokens, temperature, top_p, top_k, repeat_penalty,
        [&](const std::string &piece) -> bool {
            if (on_token) on_token(piece.c_str(), user);
            return !g_cancel.load();
        }
    );
    // Flow completes naturally when llm_generate_stream returns — no on_complete callback needed.
}

void llm_cancel(void) {
    g_cancel = true;
}

void llm_free_string(char *ptr) {
    free(ptr);
}

} // extern "C"
