/**
 * deviceai_speech_engine.cpp
 *
 * Unified Speech engine — STT (whisper.cpp) + TTS (Piper/eSpeak-ng).
 * Pure C++ core, zero JNI/Swift/Flutter imports.
 *
 * Key STT features carried forward from the Android implementation:
 *   - Adaptive energy-based VAD: trims silence, prevents whisper looping
 *   - audio_ctx derived from actual sample count (avoids full 30s window)
 *   - Fresh whisper_state per call (prevents context bleed between calls)
 *
 * Exposes dai_stt_* and dai_tts_* C API declared in deviceai_speech_engine.h.
 */

#include "deviceai_speech_engine.h"
#include "whisper.h"
#ifdef HAVE_SHERPA_ONNX
#  include "sherpa-onnx/c-api/c-api.h"
#endif

#include <string>
#include <vector>
#include <tuple>
#include <atomic>
#include <mutex>
#include <functional>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <memory>

// ─── Logging ─────────────────────────────────────────────────────────────────

#ifdef __ANDROID__
#  include <android/log.h>
#  define STT_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "DeviceAI-STT", __VA_ARGS__)
#  define STT_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DeviceAI-STT", __VA_ARGS__)
#  define STT_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "DeviceAI-STT", __VA_ARGS__)
#  define TTS_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "DeviceAI-TTS", __VA_ARGS__)
#  define TTS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DeviceAI-TTS", __VA_ARGS__)
#  define TTS_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "DeviceAI-TTS", __VA_ARGS__)
#else
#  define STT_LOGI(...) fprintf(stdout, "[DeviceAI-STT] "       __VA_ARGS__); fputc('\n', stdout)
#  define STT_LOGE(...) fprintf(stderr, "[DeviceAI-STT ERROR] " __VA_ARGS__); fputc('\n', stderr)
#  define STT_LOGD(...) fprintf(stdout, "[DeviceAI-STT DEBUG] " __VA_ARGS__); fputc('\n', stdout)
#  define TTS_LOGI(...) fprintf(stdout, "[DeviceAI-TTS] "       __VA_ARGS__); fputc('\n', stdout)
#  define TTS_LOGE(...) fprintf(stderr, "[DeviceAI-TTS ERROR] " __VA_ARGS__); fputc('\n', stderr)
#  define TTS_LOGD(...) fprintf(stdout, "[DeviceAI-TTS DEBUG] " __VA_ARGS__); fputc('\n', stdout)
#endif

static inline long now_ms() {
    using namespace std::chrono;
    return (long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ─── STT global state ─────────────────────────────────────────────────────────

static struct whisper_context    *g_stt_ctx     = nullptr;
static struct whisper_full_params g_stt_params;
static std::mutex                 g_stt_mutex;
static std::atomic<bool>          g_stt_cancel{false};

static std::string  g_stt_language    = "en";
static std::atomic<bool>  g_stt_translate{false};
static std::atomic<int>   g_stt_threads{4};
static std::atomic<bool>  g_stt_use_gpu{true};
static std::atomic<bool>  g_stt_use_vad{true};
static std::atomic<bool>  g_stt_single_segment{true};
static std::atomic<bool>  g_stt_no_context{true};

// ─── TTS global state ─────────────────────────────────────────────────────────

#ifdef HAVE_SHERPA_ONNX
static SherpaOnnxOfflineTts    *g_tts            = nullptr;
static std::mutex               g_tts_mutex;
static std::atomic<bool>        g_tts_cancel{false};
// g_tts_sample_rate intentionally omitted: the sample rate is determined by
// the voice model, not the caller. Read from audio->sample_rate at synthesis time.
static std::atomic<int>         g_tts_speaker_id{0};

// ─── VAD global state ─────────────────────────────────────────────────────────
// Silero VAD via sherpa-onnx: segment-based, stateful for streaming.

static SherpaOnnxVoiceActivityDetector *g_vad          = nullptr;
static float                            g_vad_threshold = 0.5f;
static int                              g_vad_sample_rate_val = 16000;
static std::atomic<bool>                g_vad_in_speech{false};
static std::mutex                       g_vad_mutex;
#endif // HAVE_SHERPA_ONNX

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Shared C string helpers
// ═══════════════════════════════════════════════════════════════════════════

static char *strdup_c(const std::string &s) {
    char *out = static_cast<char *>(malloc(s.size() + 1));
    if (out) memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Audio utilities
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Read a PCM WAV file into float32 samples.
 * Supports mono and stereo (stereo is downmixed to mono).
 */
static bool read_wav_file(
    const std::string    &path,
    std::vector<float>   &samples,
    int                  &sample_rate
) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        STT_LOGE("Cannot open WAV: %s", path.c_str());
        return false;
    }

    // RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        STT_LOGE("Not a RIFF WAV file: %s", path.c_str());
        return false;
    }
    file.seekg(4, std::ios::cur); // skip file size

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        STT_LOGE("Missing WAVE header: %s", path.c_str());
        return false;
    }

    uint16_t num_channels = 1;
    bool     fmt_seen     = false;

    while (file.good()) {
        char     chunk_id[4];
        uint32_t chunk_size;
        file.read(chunk_id, 4);
        file.read(reinterpret_cast<char *>(&chunk_size), 4);
        if (!file) break;

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 8) {
                STT_LOGE("Malformed fmt chunk (size=%u): %s", chunk_size, path.c_str());
                return false;
            }
            uint16_t audio_format;
            file.read(reinterpret_cast<char *>(&audio_format), 2);
            file.read(reinterpret_cast<char *>(&num_channels), 2);
            uint32_t sr;
            file.read(reinterpret_cast<char *>(&sr), 4);
            sample_rate = static_cast<int>(sr);
            // chunk_size - 8: safe because we validated chunk_size >= 8 above.
            file.seekg(chunk_size - 8, std::ios::cur);

            if (audio_format != 1) {  // 1 = PCM; others: float(3), ADPCM(2), etc.
                STT_LOGE("Unsupported WAV format %u (only PCM=1 supported): %s",
                         audio_format, path.c_str());
                return false;
            }
            if (num_channels == 0 || num_channels > 8) {
                STT_LOGE("Invalid channel count %u: %s", num_channels, path.c_str());
                return false;
            }
            fmt_seen = true;

        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            if (!fmt_seen || sample_rate <= 0) {
                STT_LOGE("data chunk before fmt or invalid sample_rate=%d: %s",
                         sample_rate, path.c_str());
                return false;
            }
            std::vector<int16_t> pcm(chunk_size / 2);
            file.read(reinterpret_cast<char *>(pcm.data()), chunk_size);

            // Downmix to mono if stereo, convert int16 → float32.
            // num_channels >= 1 is guaranteed by the fmt validation above.
            size_t n_frames = pcm.size() / num_channels;
            samples.resize(n_frames);
            for (size_t i = 0; i < n_frames; i++) {
                float sum = 0.0f;
                for (int c = 0; c < num_channels; c++)
                    sum += static_cast<float>(pcm[i * num_channels + c]);
                samples[i] = (sum / num_channels) / 32768.0f;
            }
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    return !samples.empty();
}

