import Foundation
import DeviceAiCore
import CLlama

/// LLM inference engine backed by llama.cpp via CLlama.xcframework.
///
/// Internal — never exposed publicly. `ChatSession` is the developer-facing API.
final class LlamaEngine: @unchecked Sendable {

    private let queue = DispatchQueue(label: "dev.deviceai.llm.inference", qos: .userInitiated)
    private var isCancelled = false
    private var isClosed    = false

    private var model: OpaquePointer?
    private var ctx:   OpaquePointer?

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    func load(modelPath: String, config: ChatConfig) async throws {
        guard !isClosed else { throw DeviceAiError.sessionClosed }

        guard FileManager.default.fileExists(atPath: modelPath) else {
            throw DeviceAiError.modelNotFound(path: modelPath)
        }

        return try await withCheckedThrowingContinuation { continuation in
            queue.async {
                var mparams = llama_model_default_params()
                mparams.n_gpu_layers = -1  // all layers on Metal (fastest on Apple Silicon)

                guard let m = llama_model_load_from_file(modelPath, mparams) else {
                    continuation.resume(throwing: DeviceAiError.modelLoadFailed(reason: "llama_model_load_from_file returned nil"))
                    return
                }
                self.model = m

                var cparams = llama_context_default_params()
                // n_ctx = 0 → llama.cpp reads n_ctx_train from GGUF metadata.
                // This gives each model its own correct context window automatically.
                cparams.n_ctx     = config.contextSize > 0 ? UInt32(config.contextSize) : 0
                cparams.n_threads = Int32(config.threads)

                guard let c = llama_new_context_with_model(m, cparams) else {
                    llama_model_free(m)
                    continuation.resume(throwing: DeviceAiError.modelLoadFailed(reason: "llama_new_context_with_model returned nil"))
                    return
                }
                self.ctx = c
                DeviceAiLogger.info("LlamaEngine", "Model loaded: \(modelPath)")
                continuation.resume()
            }
        }
    }

    func close() async {
        guard !isClosed else { return }
        isClosed = true
        queue.async {
            if let c = self.ctx   { llama_free(c);        self.ctx   = nil }
            if let m = self.model { llama_model_free(m);  self.model = nil }
        }
        DeviceAiLogger.info("LlamaEngine", "Closed.")
    }

    // ── Generating ────────────────────────────────────────────────────────────

    func generate(messages: [LlmMessage], config: LlmGenConfig) async throws -> LlmResult {
        let start = Date()
        var full = ""
        var count = 0
        for try await token in generateStream(messages: messages, config: config) {
            full += token; count += 1
        }
        return LlmResult(
            text: full, tokenCount: count, promptTokenCount: nil,
            finishReason: isCancelled ? .cancelled : .stop,
            generationTimeMs: Int64(Date().timeIntervalSince(start) * 1000)
        )
    }

