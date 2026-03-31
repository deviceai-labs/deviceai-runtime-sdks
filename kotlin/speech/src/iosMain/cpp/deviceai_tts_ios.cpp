/**
 * deviceai_tts_ios.cpp - C wrapper for Text-to-Speech via sherpa-onnx on iOS.
 *
 * Exposes the speech_tts_* C API consumed by Kotlin cinterop.
 * When sherpa-onnx is not yet initialised as a submodule the entire
 * implementation compiles to no-op stubs (guarded by HAVE_SHERPA_ONNX).
 */

#include "../c_interop/include/speech_ios.h"

#include <string>
#include <atomic>
#include <mutex>
#include <fstream>
#include <cstring>
#include <memory>
#include <cstdlib>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
//                           LOGGING
// ═══════════════════════════════════════════════════════════════

#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DeviceAI-TTS] "       fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[DeviceAI-TTS ERROR] " fmt "\n", ##__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════
//                     SHERPA-ONNX BACKEND
// ═══════════════════════════════════════════════════════════════

#ifdef HAVE_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"

static const SherpaOnnxOfflineTts *g_tts = nullptr;
static std::mutex                  g_mutex;
static std::atomic<bool>           g_cancel_requested{false};
static int                         g_speaker_id = 0;

// Write mono 16-bit PCM WAV from float samples.
static bool write_wav_file(const std::string &path,
                           const float *samples, int n_samples,
                           int sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file: %s", path.c_str());
        return false;
    }

    uint32_t data_size      = n_samples * sizeof(int16_t);
    uint32_t file_size      = 36 + data_size;
    uint16_t audio_format   = 1;
    uint16_t num_channels   = 1;
    uint32_t sr             = sample_rate;
    uint32_t byte_rate      = sr * sizeof(int16_t);
    uint16_t block_align    = sizeof(int16_t);
    uint16_t bits_per_sample = 16;
    uint32_t fmt_size       = 16;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char *>(&file_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<char *>(&fmt_size),        4);
    file.write(reinterpret_cast<char *>(&audio_format),    2);
    file.write(reinterpret_cast<char *>(&num_channels),    2);
    file.write(reinterpret_cast<char *>(&sr),              4);
    file.write(reinterpret_cast<char *>(&byte_rate),       4);
    file.write(reinterpret_cast<char *>(&block_align),     2);
    file.write(reinterpret_cast<char *>(&bits_per_sample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<char *>(&data_size), 4);

    for (int i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = static_cast<int16_t>(v * 32767.0f);
        file.write(reinterpret_cast<char *>(&s), 2);
    }
    return true;
}

bool speech_tts_init(const char *model_path, const char *tokens_path,
                     const char *data_dir,   const char *voices_path,
                     int speaker_id,         float speech_rate) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_tts) {
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }

    LOG_DEBUG("Initializing TTS (sherpa-onnx)");
    LOG_DEBUG("Model: %s", model_path ? model_path : "(null)");
    LOG_DEBUG("Tokens: %s", tokens_path ? tokens_path : "(null)");
    LOG_DEBUG("DataDir: %s", data_dir ? data_dir : "(null)");
    LOG_DEBUG("Voices: %s (empty = VITS)", voices_path ? voices_path : "");

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    bool is_kokoro = voices_path && voices_path[0] != '\0';

    if (is_kokoro) {
        config.model.kokoro.model        = model_path;
        config.model.kokoro.voices       = voices_path;
        config.model.kokoro.tokens       = tokens_path;
        config.model.kokoro.data_dir     = data_dir;
        config.model.kokoro.length_scale = (speech_rate > 0.0f) ? 1.0f / speech_rate : 1.0f;
    } else {
        config.model.vits.model          = model_path;
        config.model.vits.tokens         = tokens_path;
        config.model.vits.data_dir       = data_dir;
        config.model.vits.length_scale   = (speech_rate > 0.0f) ? 1.0f / speech_rate : 1.0f;
        config.model.vits.noise_scale    = 0.667f;
        config.model.vits.noise_scale_w  = 0.8f;
    }

    config.model.num_threads     = 2;
    config.model.debug           = 0;
    config.model.provider        = "cpu";
    config.max_num_sentences     = 2;

    g_tts = SherpaOnnxCreateOfflineTts(&config);
    if (!g_tts) {
        LOG_ERROR("SherpaOnnxCreateOfflineTts failed");
        return false;
    }

    g_speaker_id = speaker_id;
    LOG_DEBUG("TTS initialized, sample_rate=%d, speaker_id=%d", SherpaOnnxOfflineTtsSampleRate(g_tts), g_speaker_id);
    return true;
}

