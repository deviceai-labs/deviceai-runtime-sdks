/**
 * piper_jni.cpp - JNI bridge for Piper TTS (Text-to-Speech)
 *
 * Shared between Android and Desktop (JVM) platforms.
 */

#include "deviceai_speech_jni.h"
#include "piper.hpp"

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
#define LOG_TAG "SpeechKMP-TTS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, "[SpeechKMP-TTS INFO] " __VA_ARGS__); fprintf(stdout, "\n")
#define LOGE(...) fprintf(stderr, "[SpeechKMP-TTS ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGD(...) fprintf(stdout, "[SpeechKMP-TTS DEBUG] " __VA_ARGS__); fprintf(stdout, "\n")
#endif

// ═══════════════════════════════════════════════════════════════
//                          GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

static piper::PiperConfig g_config;
static piper::Voice g_voice;
static bool g_initialized = false;
static std::mutex g_mutex;
static std::atomic<bool> g_cancel_requested{false};

// Configuration
static std::atomic<int> g_speaker_id{-1};
static std::atomic<float> g_speech_rate{1.0f};
static std::atomic<int> g_sample_rate{22050};
static std::atomic<float> g_sentence_silence{0.2f};

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

static bool write_wav_file(const std::string &path, const std::vector<int16_t> &samples, int sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open file for writing: %s", path.c_str());
        return false;
    }

    // WAV header
    uint32_t data_size = samples.size() * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    // RIFF header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file.write("WAVE", 4);

    // fmt chunk
    file.write("fmt ", 4);
    uint32_t fmt_size = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    uint16_t audio_format = 1; // PCM
    file.write(reinterpret_cast<char*>(&audio_format), 2);
    uint16_t num_channels = 1; // Mono
    file.write(reinterpret_cast<char*>(&num_channels), 2);
    uint32_t sr = sample_rate;
    file.write(reinterpret_cast<char*>(&sr), 4);
    uint32_t byte_rate = sample_rate * num_channels * sizeof(int16_t);
    file.write(reinterpret_cast<char*>(&byte_rate), 4);
    uint16_t block_align = num_channels * sizeof(int16_t);
    file.write(reinterpret_cast<char*>(&block_align), 2);
    uint16_t bits_per_sample = 16;
    file.write(reinterpret_cast<char*>(&bits_per_sample), 2);

    // data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);
    file.write(reinterpret_cast<const char*>(samples.data()), data_size);

    file.close();
    return true;
}

