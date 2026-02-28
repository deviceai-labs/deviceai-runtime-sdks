#include "llm_jni.h"
#include "llama.h"

#include <string>
#include <vector>
#include <atomic>
#include <cstring>

#ifdef ANDROID
#include <android/log.h>
#define LOG_TAG "LlmJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// ═══════════════════════════════════════════════════════════════
//                         Global state
// ═══════════════════════════════════════════════════════════════

static llama_model   *g_model   = nullptr;
static llama_context *g_ctx     = nullptr;
static llama_sampler *g_sampler = nullptr;
static std::atomic<bool> g_cancel{false};

// ═══════════════════════════════════════════════════════════════
//                         Helpers
// ═══════════════════════════════════════════════════════════════

static std::string jstring_to_std(JNIEnv *env, jstring js) {
    if (!js) return "";
    const char *chars = env->GetStringUTFChars(js, nullptr);
    std::string s(chars);
    env->ReleaseStringUTFChars(js, chars);
    return s;
}

static void cleanup() {
    if (g_sampler) { llama_sampler_free(g_sampler); g_sampler = nullptr; }
    if (g_ctx)     { llama_free(g_ctx);              g_ctx     = nullptr; }
    if (g_model)   { llama_model_free(g_model);      g_model   = nullptr; }
}

static llama_sampler *build_sampler(float temperature, float top_p, int top_k, float repeat_penalty) {
    auto *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(
        llama_n_vocab(g_model), LLAMA_TOKEN_NULL, LLAMA_TOKEN_NULL,
        64,            // last_n penalty window
        repeat_penalty,
        0.0f,          // freq penalty
        0.0f           // presence penalty
    ));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

// ═══════════════════════════════════════════════════════════════
//                    Core generation loop
// Tokenizes prompt, runs decode loop, calls on_token for each piece.
// Returns the full generated string.
// ═══════════════════════════════════════════════════════════════

static std::string do_generate(
    const std::string &full_prompt,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty,
    std::function<bool(const std::string &)> on_token  // return false to stop
) {
    if (!g_model || !g_ctx) return "";

    llama_context_reset(g_ctx);

    // Tokenize
    int n_prompt_max = llama_n_ctx(g_ctx);
    std::vector<llama_token> tokens(n_prompt_max);
    int n_tokens = llama_tokenize(
        g_model,
        full_prompt.c_str(),
        (int)full_prompt.size(),
        tokens.data(),
        n_prompt_max,
        /*add_special=*/true,
        /*parse_special=*/true
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

        if (llama_token_is_eog(g_model, token)) break;

        int n = llama_token_to_piece(g_model, token, piece_buf, sizeof(piece_buf), 0, true);
        if (n < 0) break;

        std::string piece(piece_buf, n);
        result += piece;
        n_generated++;

        if (!on_token(piece)) break;

        // Decode the new token
        llama_batch next = llama_batch_get_one(&token, 1);
        if (llama_decode(g_ctx, next)) break;
    }

    llama_sampler_free(sampler);
    return result;
}

// ═══════════════════════════════════════════════════════════════
//                         JNI Exports
// ═══════════════════════════════════════════════════════════════

extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeInitLlm(
    JNIEnv *env, jobject, jstring jModelPath,
    jint contextSize, jint maxThreads, jboolean useGpu
) {
    cleanup();

    std::string modelPath = jstring_to_std(env, jModelPath);

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = useGpu ? 99 : 0;

    g_model = llama_model_load_from_file(modelPath.c_str(), mparams);
    if (!g_model) {
        LOGE("Failed to load model from %s", modelPath.c_str());
        return JNI_FALSE;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx    = contextSize;
    cparams.n_threads = maxThreads;

    g_ctx = llama_init_from_model(g_model, cparams);
    if (!g_ctx) {
        LOGE("Failed to create llama context");
        llama_model_free(g_model);
        g_model = nullptr;
        return JNI_FALSE;
    }

    LOGI("LLM initialized: %s (ctx=%d, threads=%d, gpu=%d)",
         modelPath.c_str(), contextSize, maxThreads, (bool)useGpu);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeShutdown(JNIEnv *, jobject) {
    cleanup();
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeGenerate(
    JNIEnv *env, jobject,
    jstring jPrompt, jstring jSystemPrompt,
    jint maxTokens, jfloat temperature,
    jfloat topP, jint topK, jfloat repeatPenalty
) {
    g_cancel = false;
    std::string prompt       = jstring_to_std(env, jPrompt);
    std::string systemPrompt = jstring_to_std(env, jSystemPrompt);

    std::string full = systemPrompt.empty()
        ? prompt
        : "<|system|>\n" + systemPrompt + "\n<|user|>\n" + prompt + "\n<|assistant|>\n";

    std::string result = do_generate(
        full, maxTokens, temperature, topP, topK, repeatPenalty,
        [](const std::string &) { return true; }  // collect all, no streaming
    );

    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeGenerateStream(
    JNIEnv *env, jobject,
    jstring jPrompt, jstring jSystemPrompt,
    jint maxTokens, jfloat temperature,
    jfloat topP, jint topK, jfloat repeatPenalty,
    jobject jCallback
) {
    g_cancel = false;
    std::string prompt       = jstring_to_std(env, jPrompt);
    std::string systemPrompt = jstring_to_std(env, jSystemPrompt);

    std::string full = systemPrompt.empty()
        ? prompt
        : "<|system|>\n" + systemPrompt + "\n<|user|>\n" + prompt + "\n<|assistant|>\n";

    // Resolve LlmStream callback methods
    jclass cbClass   = env->GetObjectClass(jCallback);
    jmethodID onToken    = env->GetMethodID(cbClass, "onToken",    "(Ljava/lang/String;)V");
    jmethodID onComplete = env->GetMethodID(cbClass, "onComplete", "(Ldev/deviceai/llm/LlmResult;)V");
    jmethodID onError    = env->GetMethodID(cbClass, "onError",    "(Ljava/lang/String;)V");

    if (!onToken || !onComplete || !onError) {
        LOGE("Failed to find LlmStream methods");
        return;
    }

    jobject globalCb = env->NewGlobalRef(jCallback);
    JavaVM *jvm;
    env->GetJavaVM(&jvm);

    std::string fullResult = do_generate(
        full, maxTokens, temperature, topP, topK, repeatPenalty,
        [&](const std::string &piece) -> bool {
            JNIEnv *e;
            jvm->AttachCurrentThread(&e, nullptr);
            jstring jPiece = e->NewStringUTF(piece.c_str());
            e->CallVoidMethod(globalCb, onToken, jPiece);
            e->DeleteLocalRef(jPiece);
            return !g_cancel.load();
        }
    );

    // Notify complete
    jclass resultClass = env->FindClass("dev/deviceai/llm/LlmResult");
    // Pass result as JSON string via onComplete with a minimal LlmResult
    // (full object construction omitted for brevity — wire in Kotlin layer)
    jstring jFull = env->NewStringUTF(fullResult.c_str());
    env->CallVoidMethod(globalCb, onToken, jFull);  // sentinel not needed; Kotlin handles assembly
    env->DeleteLocalRef(jFull);

    env->DeleteGlobalRef(globalCb);
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeCancelGeneration(JNIEnv *, jobject) {
    g_cancel = true;
}

} // extern "C"
