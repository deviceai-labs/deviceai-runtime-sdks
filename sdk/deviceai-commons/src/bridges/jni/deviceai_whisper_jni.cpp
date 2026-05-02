/**
 * deviceai_whisper_jni.cpp — Thin JNI bridge for STT inference.
 *
 * All inference logic lives in features/stt/stt_engine.cpp (dai_stt_*).
 * This file only handles JNI type conversion and JNI object construction.
 */

#include "deviceai_speech_jni.h"
#include "dai_stt.h"

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

static jobject build_jni_result(JNIEnv *env, const dai_stt_result_t *result) {
    jclass resultClass  = env->FindClass("dev/deviceai/TranscriptionResult");
    jclass segmentClass = env->FindClass("dev/deviceai/Segment");
    jclass listClass    = env->FindClass("java/util/ArrayList");

    if (!resultClass || !segmentClass || !listClass) return nullptr;

    jmethodID resultCtor  = env->GetMethodID(resultClass,  "<init>",
        "(Ljava/lang/String;Ljava/util/List;Ljava/lang/String;J)V");
    jmethodID listCtor    = env->GetMethodID(listClass,    "<init>",  "()V");
    jmethodID listAdd     = env->GetMethodID(listClass,    "add",     "(Ljava/lang/Object;)Z");
    jmethodID segmentCtor = env->GetMethodID(segmentClass, "<init>",  "(Ljava/lang/String;JJ)V");

    jobject segmentList = env->NewObject(listClass, listCtor);

    if (result && result->segments) {
        for (int i = 0; i < result->segment_count; i++) {
            jstring segText = env->NewStringUTF(result->segments[i].text ? result->segments[i].text : "");
            jobject segment = env->NewObject(segmentClass, segmentCtor,
                segText, (jlong)result->segments[i].start_ms, (jlong)result->segments[i].end_ms);
            env->DeleteLocalRef(segText);
            env->CallBooleanMethod(segmentList, listAdd, segment);
            env->DeleteLocalRef(segment);
        }
    }

    jstring fullStr = env->NewStringUTF(result && result->text ? result->text : "");
    jstring langStr = env->NewStringUTF(result && result->language ? result->language : "");
    jlong durationMs = result ? result->duration_ms : 0;

    jobject jResult = env->NewObject(resultClass, resultCtor, fullStr, segmentList, langStr, durationMs);

    env->DeleteLocalRef(fullStr);
    env->DeleteLocalRef(langStr);
    env->DeleteLocalRef(segmentList);
    env->DeleteLocalRef(segmentClass);
    env->DeleteLocalRef(listClass);
    env->DeleteLocalRef(resultClass);

    return jResult;
}

// ── JNI Exports ──────────────────────────────────────────────────────────────

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_SpeechBridge_nativeInitStt(
    JNIEnv *env, jobject,
    jstring modelPath, jstring language, jboolean translate,
    jint maxThreads, jboolean useGpu, jboolean useVad,
    jboolean singleSegment, jboolean noContext
) {
    std::string path = jstring_to_string(env, modelPath);
    std::string lang = jstring_to_string(env, language);

    bool ok = dai_stt_init(
        path.c_str(), lang.c_str(), translate,
        maxThreads, useGpu, useVad, singleSegment, noContext
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribe(
    JNIEnv *env, jobject, jstring audioPath
) {
    std::string path = jstring_to_string(env, audioPath);
    char *result = dai_stt_transcribe(path.c_str());

    if (!result) return env->NewStringUTF("");
    jstring jResult = env->NewStringUTF(result);
    dai_stt_free_string(result);
    return jResult;
}

JNIEXPORT jobject JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeDetailed(
    JNIEnv *env, jobject, jstring audioPath
) {
    std::string path = jstring_to_string(env, audioPath);
    dai_stt_result_t *result = dai_stt_transcribe_detailed(path.c_str());

    jobject jResult = build_jni_result(env, result);
    if (result) dai_stt_free_result(result);

    return jResult ? jResult : build_jni_result(env, nullptr);
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeAudio(
    JNIEnv *env, jobject, jfloatArray samples
) {
    jsize len = env->GetArrayLength(samples);
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);

    char *result = dai_stt_transcribe_audio(data, len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    if (!result) return env->NewStringUTF("");
    jstring jResult = env->NewStringUTF(result);
    dai_stt_free_string(result);
    return jResult;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeTranscribeStream(
    JNIEnv *env, jobject, jfloatArray samples, jobject callback
) {
    jclass cbClass = env->GetObjectClass(callback);
    jmethodID onPartial = env->GetMethodID(cbClass, "onPartialResult", "(Ljava/lang/String;)V");
    jmethodID onFinal   = env->GetMethodID(cbClass, "onFinalResult",
                                           "(Ldev/deviceai/TranscriptionResult;)V");
    jmethodID onError   = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cbClass);

    if (!onPartial || !onFinal || !onError) return;

    jsize len = env->GetArrayLength(samples);
    jfloat *data = env->GetFloatArrayElements(samples, nullptr);
    std::vector<float> audio(data, data + len);
    env->ReleaseFloatArrayElements(samples, data, 0);

    struct StreamCtx {
        JNIEnv *env;
        jobject callback;
        jmethodID onPartial, onFinal, onError;
    };
    StreamCtx ctx{env, callback, onPartial, onFinal, onError};

    dai_stt_transcribe_stream(
        audio.data(), (int)audio.size(),
        [](const char *text, void *user) {
            auto *c = static_cast<StreamCtx*>(user);
            jstring jText = c->env->NewStringUTF(text);
            c->env->CallVoidMethod(c->callback, c->onPartial, jText);
            c->env->DeleteLocalRef(jText);
        },
        [](const dai_stt_result_t *result, void *user) {
            auto *c = static_cast<StreamCtx*>(user);
            jobject jResult = build_jni_result(c->env, result);
            if (jResult) {
                c->env->CallVoidMethod(c->callback, c->onFinal, jResult);
                c->env->DeleteLocalRef(jResult);
            }
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
Java_dev_deviceai_SpeechBridge_nativeCancelStt(JNIEnv *, jobject) {
    dai_stt_cancel();
}

JNIEXPORT void JNICALL
Java_dev_deviceai_SpeechBridge_nativeShutdownStt(JNIEnv *, jobject) {
    dai_stt_shutdown();
}