/**
 * Write int16_t PCM samples to a WAV file.
 */
static bool write_wav_file(
    const std::string          &path,
    const std::vector<int16_t> &samples,
    int                         sample_rate
) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        TTS_LOGE("Cannot open for writing: %s", path.c_str());
        return false;
    }

    uint32_t data_size  = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t file_size  = 36 + data_size;
    uint16_t channels   = 1;
    uint16_t bits       = 16;
    uint32_t byte_rate  = sample_rate * channels * sizeof(int16_t);
    uint16_t block_align = channels * sizeof(int16_t);
    uint16_t fmt_size   = 16;
    uint16_t audio_fmt  = 1; // PCM

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char *>(&file_size),   4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<char *>(&fmt_size),    4);
    file.write(reinterpret_cast<char *>(&audio_fmt),   2);
    file.write(reinterpret_cast<char *>(&channels),    2);
    file.write(reinterpret_cast<char *>(&sample_rate), 4);
    file.write(reinterpret_cast<char *>(&byte_rate),   4);
    file.write(reinterpret_cast<char *>(&block_align), 2);
    file.write(reinterpret_cast<char *>(&bits),        2);
    file.write("data", 4);
    file.write(reinterpret_cast<char *>(&data_size),   4);
    file.write(reinterpret_cast<const char *>(samples.data()), data_size);
    file.close();
    return true;
}

/**
 * Resample float32 audio to 16 kHz using linear interpolation.
 * No-op if already at 16 kHz.
 */