// ═══════════════════════════════════════════════════════════════
//                        JNI FUNCTIONS
// ═══════════════════════════════════════════════════════════════

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

    std::lock_guard<std::mutex> lock(g_mutex);

    // Cleanup existing state
    if (g_initialized) {
        piper::terminate(g_config);
        g_initialized = false;
    }

    std::string model = jstring_to_string(env, modelPath);
    std::string config = jstring_to_string(env, configPath);
    std::string espeakData = jstring_to_string(env, espeakDataPath);

    g_speaker_id = speakerId;
    g_speech_rate = speechRate;
    g_sample_rate = sampleRate;
    g_sentence_silence = sentenceSilence;

    LOGI("Initializing Piper TTS");
    LOGI("Model: %s", model.c_str());
    LOGI("Config: %s", config.c_str());
    LOGI("eSpeak data: %s", espeakData.c_str());
    LOGI("Speaker ID: %d, Rate: %.2f, Sample Rate: %d", speakerId, speechRate, sampleRate);

    try {
        // Set espeak-ng data path (required for phonemization)
        g_config.eSpeakDataPath = espeakData;

        // Initialize piper (loads espeak-ng)
        piper::initialize(g_config);

        // Load voice
        std::optional<piper::SpeakerId> sid;
        if (speakerId >= 0) {
            sid = static_cast<piper::SpeakerId>(speakerId);
        }

        // Load voice model (useCuda = false for mobile)
        piper::loadVoice(g_config, model, config, g_voice, sid, false);

        // Apply configurations
        if (speechRate != 1.0f) {
            g_voice.synthesisConfig.lengthScale = 1.0f / speechRate;
        }
        g_voice.synthesisConfig.sentenceSilenceSeconds = sentenceSilence;

        g_initialized = true;
        LOGI("Piper TTS initialized successfully");
        return JNI_TRUE;

    } catch (const std::exception &e) {
        LOGE("Failed to initialize Piper: %s", e.what());
        return JNI_FALSE;
    }
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(
    JNIEnv *env, jobject thiz,
    jstring text) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized) {
        LOGE("Piper not initialized");
        return env->NewShortArray(0);
    }

    g_cancel_requested = false;

    std::string input = jstring_to_string(env, text);
    LOGD("Synthesizing: %s", input.c_str());

    try {
        std::vector<int16_t> audio;
        piper::SynthesisResult result;

        // Empty callback for non-streaming synthesis
        piper::textToAudio(g_config, g_voice, input, audio, result, []() {});

        if (audio.empty()) {
            LOGE("Synthesis produced no audio");
            return env->NewShortArray(0);
        }

        LOGD("Synthesized %zu samples (%.2f sec, RTF: %.2f)",
             audio.size(), result.audioSeconds, result.realTimeFactor);

        // Create result array
        jshortArray samples = env->NewShortArray(audio.size());
        env->SetShortArrayRegion(samples, 0, audio.size(), audio.data());

        return samples;

    } catch (const std::exception &e) {
        LOGE("Synthesis failed: %s", e.what());
        return env->NewShortArray(0);
    }
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv *env, jobject thiz,
    jstring text,
    jstring outputPath) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized) {
        LOGE("Piper not initialized");
        return JNI_FALSE;
    }

    g_cancel_requested = false;

    std::string input = jstring_to_string(env, text);
    std::string path = jstring_to_string(env, outputPath);

    LOGD("Synthesizing to file: %s", path.c_str());

    try {
        std::vector<int16_t> audio;
        piper::SynthesisResult result;

        // Empty callback for non-streaming synthesis
        piper::textToAudio(g_config, g_voice, input, audio, result, []() {});

        if (audio.empty()) {
            LOGE("Synthesis produced no audio");
            return JNI_FALSE;
        }

        // Get sample rate from voice config
        int sr = g_voice.synthesisConfig.sampleRate;
        if (!write_wav_file(path, audio, sr)) {
            return JNI_FALSE;
        }

        LOGI("Wrote %zu samples to %s (%.2f sec)", audio.size(), path.c_str(), result.audioSeconds);
        return JNI_TRUE;

    } catch (const std::exception &e) {
        LOGE("Synthesis failed: %s", e.what());
        return JNI_FALSE;
    }
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject thiz,
    jstring text,
    jobject callback) {

    std::lock_guard<std::mutex> lock(g_mutex);

    // Get callback methods
    jclass cbClass = env->GetObjectClass(callback);
    jmethodID onChunk = env->GetMethodID(cbClass, "onAudioChunk", "([S)V");
    jmethodID onComplete = env->GetMethodID(cbClass, "onComplete", "()V");
    jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");

    if (!g_initialized) {
        env->CallVoidMethod(callback, onError, env->NewStringUTF("Piper not initialized"));
        return;
    }

    g_cancel_requested = false;

    std::string input = jstring_to_string(env, text);

    try {
        std::vector<int16_t> audio;
        piper::SynthesisResult result;

        // Synthesize all audio first (Piper doesn't support true streaming)
        piper::textToAudio(g_config, g_voice, input, audio, result, [&]() {
            // Progress callback - check for cancellation
            if (g_cancel_requested) {
                throw std::runtime_error("Cancelled");
            }
        });

        if (g_cancel_requested || audio.empty()) {
            if (!g_cancel_requested) {
                env->CallVoidMethod(callback, onError, env->NewStringUTF("No audio generated"));
            }
            return;
        }

        // Send audio in chunks (4096 samples per chunk ≈ 185ms at 22050Hz)
        const size_t CHUNK_SIZE = 4096;
        for (size_t i = 0; i < audio.size() && !g_cancel_requested; i += CHUNK_SIZE) {
            size_t end = std::min(i + CHUNK_SIZE, audio.size());
            size_t chunk_len = end - i;

            jshortArray samples = env->NewShortArray(chunk_len);
            env->SetShortArrayRegion(samples, 0, chunk_len, &audio[i]);
            env->CallVoidMethod(callback, onChunk, samples);
            env->DeleteLocalRef(samples);
        }

        if (!g_cancel_requested) {
            env->CallVoidMethod(callback, onComplete);
        }

    } catch (const std::exception &e) {
        if (!g_cancel_requested) {
            env->CallVoidMethod(callback, onError, env->NewStringUTF(e.what()));
        }
    }
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(
    JNIEnv *env, jobject thiz) {

    LOGI("Cancel TTS requested");
    g_cancel_requested = true;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(
    JNIEnv *env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized) {
        LOGI("Shutting down Piper TTS");
        piper::terminate(g_config);
        g_initialized = false;
    }
}
