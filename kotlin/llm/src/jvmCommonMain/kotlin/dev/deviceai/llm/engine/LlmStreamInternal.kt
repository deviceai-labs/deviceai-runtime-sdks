package dev.deviceai.llm.engine

/**
 * Internal JNI callback interface — implementation detail of [LlmJniEngine].
 * Not part of the public SDK API. Callers use Flow operators instead.
 */
internal interface LlmStreamInternal {
    fun onToken(token: String)
    fun onError(message: String)
}
