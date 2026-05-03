/**
 * tts_engine.cpp — Platform-agnostic TTS inference via sherpa-onnx.
 *
 * Extracted from deviceai_tts_jni.cpp. Contains all inference logic:
 * - sherpa-onnx config (VITS vs Kokoro detection)
 * - Float → int16 conversion
 * - WAV file writing
 * - Streaming synthesis with chunked delivery
 *
 * Compiles to no-op stubs when HAVE_SHERPA_ONNX is not defined.
 * No JNI, no platform-specific code. Called by bridges/jni/ and bridges/ios/.
 */

#include "dai_tts.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
//                         Logging
// ═══════════════════════════════════════════════════════════════

#ifdef ANDROID
#include <android/log.h>
#define LOG_TAG "TtsEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
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

static bool write_wav_file(const std::string &path,
                           const float *samples, int n_samples,
                           int sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    uint32_t data_size = n_samples * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint32_t sr = sample_rate;
    uint32_t byte_rate = sr * sizeof(int16_t);
    uint16_t block_align = sizeof(int16_t);
    uint16_t bits_per_sample = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    file.write(reinterpret_cast<char*>(&audio_format), 2);
    file.write(reinterpret_cast<char*>(&num_channels), 2);
    file.write(reinterpret_cast<char*>(&sr), 4);
    file.write(reinterpret_cast<char*>(&byte_rate), 4);
    file.write(reinterpret_cast<char*>(&block_align), 2);
    file.write(reinterpret_cast<char*>(&bits_per_sample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);

    for (int i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = static_cast<int16_t>(v * 32767.0f);
        file.write(reinterpret_cast<char*>(&s), 2);
    }
    return true;
}

static void float_to_int16(const float* in, int16_t* out, int n) {
    for (int i = 0; i < n; ++i) {
        float v = in[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = static_cast<int16_t>(v * 32767.0f);
    }
}

// ── Public C API ─────────────────────────────────────────────

extern "C" {

bool dai_tts_init(
    const char* model_path,
    const char* tokens_path,
    const char* data_dir,
    const char* voices_path,
    int speaker_id,
    float speech_rate
) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_tts) {
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    bool is_kokoro = voices_path && voices_path[0] != '\0';
    float length_scale = (speech_rate > 0.0f) ? 1.0f / speech_rate : 1.0f;

    if (is_kokoro) {
        config.model.kokoro.model       = model_path;
        config.model.kokoro.voices      = voices_path;
        config.model.kokoro.tokens      = tokens_path;
        config.model.kokoro.data_dir    = data_dir ? data_dir : "";
        config.model.kokoro.length_scale = length_scale;
    } else {
        config.model.vits.model       = model_path;
        config.model.vits.tokens      = tokens_path;
        config.model.vits.data_dir    = data_dir ? data_dir : "";
        config.model.vits.length_scale = length_scale;
        config.model.vits.noise_scale   = 0.667f;
        config.model.vits.noise_scale_w = 0.8f;
    }

    config.model.num_threads    = 2;
    config.model.debug          = 0;
    config.model.provider       = "cpu";
    config.max_num_sentences    = 2;

    g_tts = SherpaOnnxCreateOfflineTts(&config);
    if (!g_tts) {
        LOGE("SherpaOnnxCreateOfflineTts failed");
        return false;
    }

    g_speaker_id = speaker_id >= 0 ? speaker_id : 0;
    LOGI("TTS initialized (sample_rate=%d, speaker=%d, kokoro=%d)",
         SherpaOnnxOfflineTtsSampleRate(g_tts), g_speaker_id, is_kokoro);
    return true;
}

int16_t* dai_tts_synthesize(const char* text, int* out_len) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_tts || !text) { if (out_len) *out_len = 0; return nullptr; }

    g_cancel_requested = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        if (out_len) *out_len = 0;
        return nullptr;
    }

    int16_t* result = (int16_t*)malloc(audio->n * sizeof(int16_t));
    float_to_int16(audio->samples, result, audio->n);
    if (out_len) *out_len = audio->n;

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return result;
}

bool dai_tts_synthesize_to_file(const char* text, const char* output_path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_tts || !text || !output_path) return false;

    g_cancel_requested = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return false;
    }

    int sr = SherpaOnnxOfflineTtsSampleRate(g_tts);
    bool ok = write_wav_file(output_path, audio->samples, audio->n, sr);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return ok;
}

void dai_tts_synthesize_stream(
    const char* text,
    dai_tts_on_chunk_fn on_chunk,
    dai_tts_on_complete_fn on_complete,
    dai_tts_on_error_fn on_error,
    void* ctx
) {
    // Generate all audio under lock, then deliver chunks outside lock
    std::vector<int16_t> shorts;
    bool tts_ok = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_tts) {
            // tts_ok stays false
        } else {
            g_cancel_requested = false;
            const SherpaOnnxGeneratedAudio *audio =
                SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

            if (audio && audio->n > 0 && !g_cancel_requested) {
                shorts.resize(audio->n);
                float_to_int16(audio->samples, shorts.data(), audio->n);
                tts_ok = true;
            }
            if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }
    }

    if (!tts_ok) {
        if (on_error) on_error("TTS synthesis failed or not initialized", ctx);
        return;
    }

    // Deliver in chunks (~170ms at 24kHz)
    const int CHUNK_SIZE = 4096;
    for (int i = 0; i < (int)shorts.size() && !g_cancel_requested; i += CHUNK_SIZE) {
        int chunk_len = std::min(CHUNK_SIZE, (int)shorts.size() - i);
        if (on_chunk) on_chunk(shorts.data() + i, chunk_len, ctx);
    }

    if (!g_cancel_requested && on_complete) on_complete(ctx);
}

void dai_tts_cancel(void) {
    g_cancel_requested = true;
}

void dai_tts_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_tts) {
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }
}

void dai_tts_free_audio(int16_t* samples) {
    free(samples);
}

} // extern "C"

#else // !HAVE_SHERPA_ONNX — no-op stubs

extern "C" {

bool dai_tts_init(const char*, const char*, const char*, const char*, int, float) {
    LOGE("TTS not available: sherpa-onnx not built");
    return false;
}
int16_t* dai_tts_synthesize(const char*, int* out_len) {
    if (out_len) *out_len = 0;
    return nullptr;
}
bool dai_tts_synthesize_to_file(const char*, const char*) { return false; }
void dai_tts_synthesize_stream(const char*, dai_tts_on_chunk_fn, dai_tts_on_complete_fn,
                               dai_tts_on_error_fn on_error, void* ctx) {
    if (on_error) on_error("TTS not available: sherpa-onnx not built", ctx);
}
void dai_tts_cancel(void) {}
void dai_tts_shutdown(void) {}
void dai_tts_free_audio(int16_t*) {}

} // extern "C"

#endif // HAVE_SHERPA_ONNX
