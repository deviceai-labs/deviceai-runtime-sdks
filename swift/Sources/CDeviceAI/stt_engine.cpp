/**
 * stt_engine.cpp — Platform-agnostic STT inference via whisper.cpp.
 *
 * Extracted from deviceai_whisper_jni.cpp. Contains all inference logic:
 * - Model loading and context creation
 * - Energy-based VAD (adaptive threshold, 10th-percentile noise floor)
 * - Per-call whisper_state isolation (no state bleeding across calls)
 * - WAV file parsing and resampling
 * - Inference params (language, audio_ctx, max_tokens)
 *
 * No JNI, no platform-specific code. Called by bridges/jni/ and bridges/ios/.
 */

#include "dai_stt.h"
#include "whisper.h"

#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <fstream>

// ═══════════════════════════════════════════════════════════════
//                         Logging
// ═══════════════════════════════════════════════════════════════

#ifdef ANDROID
#include <android/log.h>
#define LOG_TAG "SttEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// ═══════════════════════════════════════════════════════════════
//                         Global state
// ═══════════════════════════════════════════════════════════════

static struct whisper_context *g_ctx = nullptr;
static struct whisper_full_params g_params;
static std::mutex g_mutex;
static std::atomic<bool> g_cancel_requested{false};

static std::string g_language     = "en";
static bool  g_translate          = false;
static int   g_max_threads        = 4;
static bool  g_use_gpu            = true;
static bool  g_use_vad            = true;
static bool  g_single_segment     = true;
static bool  g_no_context         = true;

// ═══════════════════════════════════════════════════════════════
//                      Internal helpers
// ═══════════════════════════════════════════════════════════════

static bool read_wav_file(const std::string &path, std::vector<float> &samples, int &sample_rate) {
    sample_rate = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open WAV file: %s", path.c_str());
        return false;
    }

    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;

    file.seekg(4, std::ios::cur);
    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    while (file.good()) {
        char chunk_id[4];
        file.read(chunk_id, 4);
        uint32_t chunk_size = 0;
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format = 0, num_channels = 0;
            uint32_t sr = 0;
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&num_channels), 2);
            file.read(reinterpret_cast<char*>(&sr), 4);
            sample_rate = (int)sr;
            if (chunk_size > 8) file.seekg(chunk_size - 8, std::ios::cur);
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            std::vector<int16_t> pcm(chunk_size / 2);
            file.read(reinterpret_cast<char*>(pcm.data()), chunk_size);
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
    if (input_rate == WHISPER_SAMPLE_RATE) { output = input; return true; }
    if (input_rate <= 0) return false;

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

/**
 * Energy-based VAD: trims leading/trailing silence.
 * Returns false if no speech detected (caller should skip inference).
 */
static bool vad_trim(std::vector<float> &audio) {
    const int FRAME = 480;  // 30 ms at 16 kHz
    const int PAD   = 10;   // ~300 ms padding

    if ((int)audio.size() < FRAME) return false;

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
    float noise_floor = sorted_rms[n_frames / 10];
    float threshold   = std::max(0.02f, noise_floor * 4.0f);

    int first_speech = -1, last_speech = -1;
    for (int f = 0; f < n_frames; ++f) {
        if (frame_rms[f] >= threshold) {
            if (first_speech < 0) first_speech = f;
            last_speech = f;
        }
    }

    if (first_speech < 0) return false;

    int s = std::max(0, first_speech - PAD) * FRAME;
    int e = std::min(n_frames, last_speech + PAD + 1) * FRAME;

    LOGI("[VAD] trimmed %.2fs -> %.2fs (threshold=%.4f)",
         (float)audio.size() / WHISPER_SAMPLE_RATE,
         (float)(e - s) / WHISPER_SAMPLE_RATE, threshold);

    audio = std::vector<float>(audio.begin() + s, audio.begin() + e);
    return true;
}

static struct whisper_full_params make_params(const std::string &language, float audio_sec) {
    struct whisper_full_params p = g_params;
    p.language  = language.c_str();
    p.audio_ctx = 0;
    p.max_tokens = std::max(32, (int)(audio_sec * 3.0f) + 32);
    return p;
}

/**
 * Core transcription: init state → run whisper → collect segments.
 * Returns segments via output params. Caller owns the strings.
 */
