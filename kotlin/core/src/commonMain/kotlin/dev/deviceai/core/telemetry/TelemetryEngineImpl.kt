package dev.deviceai.core.telemetry

import dev.deviceai.core.CoreSDKLogger
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlin.math.min

/**
 * Coroutine-based [TelemetryEngine] backed by a Channel actor.
 *
 * All buffer mutations happen on a single coroutine consumer — no `synchronized`
 * blocks, safe on both JVM and Kotlin/Native (iOS).
 *
 * ## Delivery behaviour by priority
 * - [TelemetryPriority.Normal] — batched up to [normalFlushThreshold] events, sent on any
 *   network. Failed sends are retried with exponential backoff (1s / 2s / 4s).
 * - [TelemetryPriority.WifiPreferred] — deferred until [NetworkPolicy.isOnWifi] is true.
 *   Degrades to Normal when no Wi-Fi checker is provided.
 * - [TelemetryPriority.Critical] — sent immediately, bypasses batching and data-saver.
 */
internal class TelemetryEngineImpl(
    private val level: TelemetryLevel,
    private val policy: NetworkPolicy,
    private val sink: TelemetrySink,
) : TelemetryEngine {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    // All buffer mutations happen inside the single actor coroutine — no locking needed.
    private val normalBuffer   = ArrayDeque<TelemetryEvent>(BUFFER_CAPACITY)
    private val wifiBuffer     = ArrayDeque<TelemetryEvent>(BUFFER_CAPACITY)
    private val criticalBuffer = ArrayDeque<TelemetryEvent>(32)

    // Unbounded channel — record() never blocks; actor drains at its own pace.
    private val recordChannel = Channel<TelemetryEvent>(Channel.UNLIMITED)

    init {
        scope.launch {
            for (event in recordChannel) {
                handleRecordedEvent(event)
            }
        }
    }

    override fun record(event: TelemetryEvent) {
        if (!shouldRecord(event)) return
        recordChannel.trySend(event) // always succeeds — channel is unbounded
    }

    override suspend fun flush() = withContext(scope.coroutineContext) {
        flushCritical()
        if (policy.isOnWifi?.invoke() != false) flushWifiPreferred()
        flushNormal()
    }

    override fun close() {
        recordChannel.close()
        scope.launch {
            // Best-effort final flush before cancellation.
            flushCritical()
            flushWifiPreferred()
            flushNormal()
        }.invokeOnCompletion { scope.cancel() }
    }

    // ── Actor — runs inside scope, single-threaded buffer access ─────────────

    private suspend fun handleRecordedEvent(event: TelemetryEvent) {
        when (effectivePriority(event)) {
            TelemetryPriority.Critical -> {
                if (criticalBuffer.size >= CRITICAL_BUFFER_CAPACITY) criticalBuffer.removeFirst()
                criticalBuffer.addLast(event)
                flushCritical()
            }
            TelemetryPriority.WifiPreferred -> {
                if (wifiBuffer.size >= BUFFER_CAPACITY) wifiBuffer.removeFirst()
                wifiBuffer.addLast(event)
                if (wifiBuffer.size >= WIFI_FLUSH_THRESHOLD && policy.isOnWifi?.invoke() == true) {
                    flushWifiPreferred()
                }
            }
            TelemetryPriority.Normal -> {
                if (normalBuffer.size >= BUFFER_CAPACITY) normalBuffer.removeFirst()
                normalBuffer.addLast(event)
                if (normalBuffer.size >= normalFlushThreshold()) flushNormal()
            }
        }
    }

    private suspend fun flushCritical() {
        if (criticalBuffer.isEmpty()) return
        val batch = criticalBuffer.toList().also { criticalBuffer.clear() }
        sendWithBackoff(batch, "critical")
    }

    private suspend fun flushWifiPreferred() {
        if (policy.isOnWifi?.invoke() == false) {
            CoreSDKLogger.debug("TelemetryEngineImpl", "wifi-preferred flush skipped")
            return
        }
        if (wifiBuffer.isEmpty()) return
        val batch = wifiBuffer.toList().also { wifiBuffer.clear() }
        sendWithBackoff(batch, "wifi-preferred")
    }

    private suspend fun flushNormal() {
        if (normalBuffer.isEmpty()) return
        val batch = normalBuffer.toList().also { normalBuffer.clear() }
        sendWithBackoff(batch, "normal")
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun normalFlushThreshold(): Int {
        val dataSaver = policy.isDataSaver?.invoke() == true
        return if (dataSaver) FLUSH_THRESHOLD * policy.dataSaverThresholdMultiplier else FLUSH_THRESHOLD
    }

    private fun effectivePriority(event: TelemetryEvent): TelemetryPriority =
        if (event.priority == TelemetryPriority.WifiPreferred && policy.isOnWifi == null)
            TelemetryPriority.Normal
        else event.priority

    private fun shouldRecord(event: TelemetryEvent): Boolean = when (level) {
        TelemetryLevel.Off     -> false
        TelemetryLevel.Minimal -> event is TelemetryEvent.ModelLoad
                               || event is TelemetryEvent.ModelUnload
                               || event is TelemetryEvent.InferenceComplete
                               || event is TelemetryEvent.ControlPlaneAlert
        TelemetryLevel.Full    -> true
    }

    private suspend fun sendWithBackoff(batch: List<TelemetryEvent>, tag: String) {
        repeat(MAX_RETRIES) { attempt ->
            try {
                sink.ingest(batch)
                CoreSDKLogger.debug("TelemetryEngineImpl", "[$tag] flushed ${batch.size} events")
                return
            } catch (e: Exception) {
                if (attempt == MAX_RETRIES - 1) {
                    CoreSDKLogger.warn("TelemetryEngineImpl",
                        "[$tag] giving up after $MAX_RETRIES retries: ${e.message}")
                    // Re-queue to front of normal buffer on non-critical failures.
                    if (tag != "critical") {
                        val space = BUFFER_CAPACITY - normalBuffer.size
                        batch.takeLast(space).reversed().forEach { normalBuffer.addFirst(it) }
                    }
                    return
                }
                val delayMs = min(BACKOFF_BASE_MS * (1L shl attempt), BACKOFF_MAX_MS)
                CoreSDKLogger.debug("TelemetryEngineImpl",
                    "[$tag] attempt ${attempt + 1} failed, retry in ${delayMs}ms")
                delay(delayMs)
            }
        }
    }

    companion object {
        const val BUFFER_CAPACITY        = 256
        const val CRITICAL_BUFFER_CAPACITY = 32
        const val FLUSH_THRESHOLD        = 100
        const val WIFI_FLUSH_THRESHOLD   = 50
        const val MAX_RETRIES            = 3
        const val BACKOFF_BASE_MS        = 1_000L
        const val BACKOFF_MAX_MS         = 30_000L
    }
}
