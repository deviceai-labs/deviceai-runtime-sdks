import Foundation
import DeviceAiCore

/// A stateful LLM conversation session.
///
/// Create via `DeviceAI.llm.chat(modelPath:)` — never instantiate directly.
///
/// ## Usage
///
/// ```swift
/// let session = DeviceAI.llm.chat(modelPath: "/path/to/model.gguf") {
///     $0.systemPrompt = "You are a helpful assistant."
///     $0.temperature  = 0.8
/// }
///
/// // Streaming (recommended for UI — users see tokens as they arrive)
/// for try await token in session.send("What is Swift?") {
///     print(token, terminator: "")
/// }
///
/// // Await full response
/// let reply = try await session.sendAsync("Summarise the above.")
///
/// // Multi-turn — history is automatic
/// session.clearHistory()   // reset conversation, model stays loaded
/// await session.close()    // unload model, free all resources
/// ```
///
/// ## Concurrency contract
///
/// - `ChatSession` is an `actor` — `send()` calls are automatically serialized.
///   Calling `send()` while a previous stream is active queues after it completes.
/// - `cancel()` is `nonisolated` — callable from any thread/task without `await`.
///   It sets an atomic flag; the active stream drains and completes with `.cancelled`.
/// - `close()` is actor-isolated and idempotent — safe to call multiple times.
///   It waits for any active stream to finish before releasing native resources.
/// - `deinit` calls `close()` automatically — no resource leaks on deallocation.
/// - All methods throw `DeviceAiError.sessionClosed` after `close()` is called.
///
/// ## Kotlin parallel
///
/// Mirrors `ChatSession` in kotlin/llm — identical public API surface.
/// `send()` returns `AsyncThrowingStream` instead of `Flow<String>`.
/// `sendAsync()` replaces `sendBlocking()` — same semantics, Swift concurrency.
public actor ChatSession {

    private let config:     ChatConfig
    private let engine:     LlamaEngine
    private let loadTask:   Task<Void, Error>
    private var history:    [ChatTurn] = []
    private var closed = false

    init(modelPath: String, config: ChatConfig) {
        self.config  = config
        self.engine  = LlamaEngine()
        // Keep a reference so send() can await it — guarantees the model is
        // ready before any generation attempt, regardless of timing.
        let e = engine
        self.loadTask = Task { try await e.load(modelPath: modelPath, config: config) }
    }

    // ── Read-only state ───────────────────────────────────────────────────────

    /// Completed conversation turns (user + assistant pairs).
    /// In-progress streaming turns are not included until the stream completes.
    public var turns: [ChatTurn] { history }

    /// `true` once `close()` has been called.
    public var isClosed: Bool { closed }

    // ── Messaging ─────────────────────────────────────────────────────────────

    /// Stream a response token by token.
    ///
    /// History is updated automatically:
    /// - User turn added immediately.
    /// - Assistant turn appended when the stream completes successfully.
    /// - User turn rolled back on error, allowing a clean retry.
    public func send(_ text: String, overrideConfig: ChatConfig? = nil) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            Task {
                do {
                    try await self.loadTask.value   // wait for model to finish loading
                    try self.assertOpen()
                    let userTurn = ChatTurn(role: .user, content: text)
                    self.history.append(userTurn)

                    let messages = self.buildMessages(overrideConfig: overrideConfig)
                    let genConfig = (overrideConfig ?? self.config).toGenConfig()
                    var reply = ""

                    for try await token in self.engine.generateStream(messages: messages, config: genConfig) {
                        reply += token
                        continuation.yield(token)
                    }

                    self.history.append(ChatTurn(role: .assistant, content: reply))
                    continuation.finish()
                } catch {
                    // Roll back user turn so caller can retry cleanly
                    if self.history.last?.isUser == true { self.history.removeLast() }
                    continuation.finish(throwing: error)
                }
            }
        }
    }

    /// Await the full response (non-streaming).
    /// Equivalent to collecting the entire `send()` stream.
    public func sendAsync(_ text: String, overrideConfig: ChatConfig? = nil) async throws -> String {
        var full = ""
        for try await token in send(text, overrideConfig: overrideConfig) { full += token }
        return full
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Cancel any in-progress generation. Safe to call from any thread.
    /// The active stream completes with `.cancelled` — no error thrown to the caller.
    public nonisolated func cancel() { engine.cancel() }

    /// Clear conversation history. The model stays loaded and the session remains usable.
    public func clearHistory() { history.removeAll() }

    /// Unload the model and release all native resources.
    ///
    /// **Idempotent** — calling multiple times is safe.
    /// All subsequent method calls throw `DeviceAiError.sessionClosed`.
    public func close() async {
        guard !closed else { return }
        closed = true
        await engine.close()
        DeviceAiLogger.info("ChatSession", "Closed.")
    }

    // NOTE: async close() cannot be awaited from deinit (SE-0371).
    // cancel() is nonisolated+synchronous — stops any in-flight C++ work.
    // The engine's own deinit releases native resources.
    deinit { engine.cancel() }

    // ── Private ───────────────────────────────────────────────────────────────

    private func assertOpen() throws {
        guard !closed else { throw DeviceAiError.sessionClosed }
    }

    private func buildMessages(overrideConfig: ChatConfig?) -> [LlmMessage] {
        let cfg = overrideConfig ?? config
        var messages: [LlmMessage] = [.system(cfg.systemPrompt)]
        messages += history.map { LlmMessage(role: $0.role, content: $0.content) }
        return messages
    }
}

// ── ChatConfig → engine config mapping ───────────────────────────────────────

private extension ChatConfig {
    func toGenConfig() -> LlmGenConfig {
        LlmGenConfig(
            maxTokens:    maxTokens,
            temperature:  temperature,
            topP:         topP,
            topK:         topK,
            repeatPenalty: repeatPenalty
        )
    }
}
