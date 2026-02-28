package dev.deviceai.llm

import kotlin.system.measureTimeMillis

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
actual object LlmBridge {

    init {
        System.loadLibrary("llm_jni")
    }

    actual fun initLlm(modelPath: String, config: LlmConfig): Boolean =
        nativeInitLlm(modelPath, config.contextSize, config.maxThreads, config.useGpu)

    actual fun shutdown() = nativeShutdown()

    actual fun generate(prompt: String, config: LlmConfig): LlmResult {
        var text = ""
        val ms = measureTimeMillis {
            text = nativeGenerate(
                prompt, config.systemPrompt,
                config.maxTokens, config.temperature,
                config.topP, config.topK, config.repeatPenalty
            )
        }
        return LlmResult(
            text = text,
            tokenCount = text.split(" ").size,  // approximate; native layer can refine
            promptTokenCount = prompt.split(" ").size,
            finishReason = FinishReason.STOP,
            generationTimeMs = ms
        )
    }

    actual fun generateStream(prompt: String, config: LlmConfig, callback: LlmStream) {
        val startMs = System.currentTimeMillis()
        val fullText = StringBuilder()

        val internalCallback = object : LlmStream {
            override fun onToken(token: String) {
                fullText.append(token)
                callback.onToken(token)
            }
            override fun onComplete(result: LlmResult) { /* handled below */ }
            override fun onError(message: String) = callback.onError(message)
        }

        nativeGenerateStream(
            prompt, config.systemPrompt,
            config.maxTokens, config.temperature,
            config.topP, config.topK, config.repeatPenalty,
            internalCallback
        )

        callback.onComplete(
            LlmResult(
                text = fullText.toString(),
                tokenCount = fullText.toString().split(" ").size,
                promptTokenCount = prompt.split(" ").size,
                finishReason = FinishReason.STOP,
                generationTimeMs = System.currentTimeMillis() - startMs
            )
        )
    }

    actual fun cancelGeneration() = nativeCancelGeneration()

    // ══════════════════════════════════════════════════════════════
    //                    NATIVE DECLARATIONS
    // ══════════════════════════════════════════════════════════════

    private external fun nativeInitLlm(
        modelPath: String, contextSize: Int, maxThreads: Int, useGpu: Boolean
    ): Boolean

    private external fun nativeShutdown()

    private external fun nativeGenerate(
        prompt: String, systemPrompt: String,
        maxTokens: Int, temperature: Float,
        topP: Float, topK: Int, repeatPenalty: Float
    ): String

    private external fun nativeGenerateStream(
        prompt: String, systemPrompt: String,
        maxTokens: Int, temperature: Float,
        topP: Float, topK: Int, repeatPenalty: Float,
        callback: LlmStream
    )

    private external fun nativeCancelGeneration()
}
