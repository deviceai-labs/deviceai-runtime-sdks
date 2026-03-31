/**
 * deviceai_tts_jni.cpp - JNI bridge for Text-to-Speech via sherpa-onnx.
 *
 * Uses the sherpa-onnx C API (SherpaOnnxOfflineTts). Supports both VITS and
 * Kokoro voice models. When sherpa-onnx is not yet initialized as a submodule
 * the entire implementation compiles to no-op stubs (guarded by HAVE_SHERPA_ONNX).
 *
 * Shared between Android and Desktop (JVM) platforms.
 */

#include "deviceai_speech_jni.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <fstream>
#include <cstring>
#include <memory>

// ═══════════════════════════════════════════════════════════════
//                     PLATFORM-SPECIFIC LOGGING
// ═══════════════════════════════════════════════════════════════

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "DeviceAI-TTS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, "[DeviceAI-TTS INFO] "  __VA_ARGS__); fprintf(stdout, "\n")
#define LOGE(...) fprintf(stderr, "[DeviceAI-TTS ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGD(...) fprintf(stdout, "[DeviceAI-TTS DEBUG] " __VA_ARGS__); fprintf(stdout, "\n")
#endif

// ═══════════════════════════════════════════════════════════════
//                     SHERPA-ONNX BACKEND
// ═══════════════════════════════════════════════════════════════

#ifdef HAVE_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"

static const SherpaOnnxOfflineTts *g_tts = nullptr;
static std::mutex                  g_mutex;
static std::atomic<bool>           g_cancel_requested{false};
static int                         g_speaker_id = 0;

