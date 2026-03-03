package dev.deviceai.llm

import dev.deviceai.llm.native.*
import kotlinx.cinterop.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.SendChannel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.flowOn
import kotlin.time.measureTime

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
@OptIn(ExperimentalForeignApi::class)
actual object LlmBridge {

    actual fun initLlm(modelPath: String, config: LlmInitConfig): Boolean =
        llm_init(modelPath, config.contextSize, config.maxThreads, config.useGpu)

    actual fun shutdown() = llm_shutdown()

    actual fun generate(messages: List<LlmMessage>, config: LlmGenConfig): LlmResult {
        var text = ""
        val elapsed = measureTime {
            memScoped {
                val rolesArr    = allocArray<CPointerVar<ByteVar>>(messages.size)
                val contentsArr = allocArray<CPointerVar<ByteVar>>(messages.size)
                messages.forEachIndexed { i, msg ->
                    rolesArr[i]    = msg.role.name.lowercase().cstr.getPointer(this)
                    contentsArr[i] = msg.content.cstr.getPointer(this)
                }
                val result = llm_generate(
                    rolesArr, contentsArr, messages.size,
                    config.maxTokens, config.temperature,
                    config.topP, config.topK, config.repeatPenalty
                )
                text = result?.toKString()?.also { llm_free_string(result) } ?: ""
            }
        }
        return LlmResult(
            text = text,
            tokenCount = text.split(" ").size,
            promptTokenCount = 0,
            finishReason = FinishReason.STOP,
            generationTimeMs = elapsed.inWholeMilliseconds
        )
    }

    actual fun generateStream(messages: List<LlmMessage>, config: LlmGenConfig): Flow<String> =
        channelFlow {
            val ref = StableRef.create(this as SendChannel<String>)

            val onToken = staticCFunction { token: CPointer<ByteVar>?, user: COpaquePointer? ->
                val ch = user!!.asStableRef<SendChannel<String>>().get()
                val piece = token?.toKString() ?: return@staticCFunction
                ch.trySend(piece)
            }

            val onError = staticCFunction { message: CPointer<ByteVar>?, user: COpaquePointer? ->
                val ch = user!!.asStableRef<SendChannel<String>>().get()
                ch.close(RuntimeException(message?.toKString() ?: "Unknown error"))
            }

            memScoped {
                val rolesArr    = allocArray<CPointerVar<ByteVar>>(messages.size)
                val contentsArr = allocArray<CPointerVar<ByteVar>>(messages.size)
                messages.forEachIndexed { i, msg ->
                    rolesArr[i]    = msg.role.name.lowercase().cstr.getPointer(this)
                    contentsArr[i] = msg.content.cstr.getPointer(this)
                }
                llm_generate_stream(
                    rolesArr, contentsArr, messages.size,
                    config.maxTokens, config.temperature,
                    config.topP, config.topK, config.repeatPenalty,
                    onToken, onError,
                    ref.asCPointer()
                )
            }

            ref.dispose()
        }.flowOn(Dispatchers.Default)

    actual fun cancelGeneration() = llm_cancel()
}
