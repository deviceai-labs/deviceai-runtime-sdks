/**
 * whisper_jni.cpp - JNI bridge for whisper.cpp (Speech-to-Text)
 *
 * Shared between Android and Desktop (JVM) platforms.
 */

#include "speech_jni.h"
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
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, "[SpeechKMP-STT INFO] " __VA_ARGS__); fprintf(stdout, "\n")
#define LOGE(...) fprintf(stderr, "[SpeechKMP-STT ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGD(...) fprintf(stdout, "[SpeechKMP-STT DEBUG] " __VA_ARGS__); fprintf(stdout, "\n")
#endif

// ═══════════════════════════════════════════════════════════════
//                          GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

static struct whisper_context *g_ctx = nullptr;
static struct whisper_full_params g_params;
static std::mutex g_mutex;
static std::atomic<bool> g_cancel_requested{false};

// Configuration
static std::string g_language = "en";
static std::atomic<bool> g_translate{false};
static std::atomic<int> g_max_threads{4};
static std::atomic<bool> g_use_gpu{true};
static std::atomic<bool> g_use_vad{true};
static std::atomic<bool> g_single_segment{true};
static std::atomic<bool> g_no_context{true};

// ═══════════════════════════════════════════════════════════════
//                      HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

static std::string jstring_to_string(JNIEnv *env, jstring jstr) {
    if (jstr == nullptr) return "";
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

static bool read_wav_file(const std::string &path, std::vector<float> &samples, int &sample_rate) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open WAV file: %s", path.c_str());
        return false;
    }

    // Read WAV header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        LOGE("Invalid WAV file: missing RIFF header");
        return false;
    }

    file.seekg(4, std::ios::cur); // Skip file size

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        LOGE("Invalid WAV file: missing WAVE header");
        return false;
    }

    // Find fmt chunk
    while (file.good()) {
        char chunk_id[4];
        file.read(chunk_id, 4);

        uint32_t chunk_size;
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format;
            file.read(reinterpret_cast<char*>(&audio_format), 2);

            uint16_t num_channels;
            file.read(reinterpret_cast<char*>(&num_channels), 2);

            uint32_t sr;
            file.read(reinterpret_cast<char*>(&sr), 4);
            sample_rate = sr;

            file.seekg(chunk_size - 8, std::ios::cur);
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            // Read audio data
            std::vector<int16_t> pcm(chunk_size / 2);
            file.read(reinterpret_cast<char*>(pcm.data()), chunk_size);

            // Convert to float
            samples.resize(pcm.size());
            for (size_t i = 0; i < pcm.size(); i++) {
                samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
            }
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    return !samples.empty();
}

static bool resample_to_16k(const std::vector<float> &input, int input_rate, std::vector<float> &output) {
    if (input_rate == WHISPER_SAMPLE_RATE) {
        output = input;
        return true;
    }

    // Simple linear interpolation resampling
    double ratio = static_cast<double>(WHISPER_SAMPLE_RATE) / input_rate;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    output.resize(output_size);

    for (size_t i = 0; i < output_size; i++) {
        double src_idx = i / ratio;
        size_t idx0 = static_cast<size_t>(src_idx);
        size_t idx1 = std::min(idx0 + 1, input.size() - 1);
        double frac = src_idx - idx0;
        output[i] = static_cast<float>(input[idx0] * (1.0 - frac) + input[idx1] * frac);
    }

    return true;
}

// Callback for streaming transcription
struct StreamCallbackData {
    JNIEnv *env;
    jobject callback;
    jmethodID onPartialResult;
    jmethodID onFinalResult;
    jmethodID onError;
};

