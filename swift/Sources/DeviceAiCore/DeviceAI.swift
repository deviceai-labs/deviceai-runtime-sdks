/// Primary entry point for the DeviceAI SDK.
///
/// `DeviceAI` is a namespace — it cannot be instantiated.
/// Feature modules extend it with module-specific namespaces via Swift extensions:
///
/// ```swift
/// // DeviceAiLlm adds:   DeviceAI.llm
/// // DeviceAiStt adds:   DeviceAI.stt
/// // DeviceAiTts adds:   DeviceAI.tts
/// ```
///
/// ## Minimal setup (Development — no API key required)
///
/// ```swift
/// // AppDelegate / @main
/// DeviceAI.configure()
///
/// // LLM
/// let session = DeviceAI.llm.chat(modelPath: "/path/to/model.gguf") {
///     $0.systemPrompt = "You are a helpful assistant."
/// }
/// for try await token in session.send("Hello") { print(token, terminator: "") }
///
/// // STT
/// let stt = DeviceAI.stt.session(modelPath: "/path/to/ggml-tiny.bin")
/// let result = try await stt.transcribe(audioPath: "/path/to/audio.wav")
///
/// // TTS
/// let tts = DeviceAI.tts.session(modelPath: "/path/to/voice.onnx", tokensPath: "...")
/// let audio = try await tts.synthesize("Hello world")
/// ```
///
/// ## Kotlin parallel
///
/// ```kotlin
/// // Kotlin — object in :core, extensions in feature modules
/// object DeviceAI { ... }
/// val DeviceAI.llm: LlmModule get() = LlmModule
///
/// // Swift — enum namespace in DeviceAiCore, extensions in feature modules
/// public enum DeviceAI { ... }
/// extension DeviceAI { public static var llm: LLMModule { .shared } }
/// ```
public enum DeviceAI {

    // ── State ─────────────────────────────────────────────────────────────────

    private(set) static var environment: DeviceAiEnvironment = .development
    private(set) static var isConfigured = false

    // ── Initialisation ────────────────────────────────────────────────────────

    /// Configure the SDK at app startup.
    ///
    /// Call once — typically in `AppDelegate.application(_:didFinishLaunchingWithOptions:)`
    /// or the `@main` struct's `init()`. Calling more than once is a no-op after
    /// the first call.
    ///
    /// In `.development` mode no API key is required — all cloud calls are skipped
    /// and models are loaded from explicit local paths you manage.
    ///
    /// - Parameter environment: Target environment. Defaults to `.development`.
    public static func configure(environment: DeviceAiEnvironment = .development) {
        guard !isConfigured else {
            DeviceAiLogger.warning("DeviceAI", "configure() called more than once — ignoring.")
            return
        }
        Self.environment  = environment
        Self.isConfigured = true
        DeviceAiLogger.info("DeviceAI", "Configured — env=\(environment), core=\(SDKVersion.core)")
    }

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Throws `.notInitialised` if `configure()` has not been called.
    /// Feature module sessions call this before touching any native code.
    ///
    /// `package` access — visible to all targets in this SPM package,
    /// but not exported to app code or downstream packages.
    package static func assertConfigured() throws {
        guard isConfigured else { throw DeviceAiError.notInitialised }
    }

    /// Reset SDK state. **For unit tests only** — not part of the public API.
    package static func reset() {
        environment  = .development
        isConfigured = false
    }
}

// ── Environment ───────────────────────────────────────────────────────────────

/// Deployment environment for the SDK.
///
public enum DeviceAiEnvironment: Equatable, Sendable {
    /// Local development. No API key required. Cloud calls disabled.
    case development

    /// Pre-release QA. Points to staging backend.
    case staging(apiKey: String)

    /// Production. Points to production backend.
    case production(apiKey: String)

    var apiKey: String? {
        switch self {
        case .development:              return nil
        case .staging(let k):           return k
        case .production(let k):        return k
        }
    }
}