static void resample_to_16k(
    const std::vector<float> &input,
    int                       input_rate,
    std::vector<float>       &output
) {
    if (input.empty() || input_rate <= 0) {
        output.clear();
        return;
    }
    if (input_rate == WHISPER_SAMPLE_RATE) {
        output = input;
        return;
    }

    double ratio      = static_cast<double>(WHISPER_SAMPLE_RATE) / input_rate;
    size_t output_len = static_cast<size_t>(input.size() * ratio);
    output.resize(output_len);

    for (size_t i = 0; i < output_len; i++) {
        double src   = i / ratio;
        size_t idx0  = static_cast<size_t>(src);
        size_t idx1  = std::min(idx0 + 1, input.size() - 1);
        double frac  = src - idx0;
        output[i]    = static_cast<float>(input[idx0] * (1.0 - frac) + input[idx1] * frac);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Silero VAD (internal helpers via sherpa-onnx)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Adaptive energy-based VAD (fallback when Silero not initialized).
 * Kept as a fallback — used when g_vad_session == nullptr.
 */
static bool apply_energy_vad(std::vector<float> &audio) {
    const int FRAME = 480;  // 30ms at 16kHz
    const int PAD   = 10;

    int n_frames = static_cast<int>(audio.size()) / FRAME;
    if (n_frames == 0) return false;

    std::vector<float> frame_rms(n_frames);
    for (int f = 0; f < n_frames; f++) {
        const float *p = audio.data() + f * FRAME;
        float sum = 0.0f;
        for (int i = 0; i < FRAME; i++) sum += p[i] * p[i];
        frame_rms[f] = std::sqrt(sum / FRAME);
    }

    std::vector<float> sorted_rms = frame_rms;
    std::sort(sorted_rms.begin(), sorted_rms.end());
    float noise_floor = sorted_rms[std::max(0, n_frames / 10)];
    float threshold   = std::max(0.02f, noise_floor * 4.0f);

    int first_speech = -1, last_speech = -1;
    for (int f = 0; f < n_frames; f++) {
        if (frame_rms[f] >= threshold) {
            if (first_speech < 0) first_speech = f;
            last_speech = f;
        }
    }
    if (first_speech < 0) return false;

    int start = std::max(0,        first_speech - PAD) * FRAME;
    int end   = std::min(n_frames, last_speech  + PAD + 1) * FRAME;
    audio = std::vector<float>(audio.begin() + start, audio.begin() + end);
    return true;
}

/**
 * Apply VAD to a batch audio buffer (used internally by STT).
 *
 * Uses sherpa-onnx Silero VAD if initialized — collects all speech segments,
 * trims audio to span from first to last segment.
 * Falls back to energy-based VAD if sherpa-onnx VAD not initialized.
 */
static bool apply_vad(std::vector<float> &audio) {
#ifdef HAVE_SHERPA_ONNX
    // Lock g_vad_mutex to guard against concurrent dai_vad_init/shutdown on
    // another thread. Lock order is always g_stt_mutex -> g_vad_mutex (the
    // public dai_vad_* APIs never acquire g_stt_mutex), so no deadlock risk.
    std::lock_guard<std::mutex> vad_lock(g_vad_mutex);
    if (g_vad) {
        float before_sec = static_cast<float>(audio.size()) / WHISPER_SAMPLE_RATE;

        SherpaOnnxVoiceActivityDetectorAcceptWaveform(g_vad, audio.data(), (int)audio.size());
        SherpaOnnxVoiceActivityDetectorFlush(g_vad);

        int first_sample = -1, last_sample = -1;
        while (!SherpaOnnxVoiceActivityDetectorEmpty(g_vad)) {
            const SherpaOnnxSpeechSegment *seg = SherpaOnnxVoiceActivityDetectorFront(g_vad);
            if (first_sample < 0) first_sample = seg->start;
            last_sample = seg->start + seg->n;
            SherpaOnnxDestroySpeechSegment(seg);
            SherpaOnnxVoiceActivityDetectorPop(g_vad);
        }

        if (first_sample < 0) {
            STT_LOGI("Silero VAD: no speech detected");
            return false;
        }

        int start = std::max(0, first_sample);
        int end   = std::min((int)audio.size(), last_sample);
        float after_sec = static_cast<float>(end - start) / WHISPER_SAMPLE_RATE;
        STT_LOGI("Silero VAD: trimmed %.2fs -> %.2fs", before_sec, after_sec);

        audio = std::vector<float>(audio.begin() + start, audio.begin() + end);
        return true;
    }
#endif
    STT_LOGD("VAD: using energy-based fallback (Silero not initialized)");
    return apply_energy_vad(audio);
}

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - STT core logic (internal, no JNI/FFI types)
// ═══════════════════════════════════════════════════════════════════════════

// Escape a string for embedding inside a JSON double-quoted value.
// Handles the six characters that JSON requires escaping plus control chars.
static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Other control characters as \uXXXX
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

/**
 * Build JSON result string from transcription segments.
 * Schema: { "text": "...", "language": "en", "durationMs": 1234,
 *           "segments": [{ "text": "...", "startMs": 0, "endMs": 500 }] }
 */
static std::string build_result_json(
    const std::string                                            &text,
    const std::vector<std::tuple<std::string, int64_t, int64_t>> &segments,
    const std::string                                            &language,
    int64_t                                                       duration_ms
) {
    std::ostringstream j;
    j << "{\"text\":\"" << json_escape(text) << "\""
      << ",\"language\":\"" << json_escape(language) << "\""
      << ",\"durationMs\":" << duration_ms
      << ",\"segments\":[";

    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) j << ",";
        j << "{\"text\":\"" << json_escape(std::get<0>(segments[i])) << "\""
          << ",\"startMs\":" << std::get<1>(segments[i])
          << ",\"endMs\":"   << std::get<2>(segments[i])
          << "}";
    }
    j << "]}";
    return j.str();
}

/**
 * Core transcription: run whisper_full_with_state on pre-processed audio.
 *
 * Applies VAD, derives audio_ctx from actual sample count, allocates a fresh
 * whisper_state per call (prevents KV-cache bleed between calls).
 *
 * Returns collected text. Also fills segments if non-null.
 */