// ═══════════════════════════════════════════════════════════════
//                        JNI FUNCTIONS
// ═══════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitStt(
    JNIEnv *env, jobject thiz,
    jstring modelPath,
    jstring language,
    jboolean translate,
    jint maxThreads,
    jboolean useGpu,
    jboolean useVad,
    jboolean singleSegment,
    jboolean noContext) {

    std::lock_guard<std::mutex> lock(g_mutex);

    // Shutdown existing context if any
    if (g_ctx != nullptr) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }

    std::string path = jstring_to_string(env, modelPath);
    g_language = jstring_to_string(env, language);
    g_translate = translate;
    g_max_threads = maxThreads;
    g_use_gpu = useGpu;
    g_use_vad = useVad;
    g_single_segment = singleSegment;
    g_no_context = noContext;

    LOGI("Initializing Whisper with model: %s", path.c_str());
    LOGI("Config: language=%s, translate=%d, threads=%d, gpu=%d, vad=%d",
         g_language.c_str(), (int)g_translate, (int)g_max_threads, (int)g_use_gpu, (int)g_use_vad);

    // Initialize context parameters
    struct whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = g_use_gpu;

    // Load model
    g_ctx = whisper_init_from_file_with_params(path.c_str(), ctx_params);
    if (g_ctx == nullptr) {
        LOGE("Failed to initialize Whisper model");
        return JNI_FALSE;
    }

    // Setup default full params
    g_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    g_params.language = g_language.c_str();
    g_params.translate = g_translate;
    g_params.n_threads = g_max_threads;
    g_params.no_timestamps = false;
    g_params.print_special = false;
    g_params.print_progress = false;
    g_params.print_realtime = false;
    g_params.print_timestamps = false;
    g_params.single_segment   = g_single_segment;
    g_params.no_context       = g_no_context;

    LOGI("Whisper model initialized successfully");
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribe(
    JNIEnv *env, jobject thiz,
    jstring audioPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        return env->NewStringUTF("");
    }

    g_cancel_requested = false;

    std::string path = jstring_to_string(env, audioPath);
    LOGD("Transcribing file: %s", path.c_str());

    // Read WAV file
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav_file(path, samples, sample_rate)) {
        return env->NewStringUTF("");
    }

    // Resample to 16kHz if needed
    std::vector<float> samples_16k;
    if (!resample_to_16k(samples, sample_rate, samples_16k)) {
        LOGE("Failed to resample audio");
        return env->NewStringUTF("");
    }

    // Run inference
    if (whisper_full(g_ctx, g_params, samples_16k.data(), samples_16k.size()) != 0) {
        LOGE("Whisper inference failed");
        return env->NewStringUTF("");
    }

    // Collect results
    std::string result;
    int n_segments = whisper_full_n_segments(g_ctx);
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text(g_ctx, i);
        if (text) {
            result += text;
        }
    }

    LOGD("Transcription result: %s", result.c_str());
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jobject JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeDetailed(
    JNIEnv *env, jobject thiz,
    jstring audioPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    // Get class references
    jclass resultClass = env->FindClass("dev/deviceai/TranscriptionResult");
    jclass segmentClass = env->FindClass("dev/deviceai/Segment");
    jclass listClass = env->FindClass("java/util/ArrayList");

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        // Return empty result
        jmethodID resultCtor = env->GetMethodID(resultClass, "<init>",
            "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
        jmethodID listCtor = env->GetMethodID(listClass, "<init>", "()V");
        jobject emptyList = env->NewObject(listClass, listCtor);
        return env->NewObject(resultClass, resultCtor,
            env->NewStringUTF(""), emptyList, env->NewStringUTF("en"), 0L);
    }

    g_cancel_requested = false;

    std::string path = jstring_to_string(env, audioPath);

    // Read WAV file
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav_file(path, samples, sample_rate)) {
        jmethodID resultCtor = env->GetMethodID(resultClass, "<init>",
            "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
        jmethodID listCtor = env->GetMethodID(listClass, "<init>", "()V");
        jobject emptyList = env->NewObject(listClass, listCtor);
        return env->NewObject(resultClass, resultCtor,
            env->NewStringUTF(""), emptyList, env->NewStringUTF("en"), 0L);
    }

    // Resample to 16kHz if needed
    std::vector<float> samples_16k;
    resample_to_16k(samples, sample_rate, samples_16k);

    // Run inference
    if (whisper_full(g_ctx, g_params, samples_16k.data(), samples_16k.size()) != 0) {
        LOGE("Whisper inference failed");
        jmethodID resultCtor = env->GetMethodID(resultClass, "<init>",
            "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
        jmethodID listCtor = env->GetMethodID(listClass, "<init>", "()V");
        jobject emptyList = env->NewObject(listClass, listCtor);
        return env->NewObject(resultClass, resultCtor,
            env->NewStringUTF(""), emptyList, env->NewStringUTF("en"), 0L);
    }

    // Build result
    std::string fullText;
    int n_segments = whisper_full_n_segments(g_ctx);

    // Create ArrayList
    jmethodID listCtor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAdd = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jobject segmentList = env->NewObject(listClass, listCtor);

    // Create Segment constructor
    jmethodID segmentCtor = env->GetMethodID(segmentClass, "<init>", "(Ljava/lang/String;JJ)V");

    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text(g_ctx, i);
        int64_t t0 = whisper_full_get_segment_t0(g_ctx, i) * 10; // Convert to ms
        int64_t t1 = whisper_full_get_segment_t1(g_ctx, i) * 10;

        if (text) {
            fullText += text;

            // Create Segment object
            jobject segment = env->NewObject(segmentClass, segmentCtor,
                env->NewStringUTF(text), (jlong)t0, (jlong)t1);
            env->CallBooleanMethod(segmentList, listAdd, segment);
            env->DeleteLocalRef(segment);
        }
    }

    // Calculate duration
    jlong durationMs = samples_16k.size() * 1000 / WHISPER_SAMPLE_RATE;

    // Create TranscriptionResult
    jmethodID resultCtor = env->GetMethodID(resultClass, "<init>",
        "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");

    return env->NewObject(resultClass, resultCtor,
        env->NewStringUTF(fullText.c_str()),
        segmentList,
        env->NewStringUTF(g_language.c_str()),
        durationMs);
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeAudio(
    JNIEnv *env, jobject thiz,
    jfloatArray samples) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx == nullptr) {
        LOGE("Whisper not initialized");
        return env->NewStringUTF("");
    }

    g_cancel_requested = false;

    long t_jni_start = now_ms();

    // Get samples
    jsize len = env->GetArrayLength(samples);
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);

    // Copy to vector
    std::vector<float> audio(data, data + len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    long t_jni_copy_done = now_ms();
    float audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;
    LOGI("[LATENCY] JNI array copy:   %ld ms  (%d samples = %.2f s of audio)",
         t_jni_copy_done - t_jni_start, (int)audio.size(), audio_sec);

    // ── VAD: trim leading/trailing silence ─────────────────────────
    // Whisper loops on trailing silence, re-generating the same tokens.
    // Energy-based VAD finds the actual speech region and crops to it,
    // so Whisper only sees real speech content.
    //
    // Adaptive threshold: measure the noise floor of this specific recording
    // (10th-percentile frame RMS) so the threshold automatically scales on
    // noisy environments (emulators, cheap mics) without cutting real speech.
    if (g_use_vad) {
        const int FRAME = 480;   // 30 ms at 16 kHz
        const int PAD   = 10;    // padding frames (~300 ms) around detected speech

        int n_frames = (int)audio.size() / FRAME;

        // Compute per-frame RMS
        std::vector<float> frame_rms(n_frames);
        for (int f = 0; f < n_frames; ++f) {
            const float *p = audio.data() + f * FRAME;
            float sum = 0.0f;
            for (int i = 0; i < FRAME; ++i) sum += p[i] * p[i];
            frame_rms[f] = std::sqrt(sum / FRAME);
        }

        // Noise floor = 10th-percentile RMS (quietest ~10% of frames)
        std::vector<float> sorted_rms = frame_rms;
        std::sort(sorted_rms.begin(), sorted_rms.end());
        float noise_floor = sorted_rms[std::max(0, n_frames / 10)];

        // Speech threshold: 4× above noise floor, minimum 0.02
        float threshold = std::max(0.02f, noise_floor * 4.0f);
        LOGI("[VAD] noise_floor=%.4f  threshold=%.4f", noise_floor, threshold);

        int first_speech = -1, last_speech = -1;
        for (int f = 0; f < n_frames; ++f) {
            if (frame_rms[f] >= threshold) {
                if (first_speech < 0) first_speech = f;
                last_speech = f;
            }
        }

        if (first_speech >= 0) {
            int s = std::max(0,        first_speech - PAD) * FRAME;
            int e = std::min(n_frames, last_speech  + PAD + 1) * FRAME;
            float trimmed_sec = (float)(e - s) / WHISPER_SAMPLE_RATE;
            LOGI("[VAD] speech frames %d–%d  trimmed %.2fs → %.2fs",
                 first_speech, last_speech, audio_sec, trimmed_sec);
            audio = std::vector<float>(audio.begin() + s, audio.begin() + e);
        } else {
            LOGI("[VAD] no speech detected — skipping transcription");
            return env->NewStringUTF("");
        }
    }

    // ── Whisper inference ──────────────────────────────────────────
    // Auto-derive audio_ctx from actual sample count so the encoder's attention
    // window matches the real audio length instead of always running over 30s.
    // Formula: each whisper frame = 160 samples; encoder conv halves it → /320.
    struct whisper_full_params params = g_params;
    int auto_ctx = (static_cast<int>(audio.size()) + 319) / 320;
    params.audio_ctx = std::min(auto_ctx, 1500);
    audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;
    LOGI("[WHISPER-CFG] audio_ctx set to %d (%.2fs after VAD trim)",
         params.audio_ctx, audio_sec);

    // ── Fresh state per call ───────────────────────────────────────
    // whisper_full() writes into g_ctx's internal state and result_all buffer,
    // which accumulates across calls and leaks into subsequent inferences even
    // with no_context=true. A fresh whisper_state isolates every call completely.
    struct whisper_state *state = whisper_init_state(g_ctx);
    if (state == nullptr) {
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
    LOGI("[LATENCY] whisper_full():   %ld ms  (RTF = %.2fx)",
         t_infer_done - t_infer_start,
         (float)(t_infer_done - t_infer_start) / (audio_sec * 1000.0f));

    LOGI("[WHISPER-CFG] n_threads=%d  single_segment=%d  no_context=%d  gpu=%d",
         (int)params.n_threads, (int)params.single_segment,
         (int)params.no_context, (int)g_use_gpu.load());
    whisper_print_timings(g_ctx);

    // ── Collect text segments ──────────────────────────────────────
    long t_collect_start = now_ms();

    std::string result;
    int n_segments = whisper_full_n_segments_from_state(state);
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        if (text) {
            result += text;
        }
    }

    whisper_free_state(state);

    long t_collect_done = now_ms();
    LOGI("[LATENCY] collect segments: %ld ms  (%d segments)",
         t_collect_done - t_collect_start, n_segments);
    LOGI("[LATENCY] ── TOTAL C++ ──   %ld ms",
         t_collect_done - t_jni_start);

    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeStream(
    JNIEnv *env, jobject thiz,
    jfloatArray samples,
    jobject callback) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx == nullptr) {
        // Call onError
        jclass cbClass = env->GetObjectClass(callback);
        jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");
        env->CallVoidMethod(callback, onError, env->NewStringUTF("Whisper not initialized"));
        return;
    }

    g_cancel_requested = false;

    // Get callback methods
    jclass cbClass = env->GetObjectClass(callback);
    jmethodID onPartial = env->GetMethodID(cbClass, "onPartialResult", "(Ljava/lang/String;)V");
    jmethodID onFinal = env->GetMethodID(cbClass, "onFinalResult", "(Ldev/deviceai/TranscriptionResult;)V");
    jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");

    // Get samples
    jsize len = env->GetArrayLength(samples);
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);
    std::vector<float> audio(data, data + len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    // Setup progress callback for partial results
    struct whisper_full_params params = g_params;

    // For now, we'll run full transcription and report result
    // Real streaming would require VAD and chunked processing

    if (whisper_full(g_ctx, params, audio.data(), audio.size()) != 0) {
        env->CallVoidMethod(callback, onError, env->NewStringUTF("Transcription failed"));
        return;
    }

    // Build result and call callbacks
    std::string fullText;
    int n_segments = whisper_full_n_segments(g_ctx);

    // Get class references for result
    jclass resultClass = env->FindClass("dev/deviceai/TranscriptionResult");
    jclass segmentClass = env->FindClass("dev/deviceai/Segment");
    jclass listClass = env->FindClass("java/util/ArrayList");

    jmethodID listCtor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAdd = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jobject segmentList = env->NewObject(listClass, listCtor);

    jmethodID segmentCtor = env->GetMethodID(segmentClass, "<init>", "(Ljava/lang/String;JJ)V");

    for (int i = 0; i < n_segments; i++) {
        if (g_cancel_requested) {
            env->CallVoidMethod(callback, onError, env->NewStringUTF("Cancelled"));
            return;
        }

        const char *text = whisper_full_get_segment_text(g_ctx, i);
        int64_t t0 = whisper_full_get_segment_t0(g_ctx, i) * 10;
        int64_t t1 = whisper_full_get_segment_t1(g_ctx, i) * 10;

        if (text) {
            fullText += text;

            // Call partial result callback
            env->CallVoidMethod(callback, onPartial, env->NewStringUTF(fullText.c_str()));

            // Create Segment
            jobject segment = env->NewObject(segmentClass, segmentCtor,
                env->NewStringUTF(text), (jlong)t0, (jlong)t1);
            env->CallBooleanMethod(segmentList, listAdd, segment);
            env->DeleteLocalRef(segment);
        }
    }

    // Calculate duration
    jlong durationMs = audio.size() * 1000 / WHISPER_SAMPLE_RATE;

    // Create final result
    jmethodID resultCtor = env->GetMethodID(resultClass, "<init>",
        "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");

    jobject result = env->NewObject(resultClass, resultCtor,
        env->NewStringUTF(fullText.c_str()),
        segmentList,
        env->NewStringUTF(g_language.c_str()),
        durationMs);


    env->CallVoidMethod(callback, onFinal, result);
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelStt(
    JNIEnv *env, jobject thiz) {

    LOGI("Cancel STT requested");
    g_cancel_requested = true;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownStt(
    JNIEnv *env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx != nullptr) {
        LOGI("Shutting down Whisper");
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════
//                    TTS STUBS (when TTS disabled)
// ═══════════════════════════════════════════════════════════════

#ifdef SPEECHKMP_STT_ONLY

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitTts(
    JNIEnv *env, jobject thiz,
    jstring modelPath,
    jstring configPath,
    jstring espeakDataPath,
    jint speakerId,
    jfloat speechRate,
    jint sampleRate,
    jfloat sentenceSilence) {
    LOGE("TTS not available - built with STT only");
    return JNI_FALSE;
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(
    JNIEnv *env, jobject thiz,
    jstring text) {
    LOGE("TTS not available - built with STT only");
    return env->NewShortArray(0);
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv *env, jobject thiz,
    jstring text,
    jstring outputPath) {
    LOGE("TTS not available - built with STT only");
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject thiz,
    jstring text,
    jobject callback) {
    jclass cbClass = env->GetObjectClass(callback);
    jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");
    env->CallVoidMethod(callback, onError, env->NewStringUTF("TTS not available - built with STT only"));
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(
    JNIEnv *env, jobject thiz) {
    // No-op when TTS disabled
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(
    JNIEnv *env, jobject thiz) {
    // No-op when TTS disabled
}

#endif // SPEECHKMP_STT_ONLY