static std::string do_transcribe(
    const float* audio, int n_samples,
    std::vector<dai_stt_segment_t>* out_segments,
    std::vector<std::string>* segment_texts,
    int64_t* out_duration_ms
) {
    if (!g_ctx) return "";

    std::vector<float> samples(audio, audio + n_samples);
    float audio_sec = (float)samples.size() / WHISPER_SAMPLE_RATE;

    // VAD
    if (g_use_vad) {
        if (!vad_trim(samples)) {
            LOGI("[VAD] no speech detected");
            if (out_duration_ms) *out_duration_ms = 0;
            return "";
        }
        audio_sec = (float)samples.size() / WHISPER_SAMPLE_RATE;
    }

    if (out_duration_ms) *out_duration_ms = (int64_t)(samples.size() * 1000 / WHISPER_SAMPLE_RATE);

    // Inference
    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        LOGE("Failed to allocate whisper state");
        return "";
    }

    if (whisper_full_with_state(g_ctx, state, params, samples.data(), (int)samples.size()) != 0) {
        whisper_free_state(state);
        LOGE("Whisper inference failed");
        return "";
    }

    // Collect segments
    std::string full_text;
    int n_seg = whisper_full_n_segments_from_state(state);
    for (int i = 0; i < n_seg && !g_cancel_requested; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        if (text) {
            full_text += text;
            if (out_segments && segment_texts) {
                int64_t t0 = whisper_full_get_segment_t0_from_state(state, i) * 10;
                int64_t t1 = whisper_full_get_segment_t1_from_state(state, i) * 10;
                segment_texts->push_back(text);
                out_segments->push_back({segment_texts->back().c_str(), t0, t1});
            }
        }
    }

    whisper_free_state(state);
    return full_text;
}

// ═══════════════════════════════════════════════════════════════
//                    Public C API (dai_stt.h)
// ═══════════════════════════════════════════════════════════════

extern "C" {

bool dai_stt_init(
    const char* model_path,
    const char* language,
    bool translate,
    int max_threads,
    bool use_gpu,
    bool use_vad,
    bool single_segment,
    bool no_context
) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ctx) { whisper_free(g_ctx); g_ctx = nullptr; }

    g_language       = language ? language : "en";
    g_translate      = translate;
    g_max_threads    = max_threads;
    g_use_gpu        = use_gpu;
    g_use_vad        = use_vad;
    g_single_segment = single_segment;
    g_no_context     = no_context;

    struct whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = g_use_gpu;

    g_ctx = whisper_init_from_file_with_params(model_path, ctx_params);
    if (!g_ctx) {
        LOGE("Failed to initialize Whisper model: %s", model_path);
        return false;
    }

    g_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    g_params.translate        = g_translate;
    g_params.n_threads        = g_max_threads;
    g_params.no_timestamps    = false;
    g_params.print_special    = false;
    g_params.print_progress   = false;
    g_params.print_realtime   = false;
    g_params.print_timestamps = false;
    g_params.single_segment   = g_single_segment;
    g_params.no_context       = g_no_context;

    LOGI("STT initialized: %s (language=%s, threads=%d, gpu=%d, vad=%d)",
         model_path, g_language.c_str(), max_threads, use_gpu, use_vad);
    return true;
}

char* dai_stt_transcribe(const char* audio_path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ctx || !audio_path) return nullptr;

    g_cancel_requested = false;

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(audio_path, samples, sample_rate)) return nullptr;

    std::vector<float> samples_16k;
    if (!resample_to_16k(samples, sample_rate, samples_16k)) return nullptr;

    std::string result = do_transcribe(samples_16k.data(), (int)samples_16k.size(), nullptr, nullptr, nullptr);
    return result.empty() ? nullptr : strdup(result.c_str());
}

char* dai_stt_transcribe_audio(const float* samples, int n_samples) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ctx || !samples || n_samples <= 0) return nullptr;

    g_cancel_requested = false;

    std::string result = do_transcribe(samples, n_samples, nullptr, nullptr, nullptr);
    return result.empty() ? nullptr : strdup(result.c_str());
}

