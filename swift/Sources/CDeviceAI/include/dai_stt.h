/**
 * dai_stt.h — Platform-agnostic Speech-to-Text engine.
 *
 * Wraps whisper.cpp with energy-based VAD, per-call state isolation,
 * and WAV file parsing. Called by both JNI (Android) and Swift (iOS).
 *
 * Global state: one model loaded at a time. Thread-safe via internal mutex.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transcription segment with timestamps.
 */
typedef struct {
    const char* text;
    int64_t     start_ms;
    int64_t     end_ms;
} dai_stt_segment_t;

/**
 * Detailed transcription result.
 */
typedef struct {
    char*              text;         /**< Full transcription text. Freed by dai_stt_free_result(). */
    dai_stt_segment_t* segments;     /**< Array of segments. */
    int                segment_count;
    char*              language;     /**< Detected/configured language. */
    int64_t            duration_ms;  /**< Audio duration in ms. */
} dai_stt_result_t;

/**
 * Initialize the STT engine with a whisper model.
 *
 * @param model_path     Absolute path to .bin model file (ggml format).
 * @param language       Language code ("en", "auto", etc.).
 * @param translate      If true, translate non-English speech to English.
 * @param max_threads    CPU threads for inference.
 * @param use_gpu        Metal on iOS/macOS, Vulkan on Android.
 * @param use_vad        Enable energy-based voice activity detection.
 * @param single_segment Force output into a single segment.
 * @param no_context     Don't use previous transcription as decoder prompt.
 * @return               true on success.
 */
bool dai_stt_init(
    const char* model_path,
    const char* language,
    bool translate,
    int max_threads,
    bool use_gpu,
    bool use_vad,
    bool single_segment,
    bool no_context
);

/**
 * Transcribe a WAV file to text.
 * @return Transcribed text. Caller must free with dai_stt_free_string(). NULL on failure.
 */
char* dai_stt_transcribe(const char* audio_path);

/**
 * Transcribe raw PCM audio samples.
 * @param samples   Float array of audio samples (16kHz, mono, normalized -1.0 to 1.0).
 * @param n_samples Number of samples.
 * @return          Transcribed text. Caller must free with dai_stt_free_string(). NULL on failure.
 */
char* dai_stt_transcribe_audio(const float* samples, int n_samples);

/**
 * Transcribe with detailed results including segments and timestamps.
 * @return Result struct. Caller must free with dai_stt_free_result(). NULL on failure.
 */
dai_stt_result_t* dai_stt_transcribe_detailed(const char* audio_path);

/**
 * Transcribe with streaming callbacks.
 *
 * NOTE: Callbacks are invoked while the internal mutex is held.
 * Callbacks MUST NOT re-enter the STT API (e.g., call dai_stt_cancel or
 * dai_stt_shutdown) — doing so will deadlock. Pointers in the result
 * passed to on_final are only valid for the duration of the callback;
 * callers must copy any data they need to retain.
 *
 * @param on_partial  Called with accumulated text after each segment.
 * @param on_final    Called with the complete result at the end.
 * @param on_error    Called on error.
 * @param ctx         Opaque pointer passed to callbacks.
 */
typedef void (*dai_stt_on_partial_fn)(const char* text, void* ctx);
typedef void (*dai_stt_on_final_fn)(const dai_stt_result_t* result, void* ctx);
typedef void (*dai_stt_on_error_fn)(const char* message, void* ctx);

void dai_stt_transcribe_stream(
    const float* samples,
    int n_samples,
    dai_stt_on_partial_fn on_partial,
    dai_stt_on_final_fn on_final,
    dai_stt_on_error_fn on_error,
    void* ctx
);

/** Cancel ongoing transcription. Thread-safe. */
void dai_stt_cancel(void);

/** Release STT resources and unload model. */
void dai_stt_shutdown(void);

/** Free a string returned by dai_stt_transcribe/transcribe_audio. */
void dai_stt_free_string(char* str);

/** Free a result returned by dai_stt_transcribe_detailed. */
void dai_stt_free_result(dai_stt_result_t* result);

#ifdef __cplusplus
}
#endif
