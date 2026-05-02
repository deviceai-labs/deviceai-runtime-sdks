/**
 * deviceai_tts_jni.cpp — Thin JNI bridge for TTS inference.
 *
 * All inference logic lives in features/tts/tts_engine.cpp (dai_tts_*).
 * This file only handles JNI type conversion.
 */

#include "deviceai_speech_jni.h"
#include "dai_tts.h"

#include <string>
#include <vector>

// ── JNI helpers ──────────────────────────────────────────────────────────────

static std::string jstring_to_string(JNIEnv *env, jstring jstr) {
    if (!jstr) return "";
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) return "";
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

// ── JNI Exports ──────────────────────────────────────────────────────────────

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitTts(
    JNIEnv *env, jobject,
    jstring modelPath, jstring tokensPath,
    jstring dataDir, jstring voicesPath,
    jint speakerId, jfloat speechRate
) {
    std::string model  = jstring_to_string(env, modelPath);
    std::string tokens = jstring_to_string(env, tokensPath);
    std::string data   = jstring_to_string(env, dataDir);
    std::string voices = jstring_to_string(env, voicesPath);

    bool ok = dai_tts_init(
        model.c_str(), tokens.c_str(), data.c_str(), voices.c_str(),
        speakerId, speechRate
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jshortArray JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesize(
    JNIEnv *env, jobject, jstring text
) {
    std::string input = jstring_to_string(env, text);
    int n_samples = 0;
    int16_t *samples = dai_tts_synthesize(input.c_str(), &n_samples);

    if (!samples || n_samples == 0) {
        if (samples) dai_tts_free_audio(samples);
        return env->NewShortArray(0);
    }

    jshortArray result = env->NewShortArray(n_samples);
    env->SetShortArrayRegion(result, 0, n_samples, reinterpret_cast<jshort*>(samples));
    dai_tts_free_audio(samples);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeToFile(
    JNIEnv *env, jobject, jstring text, jstring outputPath
) {
    std::string input = jstring_to_string(env, text);
    std::string path  = jstring_to_string(env, outputPath);

    return dai_tts_synthesize_to_file(input.c_str(), path.c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeSynthesizeStream(
    JNIEnv *env, jobject, jstring text, jobject callback
) {
    jclass cbClass = env->GetObjectClass(callback);
    jmethodID onChunk    = env->GetMethodID(cbClass, "onAudioChunk", "([S)V");
    jmethodID onComplete = env->GetMethodID(cbClass, "onComplete",   "()V");
    jmethodID onError    = env->GetMethodID(cbClass, "onError",      "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cbClass);

    if (!onChunk || !onComplete || !onError) return;

    std::string input = jstring_to_string(env, text);

    struct StreamCtx {
        JNIEnv *env;
        jobject callback;
        jmethodID onChunk, onComplete, onError;
    };
    StreamCtx ctx{env, callback, onChunk, onComplete, onError};

    dai_tts_synthesize_stream(
        input.c_str(),
        [](const int16_t *samples, int n_samples, void *user) {
            auto *c = static_cast<StreamCtx*>(user);
            jshortArray chunk = c->env->NewShortArray(n_samples);
            c->env->SetShortArrayRegion(chunk, 0, n_samples, reinterpret_cast<const jshort*>(samples));
            c->env->CallVoidMethod(c->callback, c->onChunk, chunk);
            c->env->DeleteLocalRef(chunk);
        },
        [](void *user) {
            auto *c = static_cast<StreamCtx*>(user);
            c->env->CallVoidMethod(c->callback, c->onComplete);
        },
        [](const char *msg, void *user) {
            auto *c = static_cast<StreamCtx*>(user);
            jstring jMsg = c->env->NewStringUTF(msg);
            c->env->CallVoidMethod(c->callback, c->onError, jMsg);
            c->env->DeleteLocalRef(jMsg);
        },
        &ctx
    );
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeCancelTts(JNIEnv *, jobject) {
    dai_tts_cancel();
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownTts(JNIEnv *, jobject) {
    dai_tts_shutdown();
}
