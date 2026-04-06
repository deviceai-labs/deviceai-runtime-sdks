package dev.deviceai.core

import dev.deviceai.core.backend.BackendClient
import dev.deviceai.core.backend.BackendClientImpl
import dev.deviceai.core.backend.DeviceSession
import dev.deviceai.core.backend.SessionStore
import dev.deviceai.core.backend.createBackendClient
import dev.deviceai.core.backend.createTelemetryEngine
import dev.deviceai.core.telemetry.TelemetryEngine
import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.core.telemetry.TelemetryLevel
import kotlinx.coroutines.*

/**
 * Primary entry point for the DeviceAI SDK.
 *
 * Call [initialize] **once** at app startup, then use [llm] and [speech] to run inference.
 *
 * ## Local mode (no API key / Development)
 * ```kotlin
 * // Android Application.onCreate()
 * DeviceAI.initialize(context)
 *
 * // Then anywhere in your app:
 * val session = DeviceAI.llm.chat("/path/to/model.gguf")
 * session.send("Hello").collect { token -> print(token) }
 * ```
 *
 * ## Managed mode (API key required)
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     environment = Environment.Production
 *     telemetry   = TelemetryLevel.Minimal
 *     wifiOnly    = true
 *     appVersion  = BuildConfig.VERSION_NAME
 *     capabilityProfile = mapOf("ram_gb" to 8.0, "cpu_cores" to 8, "has_npu" to true)
 * }
 *
 * // Model is resolved from the manifest — path provided automatically.
 * val session = DeviceAI.llm.chat()
 * ```
 *
 * ## Environments
 * | Environment | API key | Backend | Use for |
 * |---|---|---|---|
 * | [Environment.Development] | not required | none (local) | local dev + unit tests |
 * | [Environment.Staging]     | required | staging.api.deviceai.dev | pre-release QA |
 * | [Environment.Production]  | required | api.deviceai.dev | release builds |
 */
object DeviceAI {

    internal var cloudConfig: CloudConfig? = null
        private set

    // Implementation details — fully internal, not part of the public API.
    internal var backendClient: BackendClient? = null
        private set
    internal var telemetryEngine: TelemetryEngine? = null
        private set
    internal var deviceSession: DeviceSession? = null
        private set

    private val sdkScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    // Stable session ID for the lifetime of this process (not persisted).
    internal val processSessionId: String = generateSessionId()

    // ── Initialization ────────────────────────────────────────────────────────

    /**
     * Initialize the DeviceAI SDK.
     *
     * **Must be called exactly once** at app startup before using any module.
     *
     * ### Local mode (no API key)
     * When [apiKey] is `null` or [Environment.Development] is set, all cloud calls
     * are skipped. Models are loaded from explicit local paths passed to each module.
     *
     * ### Managed mode (API key present)
     * The SDK asynchronously:
     * 1. Registers or restores a device session (cached across launches)
     * 2. Fetches the model manifest for this device's cohort
     * 3. Starts periodic manifest refresh every [CloudConfig.manifestSyncInterval]
     * 4. Flushes buffered telemetry when threshold is reached
     *
     * @param context Android [android.content.Context]. Pass `null` on iOS/JVM.
     * @param apiKey  `dai_live_*` key from cloud.deviceai.dev.
     *   Not required in [Environment.Development].
     * @param block   Optional DSL block — see [CloudConfig.Builder].
     */
    fun initialize(
        context: Any? = null,
        apiKey: String? = null,
        block: CloudConfig.Builder.() -> Unit = {},
    ) {
        val config = CloudConfig.Builder(apiKey).apply(block).build()

        DeviceAIRuntime.configure(config.environment)
        cloudConfig = config

        CoreSDKLogger.info("DeviceAI", buildString {
            append("initialized — env=${config.environment}")
            if (config.environment != Environment.Development) {
                append(", baseUrl=${config.baseUrl}")
                append(", telemetry=${config.telemetry}")
            }
        })

        if (config.environment == Environment.Development || apiKey == null) {
            if (config.environment != Environment.Development && apiKey == null) {
                CoreSDKLogger.warn(
                    "DeviceAI",
                    "apiKey is required for Environment.${config.environment}. " +
                    "Running in local mode — cloud features disabled. " +
                    "Get your dai_live_* key from cloud.deviceai.dev."
                )
            } else {
                CoreSDKLogger.debug(
                    "DeviceAI",
                    "Local mode — cloud calls disabled. " +
                    "Provide model path explicitly: DeviceAI.llm.chat(modelPath)"
                )
            }
            return
        }

        // ── Managed mode ──────────────────────────────────────────────────────
        val client = createBackendClient(config.baseUrl, apiKey)
        backendClient = client

        // Wire up telemetry engine if enabled.
        if (config.telemetry != TelemetryLevel.Off) {
            telemetryEngine = createTelemetryEngine(
                level           = config.telemetry,
                policy          = config.networkPolicy,
                baseUrl         = config.baseUrl,
                sessionId       = processSessionId,
                customSink      = config.telemetrySink,
                clientForSink   = client as? BackendClientImpl,
                sessionProvider = { deviceSession },
            )
        }

        // Kick off async device session + manifest bootstrap.
        sdkScope.launch {
            bootstrapManagedMode(config, client)
        }
    }

    // ── Internal managed-mode bootstrap ──────────────────────────────────────