static std::string jstring_to_string(JNIEnv *env, jstring jstr) {
    if (jstr == nullptr) return "";
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    if (chars == nullptr) return "";  // OOM — GetStringUTFChars set a pending exception
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

// Write a minimal WAV file (mono 16-bit PCM).
static bool write_wav_file(const std::string &path,
                           const float *samples, int n_samples,
                           int sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open file: %s", path.c_str());
        return false;
    }

    uint32_t data_size  = n_samples * sizeof(int16_t);
    uint32_t file_size  = 36 + data_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char *>(&file_size), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    uint32_t fmt_size       = 16;
    uint16_t audio_format   = 1;  // PCM
    uint16_t num_channels   = 1;
    uint32_t sr             = sample_rate;
    uint32_t byte_rate      = sr * sizeof(int16_t);
    uint16_t block_align    = sizeof(int16_t);
    uint16_t bits_per_sample = 16;
    file.write(reinterpret_cast<char *>(&fmt_size),       4);
    file.write(reinterpret_cast<char *>(&audio_format),   2);
    file.write(reinterpret_cast<char *>(&num_channels),   2);
    file.write(reinterpret_cast<char *>(&sr),             4);
    file.write(reinterpret_cast<char *>(&byte_rate),      4);
    file.write(reinterpret_cast<char *>(&block_align),    2);
    file.write(reinterpret_cast<char *>(&bits_per_sample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<char *>(&data_size), 4);

    // Convert float [-1,1] → int16
    for (int i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = static_cast<int16_t>(v * 32767.0f);
        file.write(reinterpret_cast<char *>(&s), 2);
    }

    return true;
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitTts(
    JNIEnv *env, jobject /*thiz*/,
    jstring modelPath,
    jstring tokensPath,
    jstring dataDir,
    jstring voicesPath,
    jint    speakerId,
    jfloat  speechRate) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_tts) {
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }

    std::string model   = jstring_to_string(env, modelPath);
    std::string tokens  = jstring_to_string(env, tokensPath);
    std::string data    = jstring_to_string(env, dataDir);
    std::string voices  = jstring_to_string(env, voicesPath);

    LOGI("Initializing TTS (sherpa-onnx)");
    LOGI("Model: %s", model.c_str());
    LOGI("Tokens: %s", tokens.c_str());
    LOGI("DataDir: %s", data.c_str());
    LOGI("Voices: %s (empty = VITS)", voices.c_str());
    LOGI("SpeakerId: %d, Rate: %.2f", speakerId, speechRate);

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    bool is_kokoro = !voices.empty();

    if (is_kokoro) {
        config.model.kokoro.model      = model.c_str();
        config.model.kokoro.voices     = voices.c_str();
        config.model.kokoro.tokens     = tokens.c_str();
        config.model.kokoro.data_dir   = data.c_str();
        config.model.kokoro.length_scale = (speechRate > 0.0f) ? 1.0f / speechRate : 1.0f;
    } else {
        config.model.vits.model      = model.c_str();
        config.model.vits.tokens     = tokens.c_str();
        config.model.vits.data_dir   = data.c_str();
        config.model.vits.length_scale = (speechRate > 0.0f) ? 1.0f / speechRate : 1.0f;
        config.model.vits.noise_scale   = 0.667f;
        config.model.vits.noise_scale_w = 0.8f;
    }

    config.model.num_threads = 2;
    config.model.debug       = 0;
    config.model.provider    = "cpu";
    config.max_num_sentences = 2;

    g_tts = SherpaOnnxCreateOfflineTts(&config);
    if (!g_tts) {
        LOGE("SherpaOnnxCreateOfflineTts failed");
        return JNI_FALSE;
    }

    g_speaker_id = speakerId;
    LOGI("TTS initialized, sample_rate=%d, speaker_id=%d", SherpaOnnxOfflineTtsSampleRate(g_tts), g_speaker_id);
    return JNI_TRUE;
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(
    JNIEnv *env, jobject /*thiz*/,
    jstring text) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_tts) {
        LOGE("TTS not initialized");
        return env->NewShortArray(0);
    }

    g_cancel_requested = false;

    std::string input = jstring_to_string(env, text);
    LOGD("Synthesizing: %s", input.c_str());

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, input.c_str(), g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        LOGE("Synthesis produced no audio");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return env->NewShortArray(0);
    }

    LOGD("Synthesized %d samples", audio->n);

    // Convert float → int16
    jshortArray result = env->NewShortArray(audio->n);
    std::vector<jshort> shorts(audio->n);
    for (int i = 0; i < audio->n; ++i) {
        float v = audio->samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        shorts[i] = static_cast<jshort>(v * 32767.0f);
    }
    env->SetShortArrayRegion(result, 0, audio->n, shorts.data());

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv *env, jobject /*thiz*/,
    jstring text,
    jstring outputPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_tts) {
        LOGE("TTS not initialized");
        return JNI_FALSE;
    }

    g_cancel_requested = false;

    std::string input = jstring_to_string(env, text);
    std::string path  = jstring_to_string(env, outputPath);

    LOGD("Synthesizing to file: %s", path.c_str());

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, input.c_str(), g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        LOGE("Synthesis produced no audio");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return JNI_FALSE;
    }

    int sr = SherpaOnnxOfflineTtsSampleRate(g_tts);
    bool ok = write_wav_file(path, audio->samples, audio->n, sr);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    if (!ok) return JNI_FALSE;
    LOGI("Wrote %d samples to %s", audio->n, path.c_str());
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject /*thiz*/,
    jstring text,
    jobject callback) {

    // Resolve callback methods
    jclass cbClass = env->GetObjectClass(callback);
    if (!cbClass) return;

    jmethodID onChunk    = env->GetMethodID(cbClass, "onAudioChunk", "([S)V");
    jmethodID onComplete = env->GetMethodID(cbClass, "onComplete",   "()V");
    jmethodID onError    = env->GetMethodID(cbClass, "onError",      "(Ljava/lang/String;)V");

    if (!onChunk || !onComplete || !onError) {
        env->DeleteLocalRef(cbClass);
        return;
    }
    env->DeleteLocalRef(cbClass);

    // ── Lock: validate state, generate, copy samples, then release ──────────
    std::vector<jshort> shorts;
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (!g_tts) {
            jstring msg = env->NewStringUTF("TTS not initialized");
            if (msg) { env->CallVoidMethod(callback, onError, msg); env->DeleteLocalRef(msg); }
            return;
        }

        g_cancel_requested = false;

        std::string input = jstring_to_string(env, text);

        const SherpaOnnxGeneratedAudio *audio =
            SherpaOnnxOfflineTtsGenerate(g_tts, input.c_str(), g_speaker_id, 1.0f);

        if (!audio || audio->n == 0 || g_cancel_requested) {
            if (!g_cancel_requested) {
                jstring msg = env->NewStringUTF("No audio generated");
                if (msg) { env->CallVoidMethod(callback, onError, msg); env->DeleteLocalRef(msg); }
            }
            if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            return;
        }

        // Convert float → int16 while still under lock, before any Java call
        shorts.resize(audio->n);
        for (int i = 0; i < audio->n; ++i) {
            float v = audio->samples[i];
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            shorts[i] = static_cast<jshort>(v * 32767.0f);
        }
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    }
    // ── Lock released — safe to call into Java ───────────────────────────────

    // Send audio in chunks (4096 samples ≈ 170ms at 24kHz)
    const int CHUNK_SIZE = 4096;
    for (int i = 0; i < (int)shorts.size() && !g_cancel_requested; i += CHUNK_SIZE) {
        int chunk_len = std::min(CHUNK_SIZE, (int)shorts.size() - i);

        jshortArray chunk = env->NewShortArray(chunk_len);
        env->SetShortArrayRegion(chunk, 0, chunk_len, shorts.data() + i);
        env->CallVoidMethod(callback, onChunk, chunk);
        env->DeleteLocalRef(chunk);
    }

    if (!g_cancel_requested) {
        env->CallVoidMethod(callback, onComplete);
    }
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(
    JNIEnv * /*env*/, jobject /*thiz*/) {

    LOGI("Cancel TTS requested");
    g_cancel_requested = true;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(
    JNIEnv * /*env*/, jobject /*thiz*/) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_tts) {
        LOGI("Shutting down TTS");
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }
}

#else // !HAVE_SHERPA_ONNX — no-op stubs

static std::string jstring_to_string(JNIEnv *env, jstring jstr) {
    if (jstr == nullptr) return "";
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    if (chars == nullptr) return "";  // OOM — GetStringUTFChars set a pending exception
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitTts(
    JNIEnv * /*env*/, jobject /*thiz*/,
    jstring /*modelPath*/, jstring /*tokensPath*/,
    jstring /*dataDir*/,   jstring /*voicesPath*/,
    jint /*speakerId*/,    jfloat /*speechRate*/) {
    LOGE("TTS not available: sherpa-onnx submodule not initialised");
    return JNI_FALSE;
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(
    JNIEnv *env, jobject /*thiz*/, jstring /*text*/) {
    return env->NewShortArray(0);
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv * /*env*/, jobject /*thiz*/,
    jstring /*text*/, jstring /*outputPath*/) {
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject /*thiz*/,
    jstring /*text*/, jobject callback) {
    jclass cbClass = env->GetObjectClass(callback);
    if (!cbClass) return;
    jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cbClass);
    if (onError) {
        env->CallVoidMethod(callback, onError,
            env->NewStringUTF("TTS not available: sherpa-onnx submodule not initialised"));
    }
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(
    JNIEnv * /*env*/, jobject /*thiz*/) {}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(
    JNIEnv * /*env*/, jobject /*thiz*/) {}

#endif // HAVE_SHERPA_ONNX
