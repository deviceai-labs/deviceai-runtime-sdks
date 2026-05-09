import Foundation
import DeviceAI
import CDeviceAI

/// A stateful LLM conversation session.
///
/// ```swift
/// let session = try await ChatSession(modelPath: path) {
///     $0.systemPrompt = "You are a helpful assistant."
/// }
/// for try await token in try session.send("What is Swift?") {
///     print(token, terminator: "")
/// }
/// session.close()
/// ```
public final class ChatSession: @unchecked Sendable {
    private let modelId: String
    private let config: ChatConfig
    private let lock = NSLock()
    private var _history: [LlmMessage] = []

    public private(set) var isReady: Bool = false

    public var history: [ChatTurn] {
        lock.lock()
        defer { lock.unlock() }
        var turns: [ChatTurn] = []
        var i = 0
        while i + 1 < _history.count {
            if _history[i].role == .user && _history[i + 1].role == .assistant {
                turns.append(ChatTurn(user: _history[i].content, assistant: _history[i + 1].content))
            }
            i += 2
        }
        return turns
    }

    public init(modelPath: String, configure: ((inout ChatConfig) -> Void)? = nil) async throws {
        var cfg = ChatConfig()
        configure?(&cfg)
        self.config = cfg
        self.modelId = (modelPath as NSString).lastPathComponent

        let startMs = currentTimeMs()
        let ok = dai_llm_init(modelPath, Int32(cfg.threads), cfg.useGpu)
        guard ok else {
            throw DeviceAIError.initFailed(reason: "Failed to load model: \(modelPath)")
        }
        isReady = true
        DeviceAI.shared.recordEvent(.modelLoad(module: "llm", modelId: modelId, durationMs: currentTimeMs() - startMs))
    }

    /// Send a user message and stream the response token by token.
    public func send(_ text: String) throws -> AsyncThrowingStream<String, Error> {
        guard !text.trimmingCharacters(in: .whitespaces).isEmpty else {
            throw DeviceAIError.inferenceFailed(reason: "Message cannot be empty")
        }

        lock.lock()
        _history.append(LlmMessage(role: .user, content: text))
        let messages = [LlmMessage(role: .system, content: config.systemPrompt)] + _history
        lock.unlock()

        let cfg = self.config
        let modelId = self.modelId
        let inferenceStartMs = currentTimeMs()

        return AsyncThrowingStream { continuation in
            let roles = messages.map { $0.role.rawValue }
            let contents = messages.map { $0.content }

            // Build C string arrays
            let cRoles = roles.map { strdup($0) }
            let cContents = contents.map { strdup($0) }
            defer {
                cRoles.forEach { free($0) }
                cContents.forEach { free($0) }
            }

            var cRolePtrs: [UnsafePointer<CChar>?] = cRoles.map { UnsafePointer($0) }
            var cContentPtrs: [UnsafePointer<CChar>?] = cContents.map { UnsafePointer($0) }

            // Streaming context passed through the C void* callback
            final class StreamCtx {
                var reply = ""
                var tokenCount = 0
                var ttftMs: Int64?
                let startMs: Int64
                let continuation: AsyncThrowingStream<String, Error>.Continuation

                init(startMs: Int64, continuation: AsyncThrowingStream<String, Error>.Continuation) {
                    self.startMs = startMs
                    self.continuation = continuation
                }
            }

            let streamCtx = StreamCtx(startMs: inferenceStartMs, continuation: continuation)
            let ctxPtr = Unmanaged.passRetained(streamCtx).toOpaque()

            cRolePtrs.withUnsafeMutableBufferPointer { rolesPtr in
                cContentPtrs.withUnsafeMutableBufferPointer { contentsPtr in
                    dai_llm_generate_stream(
                        rolesPtr.baseAddress, contentsPtr.baseAddress, Int32(messages.count),
                        Int32(cfg.maxTokens), cfg.temperature, cfg.topP, Int32(cfg.topK), cfg.repeatPenalty,
                        // on_token callback
                        { tokenCStr, ctx in
                            guard let ctx, let tokenCStr else { return }
                            let streamCtx = Unmanaged<StreamCtx>.fromOpaque(ctx).takeUnretainedValue()
                            let token = String(cString: tokenCStr)
                            streamCtx.reply += token
                            streamCtx.tokenCount += 1
                            if streamCtx.ttftMs == nil {
                                streamCtx.ttftMs = Int64(Date().timeIntervalSince1970 * 1000) - streamCtx.startMs
                            }
                            streamCtx.continuation.yield(token)
                        },
                        // on_error callback
                        { msgCStr, ctx in
                            guard let ctx, let msgCStr else { return }
                            let streamCtx = Unmanaged<StreamCtx>.fromOpaque(ctx).takeUnretainedValue()
                            let msg = String(cString: msgCStr)
                            streamCtx.continuation.finish(throwing: DeviceAIError.inferenceFailed(reason: msg))
                        },
                        ctxPtr
                    )
                }
            }

            // Release the retained context
            let finalCtx = Unmanaged<StreamCtx>.fromOpaque(ctxPtr).takeRetainedValue()
            let latencyMs = currentTimeMs() - inferenceStartMs

            DeviceAI.shared.recordEvent(.inferenceComplete(
                module: "llm", modelId: modelId, latencyMs: latencyMs,
                ttftMs: finalCtx.ttftMs,
                tokensPerSec: latencyMs > 0 ? Float(finalCtx.tokenCount) * 1000.0 / Float(latencyMs) : nil,
                outputTokenCount: finalCtx.tokenCount, finishReason: finalCtx.reply.isEmpty ? "empty" : "stop"
            ))

            self.lock.lock()
            if !finalCtx.reply.isEmpty {
                self._history.append(LlmMessage(role: .assistant, content: finalCtx.reply))
            } else {
                self._history.removeLast()
            }
            self.lock.unlock()

            continuation.finish()
        }
    }

    public func sendBlocking(_ text: String) async throws -> String {
        var result = ""
        for try await token in try send(text) { result += token }
        return result
    }

    public func cancel() { dai_llm_cancel() }

    public func clearHistory() {
        lock.lock()
        _history.removeAll()
        lock.unlock()
    }

    public func close() {
        DeviceAI.shared.recordEvent(.modelUnload(module: "llm", modelId: modelId))
        dai_llm_shutdown()
        isReady = false
    }

    private func currentTimeMs() -> Int64 { Int64(Date().timeIntervalSince1970 * 1000) }
}