dai_stt_result_t* dai_stt_transcribe_detailed(const char* audio_path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ctx || !audio_path) return nullptr;

    g_cancel_requested = false;

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(audio_path, samples, sample_rate)) return nullptr;

    std::vector<float> samples_16k;
    if (!resample_to_16k(samples, sample_rate, samples_16k)) return nullptr;

    std::vector<dai_stt_segment_t> segments;
    std::vector<std::string> segment_texts;
    int64_t duration_ms = 0;

    std::string full_text = do_transcribe(
        samples_16k.data(), (int)samples_16k.size(),
        &segments, &segment_texts, &duration_ms
    );

    auto* result = (dai_stt_result_t*)calloc(1, sizeof(dai_stt_result_t));
    result->text = strdup(full_text.c_str());
    result->language = strdup(g_language.c_str());
    result->duration_ms = duration_ms;
    result->segment_count = (int)segments.size();

    if (!segments.empty()) {
        result->segments = (dai_stt_segment_t*)calloc(segments.size(), sizeof(dai_stt_segment_t));
        for (size_t i = 0; i < segments.size(); i++) {
            result->segments[i].text = strdup(segment_texts[i].c_str());
            result->segments[i].start_ms = segments[i].start_ms;
            result->segments[i].end_ms = segments[i].end_ms;
        }
    }

    return result;
}

void dai_stt_transcribe_stream(
    const float* samples,
    int n_samples,
    dai_stt_on_partial_fn on_partial,
    dai_stt_on_final_fn on_final,
    dai_stt_on_error_fn on_error,
    void* ctx
) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ctx) {
        if (on_error) on_error("STT not initialized", ctx);
        return;
    }
    if (!samples || n_samples <= 0) {
        if (on_error) on_error("Empty audio", ctx);
        return;
    }

    g_cancel_requested = false;

    std::vector<float> audio(samples, samples + n_samples);
    float audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;

    if (g_use_vad) {
        if (!vad_trim(audio)) {
            // No speech — send empty final result
            dai_stt_result_t empty = {};
            empty.text = (char*)"";
            empty.language = (char*)g_language.c_str();
            if (on_final) on_final(&empty, ctx);
            return;
        }
        audio_sec = (float)audio.size() / WHISPER_SAMPLE_RATE;
    }

    std::string lang = g_language;
    struct whisper_full_params params = make_params(lang, audio_sec);

    struct whisper_state *state = whisper_init_state(g_ctx);
    if (!state) {
        if (on_error) on_error("Failed to allocate whisper state", ctx);
        return;
    }

    if (whisper_full_with_state(g_ctx, state, params, audio.data(), (int)audio.size()) != 0) {
        whisper_free_state(state);
        if (on_error) on_error("Transcription failed", ctx);
        return;
    }

    std::string full_text;
    std::vector<dai_stt_segment_t> segments;
    std::vector<std::string> segment_texts;
    int n_seg = whisper_full_n_segments_from_state(state);

    for (int i = 0; i < n_seg && !g_cancel_requested; i++) {
        const char *text = whisper_full_get_segment_text_from_state(state, i);
        if (text) {
            full_text += text;
            if (on_partial) on_partial(full_text.c_str(), ctx);

            int64_t t0 = whisper_full_get_segment_t0_from_state(state, i) * 10;
            int64_t t1 = whisper_full_get_segment_t1_from_state(state, i) * 10;
            segment_texts.push_back(text);
            segments.push_back({segment_texts.back().c_str(), t0, t1});
        }
    }
    whisper_free_state(state);

    if (on_final && !g_cancel_requested) {
        dai_stt_result_t result = {};
        result.text = (char*)full_text.c_str();
        result.language = (char*)g_language.c_str();
        result.duration_ms = (int64_t)(audio.size() * 1000 / WHISPER_SAMPLE_RATE);
        result.segments = segments.data();
        result.segment_count = (int)segments.size();
        on_final(&result, ctx);
    }
}

void dai_stt_cancel(void) {
    g_cancel_requested = true;
}

void dai_stt_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ctx) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }
}

void dai_stt_free_string(char* str) {
    free(str);
}

void dai_stt_free_result(dai_stt_result_t* result) {
    if (!result) return;
    free(result->text);
    free(result->language);
    if (result->segments) {
        for (int i = 0; i < result->segment_count; i++) {
            free((void*)result->segments[i].text);
        }
        free(result->segments);
    }
    free(result);
}

} // extern "C"
