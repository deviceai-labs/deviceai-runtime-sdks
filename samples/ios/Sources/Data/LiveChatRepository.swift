import DeviceAiCore
import DeviceAiLlm

// MARK: - Concrete implementation

final class LiveChatRepository: ChatRepository {
    private let cache = ChatSessionCache()

    func send(_ text: String, modelPath: String) async -> AsyncThrowingStream<String, Error> {
        await cache.send(text, modelPath: modelPath)
    }

    func cancel() {
        Task { await cache.cancel() }
    }

    func clearHistory() async {
        await cache.clearHistory()
    }
}

// MARK: - Chat session cache

private actor ChatSessionCache {
    private var session: ChatSession?
    private var currentPath: String?

    private func getSession(for path: String) -> ChatSession {
        if currentPath == path, let s = session { return s }
        let s = DeviceAI.llm.chat(modelPath: path) {
            $0.systemPrompt = "You are a helpful on-device AI assistant. Be concise."
            $0.maxTokens    = 512
            $0.temperature  = 0.7
        }
        session     = s
        currentPath = path
        return s
    }

    func send(_ text: String, modelPath: String) -> AsyncThrowingStream<String, Error> {
        let s = getSession(for: modelPath)
        return AsyncThrowingStream { continuation in
            Task {
                do {
                    for try await token in await s.send(text) { continuation.yield(token) }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
        }
    }

    func cancel() { session?.cancel() }
    func clearHistory() async { await session?.clearHistory() }
}