    func generateStream(messages: [LlmMessage], config: LlmGenConfig) -> AsyncThrowingStream<String, Error> {
        let augmented: [LlmMessage] = config.ragStore.map { store in
            RagAugmentor.augment(messages: messages,
                                 with: store.retrieve(query: messages.last?.content ?? "", topK: config.ragTopK))
        } ?? messages

        return AsyncThrowingStream { continuation in
            self.isCancelled = false
            self.queue.async {
                guard !self.isClosed  else { continuation.finish(throwing: DeviceAiError.sessionClosed); return }
                guard let ctx = self.ctx, let model = self.model else {
                    continuation.finish(throwing: DeviceAiError.notInitialised); return
                }

                // Clear KV cache so each call starts from a clean position.
                // Required because we re-encode the full prompt on every send().
                llama_memory_clear(llama_get_memory(ctx), false)

                let prompt = self.buildPrompt(from: augmented, model: model)

                // Get vocab (needed for tokenize, token_to_piece, eos)
                guard let vocab = llama_model_get_vocab(model) else {
                    continuation.finish(throwing: DeviceAiError.inferenceFailed(reason: "llama_model_get_vocab returned nil"))
                    return
                }

                // Tokenise — use the context window size as the buffer, same as
                // the Kotlin SDK (llm_jni.cpp: `int n_prompt_max = llama_n_ctx(g_ctx)`).
                // This is always large enough for any prompt that fits in the context.
                let ctxSize = Int32(llama_n_ctx(ctx))
                let textLen = Int32(prompt.utf8.count)
                var tokens = [llama_token](repeating: 0, count: Int(ctxSize))
                let nTokens = llama_tokenize(vocab, prompt, textLen,
                                             &tokens, ctxSize, true, true)
                guard nTokens > 0 else {
                    continuation.finish(throwing: DeviceAiError.inferenceFailed(
                        reason: nTokens < 0
                            ? "prompt too long for context window (\(-nTokens) tokens needed, \(ctxSize) available)"
                            : "tokenise returned 0"))
                    return
                }
                tokens = Array(tokens.prefix(Int(nTokens)))

                // Build sampler chain: temperature → repetition penalty → distribution sample
                guard let smpl = llama_sampler_chain_init(llama_sampler_chain_default_params()) else {
                    continuation.finish(throwing: DeviceAiError.inferenceFailed(reason: "llama_sampler_chain_init returned nil"))
                    return
                }
                let temp = Float(config.temperature)
                if temp > 0 {
                    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temp))
                    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
                        64,                          // last-n window
                        Float(config.repeatPenalty), // repeat penalty
                        0.0,                         // freq penalty
                        0.0                          // presence penalty
                    ))
                    llama_sampler_chain_add(smpl, llama_sampler_init_dist(UInt32.random(in: 0 ... UInt32.max)))
                } else {
                    // temperature == 0 → greedy (deterministic)
                    llama_sampler_chain_add(smpl, llama_sampler_init_greedy())
                }

                // Decode prompt using llama_batch_get_one
                let tokenCount = Int32(tokens.count)
                let promptDecodeOk = tokens.withUnsafeMutableBufferPointer { buf -> Bool in
                    let batch = llama_batch_get_one(buf.baseAddress, tokenCount)
                    return llama_decode(ctx, batch) == 0
                }
                guard promptDecodeOk else {
                    llama_sampler_free(smpl)
                    continuation.finish(throwing: DeviceAiError.inferenceFailed(reason: "llama_decode (prompt) failed"))
                    return
                }

                // Get EOS token
                let eosToken = llama_vocab_eos(vocab)

                // Sample loop
                let nMax = Int32(config.maxTokens)
                var nGenerated: Int32 = 0

                while nGenerated < nMax && !self.isCancelled {
                    let newToken = llama_sampler_sample(smpl, ctx, -1)
                    if newToken == eosToken { break }

                    llama_sampler_accept(smpl, newToken)

                    var tokenBuf = [CChar](repeating: 0, count: 32)
                    let len = llama_token_to_piece(vocab, newToken, &tokenBuf, 32, 0, true)
                    if len > 0,
                       let str = String(bytes: tokenBuf.prefix(Int(len)).map { UInt8(bitPattern: $0) }, encoding: .utf8) {
                        continuation.yield(str)
                    }

                    // Decode next token
                    var tokenVal = newToken
                    let decodeOk = withUnsafeMutablePointer(to: &tokenVal) { ptr -> Bool in
                        let batch = llama_batch_get_one(ptr, 1)
                        return llama_decode(ctx, batch) == 0
                    }
                    if !decodeOk { break }
                    nGenerated += 1
                }

                llama_sampler_free(smpl)
                continuation.finish()
            }
        }
    }

    func cancel() { isCancelled = true }

    // ── Private ───────────────────────────────────────────────────────────────

    private func buildPrompt(from messages: [LlmMessage], model: OpaquePointer) -> String {
        // Map to llama_chat_message C structs
        let cMessages: [llama_chat_message] = messages.map { m in
            let role: String
            switch m.role {
            case .system:    role = "system"
            case .user:      role = "user"
            case .assistant: role = "assistant"
            }
            // These pointers are valid as long as the Swift strings are alive below
            return llama_chat_message(role: (role as NSString).utf8String,
                                      content: (m.content as NSString).utf8String)
        }

        // Get the model's chat template (reads tokenizer.chat_template from GGUF metadata)
        let tmpl = llama_model_chat_template(model, nil)

        // First call: measure required buffer size
        let needed = cMessages.withUnsafeBufferPointer { buf in
            llama_chat_apply_template(tmpl, buf.baseAddress, buf.count, true, nil, 0)
        }
        guard needed > 0 else { return "" }

        // Second call: fill buffer
        var buf = [CChar](repeating: 0, count: Int(needed) + 1)
        cMessages.withUnsafeBufferPointer { msgBuf in
            _ = llama_chat_apply_template(tmpl, msgBuf.baseAddress, msgBuf.count, true, &buf, needed)
        }
        return String(cString: buf)
    }
}
