package dev.deviceai.llm

import dev.deviceai.llm.engine.LlmJniEngine
import kotlinx.coroutines.flow.Flow

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
actual object LlmBridge {
    actual fun initLlm(modelPath: String, config: LlmInitConfig) = LlmJniEngine.init(modelPath, config)
    actual fun shutdown() = LlmJniEngine.shutdown()
    actual fun generate(messages: List<LlmMessage>, config: LlmGenConfig) = LlmJniEngine.generate(messages, config)
    actual fun generateStream(messages: List<LlmMessage>, config: LlmGenConfig): Flow<String> = LlmJniEngine.generateStream(messages, config)
    actual fun cancelGeneration() = LlmJniEngine.cancelGeneration()
}