static std::string run_transcription(
    std::vector<float>                                           &audio,
    std::vector<std::tuple<std::string, int64_t, int64_t>>      *segments_out,
    std::function<void(const std::string &)>                      on_partial
) {
    if (g_stt_ctx == nullptr) {
        STT_LOGE("Whisper not initialized");
        return "";
    }

    g_stt_cancel = false;
    float audio_sec = static_cast<float>(audio.size()) / WHISPER_SAMPLE_RATE;

    // VAD: trim silence before inference
    if (g_stt_use_vad.load()) {
        if (!apply_vad(audio)) return "";
        audio_sec = static_cast<float>(audio.size()) / WHISPER_SAMPLE_RATE;
    }

    // Derive audio_ctx from actual sample count to avoid full 30s attention window.
    // Formula: each whisper mel frame = 160 samples; encoder conv halves → /320.
    struct whisper_full_params params = g_stt_params;
    int auto_ctx = (static_cast<int>(audio.size()) + 319) / 320;
    params.audio_ctx = std::min(auto_ctx, 1500);

    // Cap the text decoder token budget proportional to actual audio duration.
    //
    // Root cause of repetition loops: the decoder generates up to n_text_ctx=448
    // tokens regardless of audio length. When it hallucinates a phrase loop, it
    // fills ALL 448 tokens before stopping — then entropy_thold retries at higher
    // temperatures, each also generating the full budget (5 retries × 448 = 2240
    // tokens total). That is why short audio produces 10-second inference spikes.
    //
    // English speech ≈ 2.5 words/sec × 1.3 tokens/word ≈ 3.5 tokens/sec.
    // We allow 8× headroom (28 tokens/sec) for pauses, punctuation, mixed input.
    // No hard floor — for very short audio keep budget tight. Cap at 220.
    params.max_tokens = std::min(220, std::max(8, static_cast<int>(audio_sec * 28.0f)));

    STT_LOGD("audio_ctx=%d max_tokens=%d (%.2fs after VAD)", params.audio_ctx, params.max_tokens, audio_sec);

    // Fresh state per call — prevents result_all accumulation across calls.
    struct whisper_state *state = whisper_init_state(g_stt_ctx);
    if (!state) {
        STT_LOGE("Failed to allocate whisper_state");
        return "";
    }

    long t0 = now_ms();
    int rc = whisper_full_with_state(g_stt_ctx, state, params,
                                     audio.data(), static_cast<int>(audio.size()));
    long t1 = now_ms();
    STT_LOGI("Inference: %ld ms (RTF %.2fx)", t1 - t0, (float)(t1 - t0) / (audio_sec * 1000.0f));

    if (rc != 0) {
        whisper_free_state(state);
        STT_LOGE("whisper_full_with_state failed");
        return "";
    }

    std::string full_text;
    int n_seg = whisper_full_n_segments_from_state(state);

    for (int i = 0; i < n_seg; i++) {
        if (g_stt_cancel.load()) break;

        // Skip segments where whisper is confident there was no speech.
        float no_speech_prob = whisper_full_get_segment_no_speech_prob_from_state(state, i);
        if (no_speech_prob > 0.8f) {
            STT_LOGD("Skipping segment %d: no_speech_prob=%.2f", i, no_speech_prob);
            continue;
        }

        const char *text = whisper_full_get_segment_text_from_state(state, i);
        int64_t     t_s0 = whisper_full_get_segment_t0_from_state(state, i) * 10; // cs → ms
        int64_t     t_s1 = whisper_full_get_segment_t1_from_state(state, i) * 10;

        if (text) {
            full_text += text;
            if (segments_out)
                segments_out->emplace_back(text, t_s0, t_s1);
            if (on_partial)
                on_partial(full_text);
        }
    }

    whisper_free_state(state);

    // Physical speech rate check: if the output has more words than a human
    // could physically speak in the available audio, it is hallucination.
    //
    // Human speech ceiling: ~6 words/sec (very fast). We allow 10 words/sec
    // (67% above ceiling) as headroom for fast speakers + short words.
    // For 1.14s audio, that means ≤11 words max. "nice nice nice × 28" in
    // 1.14s = 24.5 wps → definitively impossible → discard.
    //
    // This is a duration constraint, not a content filter. It does not strip
    // or alter legitimate speech — it rejects outputs that cannot be real.
    {
        int word_count = 0;
        bool in_word = false;
        for (unsigned char c : full_text) {
            if (std::isspace(c)) { in_word = false; }
            else if (!in_word)  { in_word = true; word_count++; }
        }
        const float max_words_per_sec = 10.0f;
        if (audio_sec > 0.0f && word_count > static_cast<int>(audio_sec * max_words_per_sec)) {
            STT_LOGD("Rejecting hallucination: %d words in %.2fs (>%.0f wps limit) — discarded",
                     word_count, audio_sec, max_words_per_sec);
            return "";
        }
    }

    STT_LOGD("Transcribed %d segments: \"%s\"", n_seg, full_text.c_str());
    return full_text;
}

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Public C API — STT
// ═══════════════════════════════════════════════════════════════════════════

