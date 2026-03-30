package dev.deviceai.llm.rag

import dev.deviceai.llm.LlmGenConfig
import dev.deviceai.llm.LlmMessage
import dev.deviceai.llm.LlmRole

/**
 * Injects retrieved RAG context into the message list before it reaches the LLM engine.
 *
 * Called by each LlmCppBridge actual inside [generate] and [generateStream].
 * Returns the original list unchanged when [LlmGenConfig.ragStore] is null.
 */
internal object RagAugmentor {

    fun augment(messages: List<LlmMessage>, config: LlmGenConfig): List<LlmMessage> {
        val store = config.ragStore ?: return messages

        // Use the last user message as the retrieval query
        val query = messages.lastOrNull { it.role == LlmRole.USER }?.content
            ?: return messages

        val chunks = store.retrieve(query, config.ragTopK)
        if (chunks.isEmpty()) return messages

        require("{context}" in config.ragPromptTemplate) {
            "ragPromptTemplate must contain the placeholder \"{context}\""
        }

        val context  = chunks.joinToString("\n\n---\n\n") { it.text }
        val injected = config.ragPromptTemplate.replace("{context}", context)

        // Prepend to existing system message, or insert a new one at position 0
        val systemIdx = messages.indexOfFirst { it.role == LlmRole.SYSTEM }
        return if (systemIdx >= 0) {
            messages.toMutableList().also {
                it[systemIdx] = it[systemIdx].copy(
                    content = injected + "\n\n" + it[systemIdx].content
                )
            }
        } else {
            listOf(LlmMessage(LlmRole.SYSTEM, injected)) + messages
        }
    }
}
