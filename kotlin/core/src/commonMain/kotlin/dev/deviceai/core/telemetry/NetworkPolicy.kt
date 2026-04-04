package dev.deviceai.core.telemetry

/**
 * Controls how the SDK handles telemetry delivery under different network conditions.
 *
 * Pass to [dev.deviceai.core.CloudConfig.Builder.networkPolicy].
 *
 * ## Defaults
 * The [Default] factory covers most production apps:
 * - Normal events are batched and sent on any network with retry backoff.
 * - Critical control-plane events (kill switch, rollback) are sent immediately.
 * - Heavy payloads (future: crash dumps, log bundles) wait for Wi-Fi.
 * - Rate is automatically reduced when the user has Data Saver enabled.
 *
 * ## Custom example
 * ```kotlin
 * DeviceAI.initialize(context, apiKey = "dai_live_...") {
 *     networkPolicy = NetworkPolicy(
 *         isOnWifi     = { wifiManager.isWifiEnabled },
 *         isDataSaver  = { connectivityManager.isActiveNetworkMetered },
 *     )
 * }
 * ```
 */
data class NetworkPolicy(
    /**
     * Returns `true` when the device is on a Wi-Fi connection.
     * Used to defer [TelemetryPriority.WifiPreferred] events (e.g. future crash dumps).
     * When `null`, Wi-Fi-preferred events fall back to [TelemetryPriority.Normal] behaviour.
     */
    val isOnWifi: (() -> Boolean)? = null,

    /**
     * Returns `true` when the user has enabled Data Saver / Low Data Mode.
     * When active, the auto-flush threshold is multiplied by [dataSaverThresholdMultiplier]
     * so the SDK sends fewer, larger batches.
     * When `null`, data saver state is not checked.
     */
    val isDataSaver: (() -> Boolean)? = null,

    /**
     * Multiplier applied to the normal flush threshold when [isDataSaver] returns `true`.
     * Default `5` means the SDK batches 5× more events before flushing on constrained networks.
     */
    val dataSaverThresholdMultiplier: Int = 5,
) {
    companion object {
        /** Sensible defaults with no app-provided connectivity callbacks. */
        val Default = NetworkPolicy()
    }
}

/**
 * Delivery priority for a telemetry event. Controls batching and network gating.
 */
enum class TelemetryPriority {
    /**
     * Batched and sent on any network type. Small payloads.
     * Failed sends are retried with exponential backoff.
     * Rate is reduced automatically when Data Saver is enabled.
     *
     * Examples: model load/unload, inference latency, manifest sync.
     */
    Normal,

    /**
     * Deferred until a Wi-Fi connection is available (requires [NetworkPolicy.isOnWifi]).
     * Falls back to [Normal] behaviour when no Wi-Fi checker is provided.
     *
     * Reserved for future heavy payloads: crash dumps, diagnostic log bundles.
     */
    WifiPreferred,

    /**
     * Sent immediately without batching, on any network type, even when Data Saver is on.
     * Used for control-plane events that must reach the backend without delay.
     *
     * Examples: kill-switch detection, forced model rollback, security violations.
     */
    Critical,
}