extern "C" {

int dai_stt_init(
    const char *model_path,
    const char *language,
    int         translate,
    int         max_threads,
    int         use_gpu,
    int         use_vad,
    int         single_segment,
    int         no_context
) {
    std::lock_guard<std::mutex> lock(g_stt_mutex);

    if (g_stt_ctx) {
        whisper_free(g_stt_ctx);
        g_stt_ctx = nullptr;
    }

    g_stt_language      = (language && language[0]) ? language : "en";
    g_stt_translate     = (translate != 0);
    g_stt_threads       = max_threads;
    g_stt_use_gpu       = (use_gpu != 0);
    g_stt_use_vad       = (use_vad != 0);
    g_stt_single_segment = (single_segment != 0);
    g_stt_no_context    = (no_context != 0);

    struct whisper_context_params ctx_params = whisper_context_default_params();
    ctx_params.use_gpu = (use_gpu != 0);

    g_stt_ctx = whisper_init_from_file_with_params(model_path, ctx_params);
    if (!g_stt_ctx) {
        STT_LOGE("Failed to load model: %s", model_path);
        return 0;
    }

    // Beam search (beam_size=5) instead of greedy: maintains 5 candidate
    // sequences simultaneously. Paths that enter repetition loops score
    // poorly against novel continuations and get pruned early, solving the
    // phrase-repetition problem ("I ate an ice cream today" × 15) that the
    // word-rate guard cannot catch because the WPS is within normal range.
    // Encoder is the bottleneck on mobile so beam decode overhead is <2×.
    g_stt_params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    g_stt_params.beam_search.beam_size = 5;
    g_stt_params.language         = g_stt_language.c_str();
    g_stt_params.translate        = (translate != 0);
    g_stt_params.n_threads        = max_threads;
    g_stt_params.single_segment   = (single_segment != 0);
    g_stt_params.no_context       = (no_context != 0);
    g_stt_params.no_timestamps    = false;
    g_stt_params.print_special    = false;
    g_stt_params.print_progress   = false;
    g_stt_params.print_realtime   = false;
    g_stt_params.print_timestamps = false;
    // Suppress non-speech tokens (music notes, laughter, etc.)
    g_stt_params.suppress_nst     = true;
    // Temperature fallback: if beam search still gets stuck (entropy too low
    // or logprob too low after all beams), increment temperature and retry.
    g_stt_params.temperature_inc  = 0.2f;
    g_stt_params.entropy_thold    = 2.4f;
    // Reject segments where average token log-probability is too low.
    g_stt_params.logprob_thold    = -0.6f;

    STT_LOGI("Initialized: %s (lang=%s vad=%d gpu=%d threads=%d)",
             model_path, g_stt_language.c_str(), use_vad, use_gpu, max_threads);
    return 1;
}

char *dai_stt_transcribe(const float *samples, int n_samples) {
    if (!samples || n_samples <= 0) return strdup_c("");
    std::lock_guard<std::mutex> lock(g_stt_mutex);
    std::vector<float> audio(samples, samples + n_samples);
    std::string text = run_transcription(audio, nullptr, nullptr);
    return strdup_c(text);
}

char *dai_stt_transcribe_file(const char *wav_path) {
    if (!wav_path || !wav_path[0]) return strdup_c("");
    std::lock_guard<std::mutex> lock(g_stt_mutex);

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(wav_path, samples, sample_rate)) return strdup_c("");

    std::vector<float> samples_16k;
    resample_to_16k(samples, sample_rate, samples_16k);

    std::string text = run_transcription(samples_16k, nullptr, nullptr);
    return strdup_c(text);
}

char *dai_stt_transcribe_file_detailed(const char *wav_path) {
    static const char *EMPTY_JSON =
        "{\"text\":\"\",\"language\":\"en\",\"durationMs\":0,\"segments\":[]}";
    if (!wav_path || !wav_path[0]) return strdup_c(EMPTY_JSON);
    std::lock_guard<std::mutex> lock(g_stt_mutex);

    std::vector<float> samples;
    int sample_rate = 0;
    if (!read_wav_file(wav_path, samples, sample_rate)) return strdup_c(EMPTY_JSON);

    std::vector<float> samples_16k;
    resample_to_16k(samples, sample_rate, samples_16k);

    std::vector<std::tuple<std::string, int64_t, int64_t>> segs;
    std::string text = run_transcription(samples_16k, &segs, nullptr);

    int64_t duration_ms = static_cast<int64_t>(samples_16k.size()) * 1000 / WHISPER_SAMPLE_RATE;
    std::string json = build_result_json(text, segs, g_stt_language, duration_ms);
    return strdup_c(json);
}

