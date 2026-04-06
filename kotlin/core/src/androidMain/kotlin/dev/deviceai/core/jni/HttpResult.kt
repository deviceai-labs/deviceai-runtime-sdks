package dev.deviceai.core.jni

/**
 * HTTP response returned by [JniHttpExecutor] to the C++ layer.
 *
 * Field names and types must match the JNI field ID lookups in dai_core_jni.cpp:
 *   env->GetFieldID(result_class, "statusCode", "I")
 *   env->GetFieldID(result_class, "body",       "Ljava/lang/String;")
 *   env->GetFieldID(result_class, "error",      "Ljava/lang/String;")
 */
data class HttpResult(
    val statusCode: Int,
    val body: String?,
    val error: String?,
)
