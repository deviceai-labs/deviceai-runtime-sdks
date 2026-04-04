package dev.deviceai.core.telemetry

import dev.deviceai.core.CoreSDKLogger
import kotlinx.coroutines.*
import kotlin.math.min

/**
 * In-memory ring buffer for SDK telemetry events with network-aware delivery.
 *
 * ## Delivery behaviour by priority
 * - [TelemetryPriority.Normal] — batched up to [normalFlushThreshold] events, sent on any
 *   network. Failed sends are retried with exponential backoff.
 * - [TelemetryPriority.WifiPreferred] — deferred until [NetworkPolicy.isOnWifi] returns `true`.
 *   Falls back to Normal behaviour when no Wi-Fi checker is provided.
 * - [TelemetryPriority.Critical] — sent immediately, bypasses batching, data-saver, and
 *   Wi-Fi gates. Always sent even on cellular.
 *
 * ## Data Saver
 * When [NetworkPolicy.isDataSaver] returns `true`, the auto-flush threshold is multiplied by
 * [NetworkPolicy.dataSaverThresholdMultiplier] — fewer, larger batches on constrained networks.
 *
 * ## Buffer
 * Capacity is [BUFFER_CAPACITY]. When full, the oldest *Normal* event is dropped.
 * Critical events are never dropped.
 */
internal class TelemetryEngine(
    private val level: TelemetryLevel,
    private val policy: NetworkPolicy,
    private val sink: TelemetrySink,
) {
    private val normalBuffer   = ArrayDeque<TelemetryEvent>(BUFFER_CAPACITY)
    private val wifiBuffer     = ArrayDeque<TelemetryEvent>(BUFFER_CAPACITY)
    private val criticalBuffer = ArrayDeque<TelemetryEvent>(32)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Record an event. Routes to the correct buffer based on [TelemetryEvent.priority]. */
    fun record(event: TelemetryEvent) {
        if (!shouldRecord(event)) return

        when (effectivePriority(event)) {
            TelemetryPriority.Critical -> {
                synchronized(criticalBuffer) { criticalBuffer.addLast(event) }
                scope.launch { flushCritical() }
            }
            TelemetryPriority.WifiPreferred -> {
                val added = synchronized(wifiBuffer) {
                    if (wifiBuffer.size >= BUFFER_CAPACITY) wifiBuffer.removeFirst()
                    wifiBuffer.addLast(event)
                    wifiBuffer.size
                }
                // Flush wifi-preferred events if we're currently on Wi-Fi.
                if (added >= WIFI_FLUSH_THRESHOLD && policy.isOnWifi?.invoke() == true) {
                    scope.launch { flushWifiPreferred() }
                }
            }
            TelemetryPriority.Normal -> {
                val threshold = normalFlushThreshold()
                val added = synchronized(normalBuffer) {
                    if (normalBuffer.size >= BUFFER_CAPACITY) normalBuffer.removeFirst()
                    normalBuffer.addLast(event)
                    normalBuffer.size
                }
                if (added >= threshold) {
                    scope.launch { flushNormal() }
                }
            }
        }
    }

    /**
     * Flush all buffers that are ready for delivery given current network state.
     *
     * Call this on Wi-Fi connect or when the app moves to the background.
     */
    suspend fun flush() {
        flushCritical()
        if (policy.isOnWifi?.invoke() != false) {
            flushWifiPreferred()
        }
        flushNormal()
    }

    /**
     * Send [TelemetryPriority.Critical] events immediately — no network gating.
     */
    private suspend fun flushCritical() {
        val batch = synchronized(criticalBuffer) {
            if (criticalBuffer.isEmpty()) return
            criticalBuffer.toList().also { criticalBuffer.clear() }
        }
        sendWithBackoff(batch, tag = "critical")
    }

    /**
     * Send [TelemetryPriority.WifiPreferred] events when on Wi-Fi.
     */
    private suspend fun flushWifiPreferred() {
        if (policy.isOnWifi != null && policy.isOnWifi.invoke() == false) {
            CoreSDKLogger.debug("TelemetryEngine", "wifi-preferred flush skipped — not on Wi-Fi")
            return
        }
        val batch = synchronized(wifiBuffer) {
            if (wifiBuffer.isEmpty()) return
            wifiBuffer.toList().also { wifiBuffer.clear() }
        }
        sendWithBackoff(batch, tag = "wifi-preferred")
    }

    /**
     * Send [TelemetryPriority.Normal] events. Any network, respects data-saver threshold.
     */
    private suspend fun flushNormal() {
        val batch = synchronized(normalBuffer) {
            if (normalBuffer.isEmpty()) return
            normalBuffer.toList().also { normalBuffer.clear() }
        }
        sendWithBackoff(batch, tag = "normal")
    }

    /**
     * Best-effort final flush. Should be called at SDK shutdown or app termination.
     */
    fun close() {
        scope.launch {
            flush()
        }.invokeOnCompletion {
            scope.cancel()
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private fun normalFlushThreshold(): Int {
        val dataS = policy.isDataSaver?.invoke() == true
        return if (dataS) FLUSH_THRESHOLD * policy.dataSaverThresholdMultiplier
        else FLUSH_THRESHOLD
    }

    /**
     * Resolve the effective priority of an event. When no [NetworkPolicy.isOnWifi] checker
     * is provided, [TelemetryPriority.WifiPreferred] degrades to [TelemetryPriority.Normal].
     */
    private fun effectivePriority(event: TelemetryEvent): TelemetryPriority {
        return if (event.priority == TelemetryPriority.WifiPreferred && policy.isOnWifi == null)
            TelemetryPriority.Normal
        else event.priority
    }

    private fun shouldRecord(event: TelemetryEvent): Boolean = when (level) {
        TelemetryLevel.Off -> false
        TelemetryLevel.Minimal -> event is TelemetryEvent.ModelLoad
                || event is TelemetryEvent.ModelUnload
                || event is TelemetryEvent.InferenceComplete
                || event is TelemetryEvent.ControlPlaneAlert
        TelemetryLevel.Full -> true
    }

    private suspend fun sendWithBackoff(batch: List<TelemetryEvent>, tag: String) {
        var attempt = 0
        while (attempt <= MAX_RETRIES) {
            try {
                sink.ingest(batch)
                CoreSDKLogger.debug("TelemetryEngine", "[$tag] flushed ${batch.size} events")
                return
            } catch (e: Exception) {
                attempt++
                if (attempt > MAX_RETRIES) {
                    CoreSDKLogger.warn("TelemetryEngine",
                        "[$tag] giving up after $MAX_RETRIES retries: ${e.message}")
                    // Re-queue to front of normal buffer if there's room (best-effort)
                    if (tag != "critical") {
                        synchronized(normalBuffer) {
                            val space = BUFFER_CAPACITY - normalBuffer.size
                            batch.takeLast(space).reversed().forEach { normalBuffer.addFirst(it) }
                        }
                    }
                    return
                }
                val delayMs = min(BACKOFF_BASE_MS * (1L shl (attempt - 1)), BACKOFF_MAX_MS)
                CoreSDKLogger.debug("TelemetryEngine",
                    "[$tag] attempt $attempt failed, retrying in ${delayMs}ms: ${e.message}")
                delay(delayMs)
            }
        }
    }

    companion object {
        const val BUFFER_CAPACITY         = 256
        const val FLUSH_THRESHOLD         = 100
        const val WIFI_FLUSH_THRESHOLD    = 50
        const val MAX_RETRIES             = 3
        const val BACKOFF_BASE_MS         = 1_000L  // 1s, 2s, 4s
        const val BACKOFF_MAX_MS          = 30_000L
    }
}
