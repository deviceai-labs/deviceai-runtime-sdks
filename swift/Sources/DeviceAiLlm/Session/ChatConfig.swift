/// Configuration for a `ChatSession`.
///
/// Applied via a closure on session creation — same DSL pattern as Kotlin:
/// ```swift
/// // Swift
/// DeviceAI.llm.chat(modelPath: path) {
///     $0.systemPrompt = "You are helpful."
///     $0.temperature  = 0.8
/// }
///
/// // Kotlin equivalent
/// DeviceAI.llm.chat(path) {
///     systemPrompt = "You are helpful."
///     temperature  = 0.8f
/// }
/// ```
///
/// All fields have sensible defaults — zero-config session works out of the box.
/// Mirrors `ChatConfig` in kotlin/llm exactly.
public final class ChatConfig: @unchecked Sendable {

    // ── Conversation ──────────────────────────────────────────────────────────

    /// System prompt applied to every request. Shapes the model's persona.
    public var systemPrompt: String = "You are a helpful assistant."

    // ── Generation ────────────────────────────────────────────────────────────

    /// Maximum tokens to generate per response.
    public var maxTokens: Int    = 512

    /// Sampling temperature. 0 = deterministic, 1 = creative. Range: 0–2.
    public var temperature: Float = 0.7

    /// Nucleus sampling. Range: 0–1.
    public var topP: Float        = 0.9

    /// Top-k sampling. 0 = disabled.
    public var topK: Int          = 40

    /// Repetition penalty. Values > 1 discourage repeating phrases.
    public var repeatPenalty: Float = 1.1

    // ── Engine (init-time) ────────────────────────────────────────────────────

    /// KV-cache context window in tokens.
    /// 0 = auto (reads n_ctx_train from the model's GGUF metadata — recommended).
    /// Set explicitly only to cap RAM usage below the model's native context.
    public var contextSize: Int = 0

    /// CPU threads for inference.
    public var threads: Int     = 4

    /// Use GPU (Metal). Default: true — Metal is 3–5× faster on Apple Silicon.
    public var useGpu: Bool     = true

    public init() {}
}