    private suspend fun bootstrapManagedMode(config: CloudConfig, client: BackendClient) {
        // 1. Restore or register device session.
        val session = resolveSession(config, client) ?: run {
            CoreSDKLogger.warn("DeviceAI", "device registration failed — cloud features unavailable")
            return
        }
        deviceSession = session
        telemetryEngine?.setSession(session.token, processSessionId)

        CoreSDKLogger.info("DeviceAI",
            "device registered — id=${session.deviceId}, tier=${session.capabilityTier}")

        // 2. Fetch initial manifest.
        fetchAndLogManifest(client, session)

        // 3. Schedule periodic manifest refresh.
        while (sdkScope.isActive) {
            delay(config.manifestSyncInterval)
            val current = deviceSession ?: break
            val active = refreshSessionIfNeeded(client, current)
            deviceSession = active
            fetchAndLogManifest(client, active)
        }
    }

    private suspend fun resolveSession(config: CloudConfig, client: BackendClient): DeviceSession? {
        // Try cached session first.
        val cached = SessionStore.load()
        if (cached != null && !cached.isExpired) {
            val active = refreshSessionIfNeeded(client, cached)
            SessionStore.save(active)
            return active
        }

        // Register fresh.
        return try {
            val session = client.registerDevice(config.capabilityProfile)
            SessionStore.save(session)
            session
        } catch (e: Exception) {
            CoreSDKLogger.warn("DeviceAI", "registerDevice failed: ${e.message}")
            null
        }
    }

    private suspend fun refreshSessionIfNeeded(
        client: BackendClient,
        session: DeviceSession,
    ): DeviceSession {
        if (!session.needsRefresh) return session
        val refreshed = client.refreshToken(session)
        if (refreshed != null) {
            SessionStore.save(refreshed)
            telemetryEngine?.setSession(refreshed.token, processSessionId)
            CoreSDKLogger.debug("DeviceAI", "device token refreshed")
            return refreshed
        }
        return session
    }

    private suspend fun fetchAndLogManifest(client: BackendClient, session: DeviceSession) {
        try {
            val manifest = client.fetchManifest(session.token)
            CoreSDKLogger.info("DeviceAI",
                "manifest synced — ${manifest.models.size} model(s) assigned, tier=${manifest.tier}")
            telemetryEngine?.record(TelemetryEvent.ManifestSync(
                timestampMs = dev.deviceai.models.currentTimeMillis(),
                success = true,
                modelCount = manifest.models.size,
            ))
        } catch (e: Exception) {
            CoreSDKLogger.warn("DeviceAI", "manifest fetch failed: ${e.message}")
            telemetryEngine?.record(TelemetryEvent.ManifestSync(
                timestampMs = dev.deviceai.models.currentTimeMillis(),
                success = false,
                errorCode = "network_error",
            ))
        }
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Flush buffered telemetry events, respecting the active [NetworkPolicy].
     *
     * Normal events flush on any network. Wi-Fi-preferred events are held if not on Wi-Fi.
     * Critical events are already sent immediately via [recordEvent] — this catches any
     * stragglers and is a good call-site for Wi-Fi connect events.
     *
     * No-op if telemetry is [TelemetryLevel.Off].
     */
    suspend fun flushTelemetry() {
        telemetryEngine?.flush()
    }

    /**
     * Shut down the SDK — cancels background jobs, best-effort flushes remaining telemetry
     * (all buffers, no network gating), and releases all resources.
     *
     * After calling this the SDK cannot be re-initialized in the same process.
     */
    fun shutdown() {
        // Close TelemetryEngine first — it does a final flush then cancels its own scope.
        telemetryEngine?.close()
        // Cancel the SDK-level scope (stops manifest refresh loop).
        sdkScope.cancel()
        backendClient?.close()
        telemetryEngine = null
        backendClient = null
        deviceSession = null
    }

    /**
     * Record a SDK telemetry event.
     *
     * **SDK modules only** — not intended for application code.
     * Call this from `kotlin/llm`, `kotlin/speech`, etc. to emit inference and lifecycle events.
     * No-op if telemetry is [TelemetryLevel.Off] or the SDK is in local mode.
     */
    @InternalDeviceAiApi
    fun recordEvent(event: TelemetryEvent) {
        telemetryEngine?.record(event)
    }

    // ── Observability ─────────────────────────────────────────────────────────

    // TODO: Phase 2 — val status: StateFlow<RuntimeStatus>
    // TODO: Phase 2 — val modelStatus: StateFlow<Map<Module, ModelStatus>>

    /** The environment this instance was initialized with, or `null` if not yet initialized. */
    val environment: Environment? get() = cloudConfig?.environment

    /** `true` when running in [Environment.Development] (no API key, local models only). */
    val isDevelopment: Boolean get() = environment == Environment.Development

    /** `true` when running in managed mode (API key present, backend connected). */
    val isManaged: Boolean get() = deviceSession != null

    /** Capability tier assigned by the backend: `"low"`, `"mid"`, `"high"`, `"flagship"`. */
    val capabilityTier: String? get() = deviceSession?.capabilityTier

    // ── Module namespaces ─────────────────────────────────────────────────────
    //
    // LLM:    DeviceAI.llm    — added by kotlin/llm via extension property
    // Speech: DeviceAI.speech — added by kotlin/speech via extension property
}

// Platform-provided session ID generator (UUID v4 string).
internal expect fun generateSessionId(): String