void dai_stt_transcribe_stream(
    const float        *samples,
    int                 n_samples,
    dai_stt_partial_cb  on_partial,
    dai_stt_final_cb    on_final,
    dai_stt_error_cb    on_error,
    void               *user_data
) {
    if (!samples || n_samples <= 0) {
        if (on_error) on_error("Invalid audio input", user_data);
        return;
    }

    // Use unique_lock so we can release the mutex before firing the final
    // callback. Firing on_final while holding g_stt_mutex would deadlock if
    // the callback calls any STT API (e.g. shutdownStt) that re-acquires it.
    // Note: on_partial is fired inside run_transcription while the mutex is
    // still held. on_partial callbacks must NOT re-enter the STT API.
    std::unique_lock<std::mutex> lock(g_stt_mutex);

    if (!g_stt_ctx) {
        if (on_error) on_error("STT not initialized", user_data);
        return;
    }

    std::vector<float> audio(samples, samples + n_samples);
    std::vector<std::tuple<std::string, int64_t, int64_t>> segs;

    std::string text = run_transcription(
        audio, &segs,
        [&](const std::string &partial) {
            if (on_partial && !g_stt_cancel.load())
                on_partial(partial.c_str(), user_data);
        }
    );

    bool cancelled = g_stt_cancel.load();
    // audio.size() is post-VAD (run_transcription trims in-place) — consistent
    // with dai_stt_transcribe_file_detailed which also uses post-VAD sample count.
    int64_t duration_ms = static_cast<int64_t>(audio.size()) * 1000 / WHISPER_SAMPLE_RATE;
    std::string json = cancelled ? "" : build_result_json(text, segs, g_stt_language, duration_ms);

    // Release mutex before invoking external callbacks.
    lock.unlock();

    if (cancelled) {
        if (on_error) on_error("Cancelled", user_data);
    } else {
        if (on_final) on_final(json.c_str(), user_data);
    }
}

void dai_stt_cancel(void) {
    g_stt_cancel = true;
}

void dai_stt_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_stt_mutex);
    if (g_stt_ctx) {
        STT_LOGI("Shutdown");
        whisper_free(g_stt_ctx);
        g_stt_ctx = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Public C API — TTS (sherpa-onnx)
// ═══════════════════════════════════════════════════════════════════════════

#ifdef HAVE_SHERPA_ONNX

// Internal helper: float32 → int16 with clipping
static inline int16_t float_to_int16(float s) {
    if (s >  1.0f) s =  1.0f;
    if (s < -1.0f) s = -1.0f;
    return static_cast<int16_t>(s * 32767.0f);
}

int dai_tts_init(
    const char *model_path,
    const char *tokens_path,
    const char *data_dir,
    const char *voices_path,
    int         speaker_id,
    float       speech_rate,
    int         sample_rate,
    float       sentence_silence
) {
    std::lock_guard<std::mutex> lock(g_tts_mutex);

    if (g_tts) { SherpaOnnxDestroyOfflineTts(g_tts); g_tts = nullptr; }

    g_tts_speaker_id  = speaker_id;
    // sample_rate is informational only — the model determines actual output rate.
    // sentence_silence has no corresponding field in the sherpa-onnx C API.
    (void)sample_rate;

    float length_scale = (speech_rate > 0.0f) ? 1.0f / speech_rate : 1.0f;
    bool  is_kokoro    = voices_path && voices_path[0] != '\0';

    SherpaOnnxOfflineTtsModelConfig model_cfg;
    memset(&model_cfg, 0, sizeof(model_cfg));
    model_cfg.num_threads = 2;
    model_cfg.debug       = 0;
    model_cfg.provider    = "cpu";

    if (is_kokoro) {
        model_cfg.kokoro.model       = model_path;
        model_cfg.kokoro.voices      = voices_path;
        model_cfg.kokoro.tokens      = tokens_path ? tokens_path : "";
        model_cfg.kokoro.data_dir    = data_dir    ? data_dir    : "";
        model_cfg.kokoro.length_scale = length_scale;
    } else {
        model_cfg.vits.model         = model_path;
        model_cfg.vits.tokens        = tokens_path ? tokens_path : "";
        model_cfg.vits.data_dir      = data_dir    ? data_dir    : "";
        model_cfg.vits.noise_scale   = 0.667f;
        model_cfg.vits.noise_scale_w = 0.8f;
        model_cfg.vits.length_scale  = length_scale;
    }

    SherpaOnnxOfflineTtsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model             = model_cfg;
    cfg.max_num_sentences = 2;
    (void)sentence_silence; // sherpa-onnx C API has no sentence_silence field

    g_tts = SherpaOnnxCreateOfflineTts(&cfg);
    if (!g_tts) {
        TTS_LOGE("Failed to create TTS engine: model=%s", model_path);
        return 0;
    }

    TTS_LOGI("TTS ready (%s, rate=%dHz speaker=%d)",
             is_kokoro ? "Kokoro" : "VITS", sample_rate, speaker_id);
    return 1;
}

int16_t *dai_tts_synthesize(const char *text, int *out_length) {
    if (!out_length) return nullptr;
    if (!text || !text[0]) { *out_length = 0; return nullptr; }
    std::lock_guard<std::mutex> lock(g_tts_mutex);
    if (!g_tts) { TTS_LOGE("Not initialized"); *out_length = 0; return nullptr; }

    g_tts_cancel = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_tts_speaker_id.load(), 1.0f);

    if (!audio || audio->n == 0) {
        TTS_LOGE("No audio produced");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        *out_length = 0;
        return nullptr;
    }

    TTS_LOGD("Synthesized %d samples at %dHz", audio->n, audio->sample_rate);

    int16_t *out = static_cast<int16_t *>(malloc(audio->n * sizeof(int16_t)));
    if (out) {
        for (int i = 0; i < audio->n; i++) out[i] = float_to_int16(audio->samples[i]);
        *out_length = audio->n;
    } else {
        *out_length = 0;
    }

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return out;
}

