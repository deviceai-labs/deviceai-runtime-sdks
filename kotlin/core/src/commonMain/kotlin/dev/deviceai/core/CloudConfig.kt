package dev.deviceai.core

import kotlin.time.Duration
import kotlin.time.Duration.Companion.hours

/**
 * Cloud / control-plane configuration for the DeviceAI SDK.
 *
 * Passed to [DeviceAI.initialize] via a DSL block:
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     environment = Environment.Staging
 *     telemetry   = Telemetry.Enabled
 *     wifiOnly    = true
 *     appVersion  = BuildConfig.VERSION_NAME
 *     appAttributes = mapOf("user_tier" to "premium")
 * }
 * ```
 *
 * In [Environment.Development] no API key is required and all cloud calls
 * are skipped — the SDK runs fully offline against a local model path.
 */
class CloudConfig private constructor(
    val environment: Environment,
    val apiKey: String?,
    val baseUrl: String,
    val telemetry: TelemetryMode,
    val telemetrySamplingRate: Float,
    val telemetryMaxPerMinute: Int,
    val wifiOnly: Boolean,
    val manifestSyncInterval: Duration,
    val appVersion: String?,
    val appAttributes: Map<String, String>,
) {

    class Builder internal constructor(private val apiKey: String? = null) {

        /** Target environment. Defaults to [Environment.Production]. */
        var environment: Environment = Environment.Production

        /**
         * Override the backend base URL. Leave null to use the default for the
         * selected [environment]:
         * - Development → `http://localhost:8080`
         * - Staging     → `https://staging.api.deviceai.dev`
         * - Production  → `https://api.deviceai.dev`
         */
        var baseUrl: String? = null

        /**
         * Telemetry mode. Defaults to [TelemetryMode.OFF].
         * Set to [TelemetryMode.MANAGED_BASIC] or higher only after obtaining user consent (GDPR).
         */
        var telemetry: TelemetryMode = TelemetryMode.OFF

        /**
         * Fraction of inference calls to record (0.0 = none, 1.0 = all).
         * Reduces ingestion load for high-frequency apps. Default: 1.0.
         */
        var telemetrySamplingRate: Float = 1.0f

        /**
         * Maximum telemetry events buffered per minute.
         * Events beyond this cap are silently dropped.
         * Default: 60.
         */
        var telemetryMaxPerMinute: Int = 60

        /**
         * When `true` the SDK defers model downloads until a Wi-Fi connection
         * is available. Defaults to `true`.
         */
        var wifiOnly: Boolean = true

        /**
         * How often the SDK re-fetches the manifest in the background.
         * Shorter intervals mean faster propagation of kill switches and
         * model updates at the cost of more API calls.
         * Defaults to 6 hours.
         */
        var manifestSyncInterval: Duration = 6.hours

        /**
         * The app version string to include in the device capability profile
         * sent to the backend. Typically `BuildConfig.VERSION_NAME` on Android
         * or `Bundle.main.infoDictionary["CFBundleShortVersionString"]` on iOS.
         */
        var appVersion: String? = null

        /**
         * Arbitrary key-value attributes sent in the device capability profile.
         * Use this to pass app-level context that the backend can use for cohort
         * targeting (e.g. `"user_tier" to "premium"`, `"locale" to "en-US"`).
         */
        var appAttributes: Map<String, String> = emptyMap()

        internal fun build(): CloudConfig {
            val resolvedUrl = baseUrl ?: when (environment) {
                Environment.Development -> "http://localhost:8080"
                Environment.Staging     -> "https://staging.api.deviceai.dev"
                Environment.Production  -> "https://api.deviceai.dev"
            }
            return CloudConfig(
                environment              = environment,
                apiKey                   = apiKey,
                baseUrl                  = resolvedUrl,
                telemetry                = telemetry,
                telemetrySamplingRate    = telemetrySamplingRate.coerceIn(0f, 1f),
                telemetryMaxPerMinute    = telemetryMaxPerMinute.coerceAtLeast(1),
                wifiOnly                 = wifiOnly,
                manifestSyncInterval     = manifestSyncInterval,
                appVersion               = appVersion,
                appAttributes            = appAttributes,
            )
        }
    }
}

/**
 * Controls what the SDK collects and where it sends it.
 *
 * Disabled by default — enable only after obtaining explicit user consent (GDPR/CCPA).
 *
 * What is **never** collected regardless of mode:
 * - Prompt or response text
 * - Audio content or transcripts
 * - Any personally identifiable information
 * - Raw exception messages (error codes only)
 */
enum class TelemetryMode {
    /** No data collected or buffered. Default. */
    OFF,

    /**
     * Data collected and logged via [dev.deviceai.core.CoreSDKLogger] only.
     * Never uploaded. Useful for integration debugging.
     */
    LOCAL,

    /**
     * Inference metrics (latency, token counts, model ID) uploaded to the DeviceAI backend.
     * Device profile (OS, RAM) sent once per flush. No error detail.
     */
    MANAGED_BASIC,

    /**
     * Same as [MANAGED_BASIC] plus error codes and device profile on every flush.
     * Requires explicit user consent — recommended only for production opt-in flows.
     */
    MANAGED_FULL,
}
