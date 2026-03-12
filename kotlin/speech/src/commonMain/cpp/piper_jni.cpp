/**
 * tts_jni.cpp  (formerly piper_jni.cpp)
 *
 * Thin JNI delegate — converts Android/JVM types to plain C, calls
 * the unified dai_tts_* API in deviceai_speech_engine.{h,cpp}.
 *
 * No TTS logic lives here. Engine is sherpa-onnx (VITS or Kokoro).
 */

#include "speech_jni.h"
#include "deviceai_speech_engine.h"

#include <jni.h>
#include <string>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string jstring_to_std(JNIEnv *env, jstring js) {
    if (!js) return "";
    const char *chars = env->GetStringUTFChars(js, nullptr);
    std::string s(chars);
    env->ReleaseStringUTFChars(js, chars);
    return s;
}

// ─── Streaming context ────────────────────────────────────────────────────────

struct TtsStreamCtx {
    JavaVM    *jvm;
    jobject    callback;   // global ref
    jmethodID  onChunk;
    jmethodID  onComplete;
    jmethodID  onError;
};

static void tts_on_chunk(const int16_t *samples, int count, void *user_data) {
    auto *ctx = static_cast<TtsStreamCtx *>(user_data);
    JNIEnv *env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jshortArray arr = env->NewShortArray(count);
    env->SetShortArrayRegion(arr, 0, count, samples);
    env->CallVoidMethod(ctx->callback, ctx->onChunk, arr);
    env->DeleteLocalRef(arr);
}

static void tts_on_complete(void *user_data) {
    auto *ctx = static_cast<TtsStreamCtx *>(user_data);
    JNIEnv *env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    env->CallVoidMethod(ctx->callback, ctx->onComplete);
}

static void tts_on_error(const char *error, void *user_data) {
    auto *ctx = static_cast<TtsStreamCtx *>(user_data);
    JNIEnv *env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jstring je = env->NewStringUTF(error ? error : "Unknown error");
    env->CallVoidMethod(ctx->callback, ctx->onError, je);
    env->DeleteLocalRef(je);
}

// ─── JNI exports ─────────────────────────────────────────────────────────────

extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitTts(
    JNIEnv *env, jobject,
    jstring modelPath, jstring tokensPath, jstring dataDir, jstring voicesPath,
    jint speakerId, jfloat speechRate, jint sampleRate, jfloat sentenceSilence
) {
    std::string model   = jstring_to_std(env, modelPath);
    std::string tokens  = jstring_to_std(env, tokensPath);
    std::string data    = jstring_to_std(env, dataDir);
    std::string voices  = jstring_to_std(env, voicesPath);
    return dai_tts_init(
        model.c_str(), tokens.c_str(), data.c_str(), voices.c_str(),
        (int)speakerId, (float)speechRate, (int)sampleRate, (float)sentenceSilence
    ) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(JNIEnv *env, jobject, jstring text) {
    std::string input = jstring_to_std(env, text);
    int out_length = 0;
    int16_t *audio = dai_tts_synthesize(input.c_str(), &out_length);
    if (!audio || out_length == 0) return env->NewShortArray(0);
    jshortArray arr = env->NewShortArray(out_length);
    env->SetShortArrayRegion(arr, 0, out_length, audio);
    dai_speech_free_audio(audio);
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv *env, jobject,
    jstring text, jstring outputPath
) {
    std::string input = jstring_to_std(env, text);
    std::string path  = jstring_to_std(env, outputPath);
    return dai_tts_synthesize_to_file(input.c_str(), path.c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject,
    jstring text, jobject callback
) {
    jclass cbClass = env->GetObjectClass(callback);
    TtsStreamCtx ctx;
    env->GetJavaVM(&ctx.jvm);
    ctx.callback   = env->NewGlobalRef(callback);
    ctx.onChunk    = env->GetMethodID(cbClass, "onAudioChunk", "([S)V");
    ctx.onComplete = env->GetMethodID(cbClass, "onComplete",   "()V");
    ctx.onError    = env->GetMethodID(cbClass, "onError",      "(Ljava/lang/String;)V");

    std::string input = jstring_to_std(env, text);
    dai_tts_synthesize_stream(input.c_str(), tts_on_chunk, tts_on_complete, tts_on_error, &ctx);

    env->DeleteGlobalRef(ctx.callback);
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(JNIEnv *, jobject) {
    dai_tts_cancel();
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(JNIEnv *, jobject) {
    dai_tts_shutdown();
}

} // extern "C"
