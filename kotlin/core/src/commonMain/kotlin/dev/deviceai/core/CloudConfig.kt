package dev.deviceai.core

import dev.deviceai.core.telemetry.NetworkPolicy
import dev.deviceai.core.telemetry.TelemetryLevel
import kotlin.time.Duration
import kotlin.time.Duration.Companion.hours

/**
 * Cloud / control-plane configuration for the DeviceAI SDK.
 *
 * Passed to [DeviceAI.initialize] via a DSL block:
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     environment = Environment.Production
 *     telemetry   = TelemetryLevel.Minimal
 *     appVersion  = BuildConfig.VERSION_NAME
 *     capabilityProfile = mapOf("ram_gb" to 8.0, "cpu_cores" to 8, "has_npu" to true)
 *     networkPolicy = NetworkPolicy(
 *         isOnWifi    = { wifiManager.connectionInfo.networkId != -1 },
 *         isDataSaver = { connectivityManager.isActiveNetworkMetered },
 *     )
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
    val telemetry: TelemetryLevel,
    val networkPolicy: NetworkPolicy,
    val manifestSyncInterval: Duration,
    val capabilityProfile: Map<String, Any?>,
    val appVersion: String?,
    val appAttributes: Map<String, String>,
) {

    class Builder internal constructor(private val apiKey: String? = null) {

        /** Target environment. Defaults to [Environment.Production]. */
        var environment: Environment = Environment.Production

        /**
         * Override the backend base URL. Leave null to use the default for the selected
         * [environment]:
         * - Development → `http://localhost:8080`
         * - Staging     → `https://staging.api.deviceai.dev`
         * - Production  → `https://api.deviceai.dev`
         */
        var baseUrl: String? = null

        /**
         * Telemetry reporting level. Defaults to [TelemetryLevel.Off].
         * Enable only after obtaining explicit user consent (GDPR/CCPA).
         *
         * - [TelemetryLevel.Off]     — nothing buffered or sent (default)
         * - [TelemetryLevel.Minimal] — model load/unload + inference latency
         * - [TelemetryLevel.Full]    — all events including OTA and manifest sync
         */
        var telemetry: TelemetryLevel = TelemetryLevel.Off

        /**
         * Controls network-aware telemetry delivery. Defaults to [NetworkPolicy.Default]
         * (any network, no data-saver awareness). Provide [NetworkPolicy.isOnWifi] and/or
         * [NetworkPolicy.isDataSaver] lambdas for finer-grained control:
         *
         * ```kotlin
         * networkPolicy = NetworkPolicy(
         *     isOnWifi    = { /* your connectivity check */ true },
         *     isDataSaver = { /* your data saver check */ false },
         * )
         * ```
         *
         * See [NetworkPolicy] for per-priority delivery semantics.
         */
        var networkPolicy: NetworkPolicy = NetworkPolicy.Default

        /**
         * How often the SDK re-fetches the manifest in the background.
         * Shorter intervals mean faster propagation of kill switches and model updates.
         * Defaults to 6 hours.
         */
        var manifestSyncInterval: Duration = 6.hours

        /**
         * Device hardware capabilities for capability tier scoring and cohort targeting.
         *
         * Recognised scoring keys:
         * - `"ram_gb"`    — Float: total device RAM in GB (e.g. `8.0`)
         * - `"cpu_cores"` — Int:   logical CPU core count (e.g. `8`)
         * - `"has_npu"`   — Boolean: dedicated NPU / ANE present
         *
         * Additional keys are stored as-is for custom targeting.
         */
        var capabilityProfile: Map<String, Any?> = emptyMap()

        /**
         * App version string included in the capability profile.
         * Typically `BuildConfig.VERSION_NAME` on Android or
         * `Bundle.main.infoDictionary["CFBundleShortVersionString"]` on iOS.
         */
        var appVersion: String? = null

        /**
         * Arbitrary string attributes for cohort targeting
         * (e.g. `"user_tier" to "premium"`, `"locale" to "en-US"`).
         */
        var appAttributes: Map<String, String> = emptyMap()

        internal fun build(): CloudConfig {
            val resolvedUrl = baseUrl ?: when (environment) {
                Environment.Development -> "http://localhost:8080"
                Environment.Staging     -> "https://staging.api.deviceai.dev"
                Environment.Production  -> "https://api.deviceai.dev"
            }
            val fullProfile = buildMap<String, Any?> {
                putAll(capabilityProfile)
                appVersion?.let { put("app_version", it) }
                putAll(appAttributes)
            }
            return CloudConfig(
                environment          = environment,
                apiKey               = apiKey,
                baseUrl              = resolvedUrl,
                telemetry            = telemetry,
                networkPolicy        = networkPolicy,
                manifestSyncInterval = manifestSyncInterval,
                capabilityProfile    = fullProfile,
                appVersion           = appVersion,
                appAttributes        = appAttributes,
            )
        }
    }
}