int dai_tts_synthesize_to_file(const char *text, const char *output_path) {
    if (!text || !text[0] || !output_path || !output_path[0]) return 0;
    std::lock_guard<std::mutex> lock(g_tts_mutex);
    if (!g_tts) { TTS_LOGE("Not initialized"); return 0; }

    g_tts_cancel = false;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(g_tts, text, g_tts_speaker_id.load(), 1.0f);

    if (!audio || audio->n == 0) {
        TTS_LOGE("No audio produced");
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return 0;
    }

    // Convert float → int16 for WAV
    std::vector<int16_t> pcm(audio->n);
    for (int i = 0; i < audio->n; i++) pcm[i] = float_to_int16(audio->samples[i]);

    int sr = audio->sample_rate;
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    if (!write_wav_file(output_path, pcm, sr)) return 0;

    TTS_LOGI("Wrote %zu samples to %s", pcm.size(), output_path);
    return 1;
}

// Streaming context passed to the sherpa-onnx callback
struct TtsStreamState {
    dai_tts_chunk_cb     on_chunk;
    dai_tts_complete_cb  on_complete;
    dai_tts_error_cb     on_error;
    void                *user_data;
    std::atomic<bool>   *cancel;
};

// Called by sherpa-onnx as each sentence is synthesized. Return 1 = continue, 0 = stop.
static int32_t tts_stream_callback(const float *samples, int32_t n, void *arg) {
    auto *state = static_cast<TtsStreamState *>(arg);
    if (!samples || n == 0 || state->cancel->load()) return 0;

    // Convert float → int16 and deliver in 4096-sample chunks (~185ms at 22050Hz)
    const int CHUNK = 4096;
    for (int offset = 0; offset < n && !state->cancel->load(); offset += CHUNK) {
        int len = std::min(CHUNK, n - offset);
        std::vector<int16_t> pcm(len);
        for (int i = 0; i < len; i++) pcm[i] = float_to_int16(samples[offset + i]);
        if (state->on_chunk) state->on_chunk(pcm.data(), len, state->user_data);
    }
    return state->cancel->load() ? 0 : 1;
}

void dai_tts_synthesize_stream(
    const char          *text,
    dai_tts_chunk_cb     on_chunk,
    dai_tts_complete_cb  on_complete,
    dai_tts_error_cb     on_error,
    void                *user_data
) {
    if (!text || !text[0]) {
        if (on_error) on_error("Invalid text input", user_data);
        return;
    }

    // unique_lock so we can release before firing on_complete — same pattern
    // as STT stream. on_chunk fires from tts_stream_callback under the lock
    // (structural — sherpa holds the call stack); on_chunk must not re-enter
    // TTS APIs. on_complete is fired after unlock, safe for re-entry.
    std::unique_lock<std::mutex> lock(g_tts_mutex);
    if (!g_tts) {
        if (on_error) on_error("TTS not initialized", user_data);
        return;
    }

    g_tts_cancel = false;

    TtsStreamState state{on_chunk, on_complete, on_error, user_data, &g_tts_cancel};

    SherpaOnnxOfflineTtsGenerateWithCallbackWithContext(
        g_tts, text, g_tts_speaker_id.load(), 1.0f,
        tts_stream_callback, &state);

    bool cancelled = g_tts_cancel.load();
    lock.unlock();

    if (!cancelled) {
        if (on_complete) on_complete(user_data);
    }
}

void dai_tts_cancel(void) {
    g_tts_cancel = true;
}

void dai_tts_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_tts_mutex);
    if (g_tts) {
        TTS_LOGI("Shutdown");
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = nullptr;
    }
}

#else // !HAVE_SHERPA_ONNX

int dai_tts_init(const char *, const char *, const char *, const char *,
                 int, float, int, float) {
    TTS_LOGE("TTS not available (sherpa-onnx not compiled in)");
    return 0;
}
int16_t *dai_tts_synthesize(const char *, int *out_length) {
    if (out_length) *out_length = 0; return nullptr;
}
int dai_tts_synthesize_to_file(const char *, const char *) { return 0; }
void dai_tts_synthesize_stream(const char *, dai_tts_chunk_cb, dai_tts_complete_cb,
                                dai_tts_error_cb on_error, void *user_data) {
    if (on_error) on_error("TTS not available", user_data);
}
void dai_tts_cancel(void)   {}
void dai_tts_shutdown(void) {}

