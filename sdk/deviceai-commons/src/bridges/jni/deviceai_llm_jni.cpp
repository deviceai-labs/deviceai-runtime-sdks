/**
 * deviceai_llm_jni.cpp — Thin JNI bridge for LLM inference.
 *
 * All inference logic lives in features/llm/llm_engine.cpp (dai_llm_*).
 * This file only handles JNI type conversion (jstring ↔ const char*, etc.).
 */

#include "deviceai_llm_jni.h"
#include "dai_llm.h"

#include <string>
#include <vector>

// ── JNI helpers ──────────────────────────────────────────────────────────────

static std::string jstring_to_std(JNIEnv *env, jstring js) {
    if (!js) return "";
    const char *chars = env->GetStringUTFChars(js, nullptr);
    std::string s(chars);
    env->ReleaseStringUTFChars(js, chars);
    return s;
}

// ── JNI Exports ──────────────────────────────────────────────────────────────

extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_llm_engine_LlmJniEngine_nativeInit(
    JNIEnv *env, jobject, jstring jModelPath,
    jint maxThreads, jboolean useGpu
) {
    std::string path = jstring_to_std(env, jModelPath);
    return dai_llm_init(path.c_str(), maxThreads, useGpu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_engine_LlmJniEngine_nativeShutdown(JNIEnv *, jobject) {
    dai_llm_shutdown();
}

JNIEXPORT jstring JNICALL
Java_dev_deviceai_llm_engine_LlmJniEngine_nativeGenerate(
    JNIEnv *env, jobject,
    jobjectArray jRoles, jobjectArray jContents,
    jint maxTokens, jfloat temperature,
    jfloat topP, jint topK, jfloat repeatPenalty
) {
    int count = env->GetArrayLength(jRoles);
    std::vector<std::string> role_strs(count), content_strs(count);
    std::vector<const char*> roles(count), contents(count);

    for (int i = 0; i < count; i++) {
        jstring jRole = (jstring)env->GetObjectArrayElement(jRoles, i);
        jstring jContent = (jstring)env->GetObjectArrayElement(jContents, i);
        role_strs[i]    = jstring_to_std(env, jRole);
        content_strs[i] = jstring_to_std(env, jContent);
        env->DeleteLocalRef(jRole);
        env->DeleteLocalRef(jContent);
        roles[i]    = role_strs[i].c_str();
        contents[i] = content_strs[i].c_str();
    }

    char* result = dai_llm_generate(
        roles.data(), contents.data(), count,
        maxTokens, temperature, topP, topK, repeatPenalty
    );

    if (!result) return env->NewStringUTF("");
    jstring jResult = env->NewStringUTF(result);
    dai_llm_free_string(result);
    return jResult;
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_engine_LlmJniEngine_nativeGenerateStream(
    JNIEnv *env, jobject,
    jobjectArray jRoles, jobjectArray jContents,
    jint maxTokens, jfloat temperature,
    jfloat topP, jint topK, jfloat repeatPenalty,
    jobject jCallback
) {
    int count = env->GetArrayLength(jRoles);
    std::vector<std::string> role_strs(count), content_strs(count);
    std::vector<const char*> roles(count), contents(count);

    for (int i = 0; i < count; i++) {
        jstring jRole = (jstring)env->GetObjectArrayElement(jRoles, i);
        jstring jContent = (jstring)env->GetObjectArrayElement(jContents, i);
        role_strs[i]    = jstring_to_std(env, jRole);
        content_strs[i] = jstring_to_std(env, jContent);
        env->DeleteLocalRef(jRole);
        env->DeleteLocalRef(jContent);
        roles[i]    = role_strs[i].c_str();
        contents[i] = content_strs[i].c_str();
    }

    // Set up JNI callback
    jclass cbClass    = env->GetObjectClass(jCallback);
    jmethodID onToken = env->GetMethodID(cbClass, "onToken", "(Ljava/lang/String;)V");
    jmethodID onError = env->GetMethodID(cbClass, "onError", "(Ljava/lang/String;)V");

    jobject globalCb = env->NewGlobalRef(jCallback);
    JavaVM *jvm;
    env->GetJavaVM(&jvm);

    struct CallbackCtx {
        JavaVM *jvm;
        jobject cb;
        jmethodID onToken;
        jmethodID onError;
    };
    CallbackCtx ctx{jvm, globalCb, onToken, onError};

    dai_llm_generate_stream(
        roles.data(), contents.data(), count,
        maxTokens, temperature, topP, topK, repeatPenalty,
        // on_token
        [](const char* token, void* user) {
            auto* c = static_cast<CallbackCtx*>(user);
            JNIEnv *e;
#ifdef ANDROID
            c->jvm->AttachCurrentThread(&e, nullptr);
#else
            c->jvm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
#endif
            jstring jPiece = e->NewStringUTF(token);
            e->CallVoidMethod(c->cb, c->onToken, jPiece);
            e->DeleteLocalRef(jPiece);
        },
        // on_error
        [](const char* msg, void* user) {
            auto* c = static_cast<CallbackCtx*>(user);
            JNIEnv *e;
#ifdef ANDROID
            c->jvm->AttachCurrentThread(&e, nullptr);
#else
            c->jvm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
#endif
            jstring jMsg = e->NewStringUTF(msg);
            e->CallVoidMethod(c->cb, c->onError, jMsg);
            e->DeleteLocalRef(jMsg);
        },
        &ctx
    );

    env->DeleteGlobalRef(globalCb);
}

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_engine_LlmJniEngine_nativeCancel(JNIEnv *, jobject) {
    dai_llm_cancel();
}

} // extern "C"
