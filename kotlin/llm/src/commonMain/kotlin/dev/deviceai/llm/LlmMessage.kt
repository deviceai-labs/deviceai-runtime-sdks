package dev.deviceai.llm

enum class LlmRole { SYSTEM, USER, ASSISTANT }

data class LlmMessage(val role: LlmRole, val content: String)