#endif // HAVE_SHERPA_ONNX

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Public C API — VAD (sherpa-onnx Silero VAD)
// ═══════════════════════════════════════════════════════════════════════════

#ifdef HAVE_SHERPA_ONNX

int dai_vad_init(const char *model_path, float threshold, int sample_rate) {
    std::lock_guard<std::mutex> lock(g_vad_mutex);

    if (g_vad) { SherpaOnnxDestroyVoiceActivityDetector(g_vad); g_vad = nullptr; }

    g_vad_threshold       = threshold;
    g_vad_sample_rate_val = sample_rate;
    g_vad_in_speech       = false;

    SherpaOnnxSileroVadModelConfig silero;
    memset(&silero, 0, sizeof(silero));
    silero.model                = model_path;
    silero.threshold            = threshold;
    silero.min_silence_duration = 0.25f;
    silero.min_speech_duration  = 0.1f;
    silero.max_speech_duration  = 30.0f;
    silero.window_size          = 512;  // 32ms at 16kHz

    SherpaOnnxVadModelConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.silero_vad  = silero;
    cfg.sample_rate = sample_rate;
    cfg.num_threads = 1;
    cfg.debug       = 0;

    // Buffer size: 10 seconds of audio
    g_vad = SherpaOnnxCreateVoiceActivityDetector(&cfg, 10.0f);
    if (!g_vad) {
        STT_LOGE("Failed to create Silero VAD: %s", model_path);
        return 0;
    }

    STT_LOGI("Silero VAD initialized: threshold=%.2f rate=%d", threshold, sample_rate);
    return 1;
}

int dai_vad_is_speech(const float *samples, int n_samples) {
    std::lock_guard<std::mutex> lock(g_vad_mutex);
    if (!g_vad) return 0;

    SherpaOnnxVoiceActivityDetectorAcceptWaveform(g_vad, samples, n_samples);
    SherpaOnnxVoiceActivityDetectorFlush(g_vad);

    int detected = 0;
    while (!SherpaOnnxVoiceActivityDetectorEmpty(g_vad)) {
        const SherpaOnnxSpeechSegment *seg = SherpaOnnxVoiceActivityDetectorFront(g_vad);
        if (seg->n > 0) detected = 1;
        SherpaOnnxDestroySpeechSegment(seg);
        SherpaOnnxVoiceActivityDetectorPop(g_vad);
    }
    return detected;
}

void dai_vad_process_stream(
    const float            *samples,
    int                     n_samples,
    dai_vad_speech_start_cb on_speech_start,
    dai_vad_speech_end_cb   on_speech_end,
    void                   *user_data
) {
    std::lock_guard<std::mutex> lock(g_vad_mutex);
    if (!g_vad) return;

    SherpaOnnxVoiceActivityDetectorAcceptWaveform(g_vad, samples, n_samples);

    while (!SherpaOnnxVoiceActivityDetectorEmpty(g_vad)) {
        const SherpaOnnxSpeechSegment *seg = SherpaOnnxVoiceActivityDetectorFront(g_vad);

        // Each segment represents a speech region — fire start then end
        if (seg->n > 0) {
            if (!g_vad_in_speech.exchange(true))
                if (on_speech_start) on_speech_start(user_data);
            g_vad_in_speech = false;
            if (on_speech_end) on_speech_end(user_data);
        }

        SherpaOnnxDestroySpeechSegment(seg);
        SherpaOnnxVoiceActivityDetectorPop(g_vad);
    }
}

void dai_vad_reset(void) {
    std::lock_guard<std::mutex> lock(g_vad_mutex);
    if (g_vad) SherpaOnnxVoiceActivityDetectorReset(g_vad);
    g_vad_in_speech = false;
}

void dai_vad_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_vad_mutex);
    if (g_vad) {
        SherpaOnnxDestroyVoiceActivityDetector(g_vad);
        g_vad = nullptr;
        STT_LOGI("Silero VAD shutdown");
    }
}

#else // !HAVE_SHERPA_ONNX

int  dai_vad_init(const char *, float, int) { return 0; }
int  dai_vad_is_speech(const float *, int)  { return 0; }
void dai_vad_process_stream(const float *, int,
                             dai_vad_speech_start_cb, dai_vad_speech_end_cb,
                             void *) {}
void dai_vad_reset(void)    {}
void dai_vad_shutdown(void) {}

#endif // HAVE_SHERPA_ONNX

// ═══════════════════════════════════════════════════════════════════════════
// MARK: - Shared utilities
// ═══════════════════════════════════════════════════════════════════════════

void dai_speech_free_string(char *ptr) {
    free(ptr);
}

void dai_speech_free_audio(int16_t *ptr) {
    free(ptr);
}

void dai_speech_shutdown_all(void) {
    dai_vad_shutdown();
    dai_stt_shutdown();
    dai_tts_shutdown();
}

} // extern "C"
