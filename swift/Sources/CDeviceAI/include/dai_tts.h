/**
 * dai_tts.h — Platform-agnostic Text-to-Speech engine.
 *
 * Wraps sherpa-onnx C API (SherpaOnnxOfflineTts). Supports VITS and Kokoro
 * voice models. Called by both JNI (Android) and Swift (iOS).
 *
 * Compiles to no-op stubs when HAVE_SHERPA_ONNX is not defined.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the TTS engine.
 *
 * @param model_path   Path to .onnx model file.
 * @param tokens_path  Path to tokens.txt vocabulary file.
 * @param data_dir     espeak-ng data directory (for VITS phoneme models).
 * @param voices_path  Path to voices.bin for Kokoro models. Empty string for VITS.
 * @param speaker_id   Speaker index for multi-speaker models. -1 for default.
 * @param speech_rate   Speed multiplier (1.0 = normal).
 * @return             true on success.
 */
bool dai_tts_init(
    const char* model_path,
    const char* tokens_path,
    const char* data_dir,
    const char* voices_path,
    int speaker_id,
    float speech_rate
);

/**
 * Synthesize text to audio samples.
 * @param text     Text to synthesize.
 * @param out_len  Output: number of samples.
 * @return         Int16 audio samples (mono, ~22050 Hz). Caller frees with dai_tts_free_audio().
 *                 NULL on failure.
 */
int16_t* dai_tts_synthesize(const char* text, int* out_len);

/**
 * Synthesize text directly to a WAV file.
 * @return true on success.
 */
bool dai_tts_synthesize_to_file(const char* text, const char* output_path);

/**
 * Streaming synthesis with audio chunk callbacks.
 */
typedef void (*dai_tts_on_chunk_fn)(const int16_t* samples, int n_samples, void* ctx);
typedef void (*dai_tts_on_complete_fn)(void* ctx);
typedef void (*dai_tts_on_error_fn)(const char* message, void* ctx);

void dai_tts_synthesize_stream(
    const char* text,
    dai_tts_on_chunk_fn on_chunk,
    dai_tts_on_complete_fn on_complete,
    dai_tts_on_error_fn on_error,
    void* ctx
);

/** Cancel ongoing synthesis. Thread-safe. */
void dai_tts_cancel(void);

/** Release TTS resources and unload model. */
void dai_tts_shutdown(void);

/** Free audio samples returned by dai_tts_synthesize(). */
void dai_tts_free_audio(int16_t* samples);

#ifdef __cplusplus
}
#endif
