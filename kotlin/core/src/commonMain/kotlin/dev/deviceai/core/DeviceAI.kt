package dev.deviceai.core

/**
 * Primary entry point for the DeviceAI SDK.
 *
 * Call [initialize] **once** at app startup, then use [llm] and [speech] to run inference.
 *
 * ## Minimal (Development — no backend required)
 * ```kotlin
 * // Android Application.onCreate()
 * DeviceAI.initialize(context)
 *
 * // Then anywhere in your app:
 * val session = DeviceAI.llm.chat("/path/to/model.gguf")
 * session.send("Hello").collect { token -> print(token) }
 * ```
 *
 * ## With cloud (Staging / Production)
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     environment = Environment.Production
 *     telemetry   = TelemetryMode.MANAGED_BASIC   // upload inference metrics; requires API key
 *     wifiOnly    = true
 *     appVersion  = BuildConfig.VERSION_NAME
 *     appAttributes = mapOf("user_tier" to "premium")
 * }
 *
 * // Model path comes from the manifest — no explicit path needed.
 * val session = DeviceAI.llm.chat()
 * ```
 *
 * ## Environments
 * | Environment | API key | Backend | Use for |
 * |---|---|---|---|
 * | [Environment.Development] | not required | none (local) | local dev + unit tests |
 * | [Environment.Staging] | required | staging.api.deviceai.dev | pre-release QA |
 * | [Environment.Production] | required | api.deviceai.dev | release builds |
 */
object DeviceAI {

    internal var cloudConfig: CloudConfig? = null
        private set

    // ── Initialization ────────────────────────────────────────────────────────

    /**
     * Initialize the DeviceAI SDK.
     *
     * **Must be called exactly once** at app startup before using any module.
     * In [Environment.Development] no [apiKey] is required — all cloud calls are
     * skipped and models are loaded from explicit local paths.
     *
     * @param context Android [android.content.Context]. Pass `null` on iOS/JVM.
     * @param apiKey  Your `dai_live_*` key from cloud.deviceai.dev.
     *   Not required in [Environment.Development].
     * @param block   Optional DSL block to configure cloud behaviour, telemetry,
     *   and app metadata. See [CloudConfig.Builder].
     */
    fun initialize(
        context: Any? = null,
        apiKey: String? = null,
        block: CloudConfig.Builder.() -> Unit = {},
    ) {
        val builder = CloudConfig.Builder(apiKey).apply(block)
        val config = builder.build()

        // Configure runtime first — throws IllegalStateException on double-init.
        // Only update cloudConfig after configure() succeeds so state stays consistent.
        DeviceAIRuntime.configure(config.environment)
        cloudConfig = config

        CoreSDKLogger.info("DeviceAI", buildString {
            append("Initialized — env=${config.environment}")
            if (config.environment != Environment.Development) {
                append(", baseUrl=${config.baseUrl}")
                append(", telemetry=${config.telemetry.name}")
            }
        })

        if (config.environment == Environment.Development) {
            CoreSDKLogger.debug("DeviceAI",
                "Development mode — cloud calls disabled. " +
                "Provide model path explicitly: DeviceAI.llm.chat(modelPath)"
            )
            return
        }

        if (apiKey == null) {
            CoreSDKLogger.warn("DeviceAI",
                "apiKey is required for Environment.${config.environment}. " +
                "Cloud features are disabled. Get your dai_live_* key from cloud.deviceai.dev."
            )
            return
        }

        // TODO: Phase 2 — start async device registration + manifest sync
        // DeviceRegistration.registerAsync(context, config)
        CoreSDKLogger.debug("DeviceAI",
            "Cloud integration pending Phase 2 — backend not yet connected."
        )
    }

    // ── Module namespaces ─────────────────────────────────────────────────────
    //
    // LLM:    DeviceAI.llm    — added by kotlin/llm via extension property
    // Speech: DeviceAI.speech — added by kotlin/speech via extension property (Phase 2)

    // ── Observability ─────────────────────────────────────────────────────────

    // TODO: Phase 2 — val status: StateFlow<RuntimeStatus>
    // TODO: Phase 2 — val capabilityTier: CapabilityTier?
    // TODO: Phase 2 — val modelStatus: StateFlow<Map<Module, ModelStatus>>

    /** The environment this instance was initialized with, or null if not yet initialized. */
    val environment: Environment? get() = cloudConfig?.environment

    /** `true` when running in [Environment.Development]. */
    val isDevelopment: Boolean get() = environment == Environment.Development
}

