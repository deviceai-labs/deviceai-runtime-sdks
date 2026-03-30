/**
 * deviceai_whisper_jni.cpp - JNI bridge for whisper.cpp (Speech-to-Text)
 *
 * Shared between Android and Desktop (JVM) platforms.
 */

#include "deviceai_speech_jni.h"
#include "whisper.h"

#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>

// Convenience: milliseconds since an arbitrary epoch (for latency spans)
static inline long now_ms() {
    using namespace std::chrono;
    return (long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════════
//                     PLATFORM-SPECIFIC LOGGING
// ═══════════════════════════════════════════════════════════════

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "SpeechKMP-STT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, "[SpeechKMP-STT INFO] "  __VA_ARGS__); fprintf(stdout, "\n")
#define LOGE(...) fprintf(stderr, "[SpeechKMP-STT ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGD(...) fprintf(stdout, "[SpeechKMP-STT DEBUG] " __VA_ARGS__); fprintf(stdout, "\n")
#endif

// ═══════════════════════════════════════════════════════════════
//                          GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

static struct whisper_context *g_ctx = nullptr;
// NOTE: g_params does NOT store language — the language pointer is set fresh
// in every transcription call to avoid dangling-pointer bugs after re-init.
static struct whisper_full_params g_params;
static std::mutex g_mutex;
static std::atomic<bool> g_cancel_requested{false};

// Configuration — all writes happen inside the mutex (initStt).
// Reads from transcription functions are also mutex-protected.
static std::string g_language     = "en";
static std::atomic<bool>  g_translate{false};
static std::atomic<int>   g_max_threads{4};
static std::atomic<bool>  g_use_gpu{true};
static std::atomic<bool>  g_use_vad{true};
static std::atomic<bool>  g_single_segment{true};
static std::atomic<bool>  g_no_context{true};

// ═══════════════════════════════════════════════════════════════
//                      HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

static std::string jstring_to_string(JNIEnv *env, jstring jstr) {
    if (jstr == nullptr) return "";
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) return "";
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

static bool read_wav_file(const std::string &path, std::vector<float> &samples, int &sample_rate) {
    sample_rate = 0; // initialise so callers always see a defined value
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open WAV file: %s", path.c_str());
        return false;
    }

    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        LOGE("Invalid WAV file: missing RIFF header");
        return false;
    }

    file.seekg(4, std::ios::cur); // skip file size

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        LOGE("Invalid WAV file: missing WAVE header");
        return false;
    }

    while (file.good()) {
        char chunk_id[4];
        file.read(chunk_id, 4);

        uint32_t chunk_size = 0;
        file.read(reinterpret_cast<char *>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format = 0, num_channels = 0;
            uint32_t sr = 0;
            file.read(reinterpret_cast<char *>(&audio_format), 2);
            file.read(reinterpret_cast<char *>(&num_channels), 2);
            file.read(reinterpret_cast<char *>(&sr), 4);
            sample_rate = (int)sr;
            // skip the rest of the fmt chunk
            if (chunk_size > 8) file.seekg(chunk_size - 8, std::ios::cur);
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            std::vector<int16_t> pcm(chunk_size / 2);
            file.read(reinterpret_cast<char *>(pcm.data()), chunk_size);

            samples.resize(pcm.size());
            for (size_t i = 0; i < pcm.size(); i++) {
                samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
            }
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    return !samples.empty() && sample_rate > 0;
}

static bool resample_to_16k(const std::vector<float> &input, int input_rate, std::vector<float> &output) {
    if (input.empty()) return false;
    if (input_rate == WHISPER_SAMPLE_RATE) {
        output = input;
        return true;
    }
    if (input_rate <= 0) return false;

    double ratio       = static_cast<double>(WHISPER_SAMPLE_RATE) / input_rate;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    output.resize(output_size);

    for (size_t i = 0; i < output_size; i++) {
        double src_idx = i / ratio;
        size_t idx0    = static_cast<size_t>(src_idx);
        size_t idx1    = std::min(idx0 + 1, input.size() - 1);
        double frac    = src_idx - idx0;
        output[i]      = static_cast<float>(input[idx0] * (1.0 - frac) + input[idx1] * frac);
    }
    return true;
}

// Build a fresh whisper_full_params from the current global configuration.
// Sets the language pointer directly into the provided std::string (caller
// must keep that string alive for the duration of the whisper call).
static struct whisper_full_params make_params(const std::string &language, float audio_sec) {
    struct whisper_full_params p = g_params; // copy base settings
    p.language     = language.c_str();       // point into caller-owned string (no dangling ptr)
    p.audio_ctx    = 0;                      // full encoder context — safe for all stock models

    // Cap output tokens to prevent beam-search repetition loops.
    // After transcribing the actual speech the decoder keeps looping on
    // silence until max_tokens stops it. Formula: 3 tok/sec + 32 headroom.
    p.max_tokens   = std::max(32, (int)(audio_sec * 3.0f) + 32);
    return p;
}

// Energy-based VAD: trims leading/trailing silence from audio.
// Returns false if no speech detected (caller should skip inference).
// audio is trimmed in-place.
static bool vad_trim(std::vector<float> &audio) {
    const int FRAME = 480;  // 30 ms at 16 kHz
    const int PAD   = 10;   // ~300 ms padding around speech

    if ((int)audio.size() < FRAME) return false; // too short to analyse

    int n_frames = (int)audio.size() / FRAME;

    std::vector<float> frame_rms(n_frames);
    for (int f = 0; f < n_frames; ++f) {
        const float *p = audio.data() + f * FRAME;
        float sum = 0.0f;
        for (int i = 0; i < FRAME; ++i) sum += p[i] * p[i];
        frame_rms[f] = std::sqrt(sum / FRAME);
    }

    std::vector<float> sorted_rms = frame_rms;
    std::sort(sorted_rms.begin(), sorted_rms.end());
    float noise_floor = sorted_rms[n_frames / 10]; // 10th-percentile frame RMS
    float threshold   = std::max(0.02f, noise_floor * 4.0f);

    int first_speech = -1, last_speech = -1;
    for (int f = 0; f < n_frames; ++f) {
        if (frame_rms[f] >= threshold) {
            if (first_speech < 0) first_speech = f;
            last_speech = f;
        }
    }

    if (first_speech < 0) return false; // no speech

    int s = std::max(0,        first_speech - PAD) * FRAME;
    int e = std::min(n_frames, last_speech  + PAD + 1) * FRAME;

    float before = (float)audio.size() / WHISPER_SAMPLE_RATE;
    float after  = (float)(e - s)      / WHISPER_SAMPLE_RATE;
    LOGI("[VAD] trimmed %.2fs → %.2fs (threshold=%.4f)", before, after, threshold);

    audio = std::vector<float>(audio.begin() + s, audio.begin() + e);
    return true;
}

// Helper: call onError callback and delete the message local ref.
static void call_on_error(JNIEnv *env, jobject callback, jmethodID onError, const char *msg) {
    jstring s = env->NewStringUTF(msg);
    env->CallVoidMethod(callback, onError, s);
    env->DeleteLocalRef(s);
}

// ═══════════════════════════════════════════════════════════════
//                        JNI FUNCTIONS
// ═══════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitStt(
    JNIEnv *env, jobject /*thiz*/,
    jstring modelPath,
    jstring language,
    jboolean translate,
    jint maxThreads,
    jboolean useGpu,
    jboolean useVad,
    jboolean singleSegment,
    jboolean noContext) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx != nullptr) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }

    std::string path = jstring_to_string(env, modelPath);
    g_language       = jstring_to_string(env, language);
    g_translate      = translate;
    g_max_threads    = maxThreads;
    g_use_gpu        = useGpu;
    g_use_vad        = useVad;
    g_single_segment = singleSegment;
    g_no_context     = noContext;

    LOGI("Initializing Whisper: model=%s language=%s threads=%d gpu=%d vad=%d",
         path.c_str(), g_language.c_str(), (int)g_max_threads,
         (int)g_use_gpu, (int)g_use_vad);

    struct whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = g_use_gpu;

    g_ctx = whisper_init_from_file_with_params(path.c_str(), ctx_params);
    if (g_ctx == nullptr) {
        LOGE("Failed to initialize Whisper model");
        return JNI_FALSE;
    }

    // Store base params (language pointer NOT stored here — it would dangle
    // after g_language is modified by a second initStt call. It is set fresh
    // in make_params() before every inference call.)
    g_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    g_params.translate       = g_translate;
    g_params.n_threads       = g_max_threads;
    g_params.no_timestamps   = false;
    g_params.print_special   = false;
    g_params.print_progress  = false;
    g_params.print_realtime  = false;
    g_params.print_timestamps = false;
    g_params.single_segment  = g_single_segment;
    g_params.no_context      = g_no_context;

    LOGI("Whisper model initialized successfully");
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribe(
    JNIEnv *env, jobject /*thiz*/,
    jstring audioPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        return env->NewStringUTF("");
    }

    g_cancel_requested = false;

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(jstring_to_string(env, audioPath), samples, sample_rate)) {
        return env->NewStringUTF("");
    }

    std::vector<float> samples_16k;
    if (!resample_to_16k(samples, sample_rate, samples_16k)) {
        LOGE("Failed to resample audio");
        return env->NewStringUTF("");
    }

    float audio_sec = (float)samples_16k.size() / WHISPER_SAMPLE_RATE;
    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        LOGE("Failed to allocate whisper state");
        return env->NewStringUTF("");
    }

    if (whisper_full_with_state(g_ctx, state, params, samples_16k.data(), (int)samples_16k.size()) != 0) {
        whisper_free_state(state);
        LOGE("Whisper inference failed");
        return env->NewStringUTF("");
    }

    std::string result;
    int n_segments = whisper_full_n_segments_from_state(state);
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        if (text) result += text;
    }
    whisper_free_state(state);

    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jobject JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeDetailed(
    JNIEnv *env, jobject /*thiz*/,
    jstring audioPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    // Resolve classes up front so we can build an empty result in all error paths.
    jclass resultClass  = env->FindClass("dev/deviceai/TranscriptionResult");
    jclass segmentClass = env->FindClass("dev/deviceai/Segment");
    jclass listClass    = env->FindClass("java/util/ArrayList");

    if (!resultClass || !segmentClass || !listClass) {
        if (resultClass)  env->DeleteLocalRef(resultClass);
        if (segmentClass) env->DeleteLocalRef(segmentClass);
        if (listClass)    env->DeleteLocalRef(listClass);
        return nullptr;
    }

    jmethodID resultCtor  = env->GetMethodID(resultClass,  "<init>",
        "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
    jmethodID listCtor    = env->GetMethodID(listClass,    "<init>",  "()V");
    jmethodID listAdd     = env->GetMethodID(listClass,    "add",     "(Ljava/lang/Object;)Z");
    jmethodID segmentCtor = env->GetMethodID(segmentClass, "<init>",  "(Ljava/lang/String;JJ)V");

    // Helper: build an empty TranscriptionResult
    auto make_empty = [&]() -> jobject {
        jobject empty_list = env->NewObject(listClass, listCtor);
        jstring empty_str  = env->NewStringUTF("");
        jstring lang_str   = env->NewStringUTF(g_language.c_str());
        jobject r = env->NewObject(resultClass, resultCtor,
                                   empty_str, empty_list, lang_str, (jlong)0);
        env->DeleteLocalRef(empty_str);
        env->DeleteLocalRef(lang_str);
        env->DeleteLocalRef(empty_list);
        env->DeleteLocalRef(segmentClass);
        env->DeleteLocalRef(listClass);
        env->DeleteLocalRef(resultClass);
        return r;
    };

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        return make_empty();
    }

    g_cancel_requested = false;

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(jstring_to_string(env, audioPath), samples, sample_rate)) {
        return make_empty();
    }

    std::vector<float> samples_16k;
    if (!resample_to_16k(samples, sample_rate, samples_16k)) {
        LOGE("Failed to resample audio");
        return make_empty();
    }

    float audio_sec = (float)samples_16k.size() / WHISPER_SAMPLE_RATE;
    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        LOGE("Failed to allocate whisper state");
        return make_empty();
    }

    if (whisper_full_with_state(g_ctx, state, params, samples_16k.data(), (int)samples_16k.size()) != 0) {
        whisper_free_state(state);
        LOGE("Whisper inference failed");
        return make_empty();
    }

    // Build result
    std::string fullText;
    int n_segments = whisper_full_n_segments_from_state(state);

    jobject segmentList = env->NewObject(listClass, listCtor);

    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        int64_t t0       = whisper_full_get_segment_t0_from_state(state, i) * 10;
        int64_t t1       = whisper_full_get_segment_t1_from_state(state, i) * 10;

        if (text) {
            fullText += text;

            jstring segText = env->NewStringUTF(text);
            jobject segment = env->NewObject(segmentClass, segmentCtor,
                                             segText, (jlong)t0, (jlong)t1);
            env->DeleteLocalRef(segText);
            env->CallBooleanMethod(segmentList, listAdd, segment);
            env->DeleteLocalRef(segment);
        }
    }
    whisper_free_state(state);

    jlong durationMs   = (jlong)(samples_16k.size() * 1000 / WHISPER_SAMPLE_RATE);
    jstring fullStr    = env->NewStringUTF(fullText.c_str());
    jstring langStr    = env->NewStringUTF(g_language.c_str());
    jobject result     = env->NewObject(resultClass, resultCtor,
                                        fullStr, segmentList, langStr, durationMs);
    env->DeleteLocalRef(fullStr);
    env->DeleteLocalRef(langStr);
    env->DeleteLocalRef(segmentList);
    env->DeleteLocalRef(segmentClass);
    env->DeleteLocalRef(listClass);
    env->DeleteLocalRef(resultClass);
    return result;
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeAudio(
    JNIEnv *env, jobject /*thiz*/,
    jfloatArray samples) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        return env->NewStringUTF("");
    }

    g_cancel_requested = false;

    long t_jni_start = now_ms();

    jsize len    = env->GetArrayLength(samples);
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);
    std::vector<float> audio(data, data + len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    long t_copy_done = now_ms();
    float audio_sec  = (float)audio.size() / WHISPER_SAMPLE_RATE;
    LOGI("[LATENCY] JNI copy: %ld ms  (%d samples = %.2f s)",
         t_copy_done - t_jni_start, (int)audio.size(), audio_sec);

    // ── VAD ────────────────────────────────────────────────────────
    if (g_use_vad) {
        if (!vad_trim(audio)) {
            LOGI("[VAD] no speech detected — skipping transcription");
            return env->NewStringUTF("");
        }
        audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;
    }

    // ── Inference ──────────────────────────────────────────────────
    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    LOGI("[WHISPER-CFG] audio_ctx=full max_tokens=%d (%.2fs after VAD)",
         params.max_tokens, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        LOGE("Failed to allocate whisper state");
        return env->NewStringUTF("");
    }

    long t_infer_start = now_ms();
    if (whisper_full_with_state(g_ctx, state, params, audio.data(), (int)audio.size()) != 0) {
        whisper_free_state(state);
        LOGE("Whisper inference failed");
        return env->NewStringUTF("");
    }
    long t_infer_done = now_ms();

    LOGI("[LATENCY] whisper_full: %ld ms  (RTF=%.2fx)",
         t_infer_done - t_infer_start,
         (float)(t_infer_done - t_infer_start) / (audio_sec * 1000.0f));

    std::string result;
    int n_segments = whisper_full_n_segments_from_state(state);
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        if (text) result += text;
    }
    whisper_free_state(state);

    LOGI("[LATENCY] total: %ld ms", now_ms() - t_jni_start);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeStream(
    JNIEnv *env, jobject /*thiz*/,
    jfloatArray samples,
    jobject callback) {

    std::lock_guard<std::mutex> lock(g_mutex);

    // ── Resolve callback methods ────────────────────────────────────
    jclass cbClass   = env->GetObjectClass(callback);
    jmethodID onPartial = env->GetMethodID(cbClass, "onPartialResult", "(Ljava/lang/String;)V");
    jmethodID onFinal   = env->GetMethodID(cbClass, "onFinalResult",
                                            "(Ldev/deviceai/TranscriptionResult;)V");
    jmethodID onError   = env->GetMethodID(cbClass, "onError",         "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cbClass);

    if (!onPartial || !onFinal || !onError) return; // methods not found

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        call_on_error(env, callback, onError, "Whisper not initialized");
        return;
    }

    g_cancel_requested = false;

    // ── Copy samples ────────────────────────────────────────────────
    jsize len = env->GetArrayLength(samples);
    if (len == 0) {
        call_on_error(env, callback, onError, "Empty audio");
        return;
    }
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);
    std::vector<float> audio(data, data + len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    float audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;

    // ── VAD ─────────────────────────────────────────────────────────
    if (g_use_vad) {
        if (!vad_trim(audio)) {
            LOGI("[VAD] no speech detected");
            audio_sec = 0.0f;
            // Fall through to produce an empty result (not an error)
        } else {
            audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;
        }
    }

    // ── Class resolution for result construction ─────────────────────
    jclass resultClass  = env->FindClass("dev/deviceai/TranscriptionResult");
    jclass segmentClass = env->FindClass("dev/deviceai/Segment");
    jclass listClass    = env->FindClass("java/util/ArrayList");

    if (!resultClass || !segmentClass || !listClass) {
        if (resultClass)  env->DeleteLocalRef(resultClass);
        if (segmentClass) env->DeleteLocalRef(segmentClass);
        if (listClass)    env->DeleteLocalRef(listClass);
        call_on_error(env, callback, onError, "Failed to find JNI classes");
        return;
    }

    jmethodID resultCtor  = env->GetMethodID(resultClass,  "<init>",
        "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
    jmethodID listCtor    = env->GetMethodID(listClass,    "<init>",  "()V");
    jmethodID listAdd     = env->GetMethodID(listClass,    "add",     "(Ljava/lang/Object;)Z");
    jmethodID segmentCtor = env->GetMethodID(segmentClass, "<init>",  "(Ljava/lang/String;JJ)V");

    // ── Empty-result helper ──────────────────────────────────────────
    auto fire_empty_final = [&]() {
        jobject empty_list = env->NewObject(listClass, listCtor);
        jstring empty_str  = env->NewStringUTF("");
        jstring lang_str   = env->NewStringUTF(g_language.c_str());
        jobject r = env->NewObject(resultClass, resultCtor,
                                   empty_str, empty_list, lang_str, (jlong)0);
        env->DeleteLocalRef(empty_str);
        env->DeleteLocalRef(lang_str);
        env->DeleteLocalRef(empty_list);
        env->CallVoidMethod(callback, onFinal, r);
        env->DeleteLocalRef(r);
    };

    // If VAD detected no speech, return an empty final result now
    if (audio.empty()) {
        fire_empty_final();
        env->DeleteLocalRef(resultClass);
        env->DeleteLocalRef(segmentClass);
        env->DeleteLocalRef(listClass);
        return;
    }

    // ── Inference — isolated state (no g_ctx mutation) ──────────────
    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    LOGI("[WHISPER-CFG] stream: audio_ctx=full max_tokens=%d (%.2fs)", params.max_tokens, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        LOGE("Failed to allocate whisper state");
        call_on_error(env, callback, onError, "Failed to allocate whisper state");
        env->DeleteLocalRef(resultClass);
        env->DeleteLocalRef(segmentClass);
        env->DeleteLocalRef(listClass);
        return;
    }

    if (whisper_full_with_state(g_ctx, state, params, audio.data(), (int)audio.size()) != 0) {
        whisper_free_state(state);
        LOGE("Whisper inference failed");
        call_on_error(env, callback, onError, "Transcription failed");
        env->DeleteLocalRef(resultClass);
        env->DeleteLocalRef(segmentClass);
        env->DeleteLocalRef(listClass);
        return;
    }

    // ── Collect segments and fire partial callbacks ──────────────────
    jobject segmentList = env->NewObject(listClass, listCtor);
    std::string fullText;
    int n_segments = whisper_full_n_segments_from_state(state);

    for (int i = 0; i < n_segments && !g_cancel_requested; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        int64_t t0       = whisper_full_get_segment_t0_from_state(state, i) * 10;
        int64_t t1       = whisper_full_get_segment_t1_from_state(state, i) * 10;

        if (text) {
            fullText += text;

            jstring partialStr = env->NewStringUTF(fullText.c_str());
            env->CallVoidMethod(callback, onPartial, partialStr);
            env->DeleteLocalRef(partialStr);

            // If the callback threw, stop iterating
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }

            jstring segText = env->NewStringUTF(text);
            jobject segment = env->NewObject(segmentClass, segmentCtor,
                                             segText, (jlong)t0, (jlong)t1);
            env->DeleteLocalRef(segText);
            env->CallBooleanMethod(segmentList, listAdd, segment);
            env->DeleteLocalRef(segment);
        }
    }
    whisper_free_state(state);
    env->DeleteLocalRef(segmentClass);
    env->DeleteLocalRef(listClass);

    jlong durationMs = (jlong)(audio.size() * 1000 / WHISPER_SAMPLE_RATE);
    jstring fullStr  = env->NewStringUTF(fullText.c_str());
    jstring langStr  = env->NewStringUTF(g_language.c_str());
    jobject result   = env->NewObject(resultClass, resultCtor,
                                      fullStr, segmentList, langStr, durationMs);
    env->DeleteLocalRef(fullStr);
    env->DeleteLocalRef(langStr);
    env->DeleteLocalRef(segmentList);
    env->DeleteLocalRef(resultClass);

    if (!g_cancel_requested) {
        env->CallVoidMethod(callback, onFinal, result);
    }
    env->DeleteLocalRef(result);
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelStt(
    JNIEnv * /*env*/, jobject /*thiz*/) {

    LOGI("Cancel STT requested");
    g_cancel_requested = true;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownStt(
    JNIEnv * /*env*/, jobject /*thiz*/) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx != nullptr) {
        LOGI("Shutting down Whisper");
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }
}