int16_t *speech_tts_synthesize(const char *text, int *out_length) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_tts) {
        LOG_ERROR("TTS not initialized");
        *out_length = 0;
        return nullptr;
    }

    g_cancel_requested = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        LOG_ERROR("Synthesis produced no audio");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        *out_length = 0;
        return nullptr;
    }

    LOG_DEBUG("Synthesized %d samples", audio->n);

    int16_t *samples = static_cast<int16_t *>(malloc(audio->n * sizeof(int16_t)));
    if (samples) {
        for (int i = 0; i < audio->n; ++i) {
            float v = audio->samples[i];
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            samples[i] = static_cast<int16_t>(v * 32767.0f);
        }
        *out_length = audio->n;
    } else {
        *out_length = 0;
    }

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return samples;
}

bool speech_tts_synthesize_to_file(const char *text, const char *output_path) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_tts) {
        LOG_ERROR("TTS not initialized");
        return false;
    }

    g_cancel_requested = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

    if (!audio || audio->n == 0) {
        LOG_ERROR("Synthesis produced no audio");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return false;
    }

    int sr = SherpaOnnxOfflineTtsSampleRate(g_tts);
    bool ok = write_wav_file(output_path, audio->samples, audio->n, sr);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    if (!ok) return false;
    LOG_DEBUG("Wrote %d samples to %s", audio->n, output_path);
    return true;
}

void speech_tts_synthesize_stream(const char *text,
                                   tts_on_chunk    on_chunk,
                                   tts_on_complete on_complete,
                                   tts_on_error    on_error,
                                   void *user) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_tts) {
        if (on_error) on_error("TTS not initialized", user);
        return;
    }

    g_cancel_requested = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_speaker_id, 1.0f);

    if (!audio || audio->n == 0 || g_cancel_requested) {
        if (!g_cancel_requested && on_error) on_error("No audio generated", user);
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return;
    }

    // Convert once, then stream in chunks
    std::vector<int16_t> shorts(audio->n);
    for (int i = 0; i < audio->n; ++i) {
        float v = audio->samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        shorts[i] = static_cast<int16_t>(v * 32767.0f);
    }
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    const int CHUNK_SIZE = 4096;
    for (int i = 0; i < (int)shorts.size() && !g_cancel_requested; i += CHUNK_SIZE) {
        int chunk_len = std::min(CHUNK_SIZE, (int)shorts.size() - i);
        if (on_chunk) on_chunk(shorts.data() + i, chunk_len, user);
    }

    if (!g_cancel_requested && on_complete) on_complete(user);
}

void speech_tts_cancel(void) {
    g_cancel_requested = true;
}

void speech_tts_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_tts) {
        LOG_DEBUG("Shutting down TTS");
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }
}

#else // !HAVE_SHERPA_ONNX — no-op stubs

bool speech_tts_init(const char * /*model_path*/, const char * /*tokens_path*/,
                     const char * /*data_dir*/,   const char * /*voices_path*/,
                     int /*speaker_id*/,           float /*speech_rate*/) {
    LOG_ERROR("TTS not available: sherpa-onnx submodule not initialised");
    return false;
}

int16_t *speech_tts_synthesize(const char * /*text*/, int *out_length) {
    *out_length = 0;
    return nullptr;
}

bool speech_tts_synthesize_to_file(const char * /*text*/, const char * /*output_path*/) {
    return false;
}

void speech_tts_synthesize_stream(const char * /*text*/,
                                   tts_on_chunk    /*on_chunk*/,
                                   tts_on_complete /*on_complete*/,
                                   tts_on_error    on_error,
                                   void *user) {
    if (on_error)
        on_error("TTS not available: sherpa-onnx submodule not initialised", user);
}

void speech_tts_cancel(void) {}

void speech_tts_shutdown(void) {}

#endif // HAVE_SHERPA_ONNX

void speech_free_audio(int16_t *ptr) {
    free(ptr);
}

void speech_shutdown_all(void) {
    speech_stt_shutdown();
    speech_tts_shutdown();
}
