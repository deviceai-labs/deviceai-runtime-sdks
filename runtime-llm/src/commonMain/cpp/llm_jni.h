#ifndef LLM_JNI_H
#define LLM_JNI_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════
//                        LIFECYCLE
// ═══════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeInitLlm(
    JNIEnv *env, jobject obj,
    jstring modelPath,
    jint contextSize,
    jint maxThreads,
    jboolean useGpu
);

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeShutdown(
    JNIEnv *env, jobject obj
);

// ═══════════════════════════════════════════════════════════════
//                        GENERATION
// ═══════════════════════════════════════════════════════════════

JNIEXPORT jstring JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeGenerate(
    JNIEnv *env, jobject obj,
    jstring prompt,
    jstring systemPrompt,
    jint maxTokens,
    jfloat temperature,
    jfloat topP,
    jint topK,
    jfloat repeatPenalty
);

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeGenerateStream(
    JNIEnv *env, jobject obj,
    jstring prompt,
    jstring systemPrompt,
    jint maxTokens,
    jfloat temperature,
    jfloat topP,
    jint topK,
    jfloat repeatPenalty,
    jobject callback
);

JNIEXPORT void JNICALL
Java_dev_deviceai_llm_LlmBridge_nativeCancelGeneration(
    JNIEnv *env, jobject obj
);

#ifdef __cplusplus
}
#endif

#endif // LLM_JNI_H
