package dev.deviceai.core.jni

import dev.deviceai.core.backend.BackendClient
import dev.deviceai.core.backend.DeviceSession
import dev.deviceai.core.backend.ManifestEntry
import dev.deviceai.core.backend.ManifestResponse
import dev.deviceai.core.telemetry.TelemetryEvent
import kotlinx.serialization.json.Json

/**
 * JNI-backed [BackendClient]. Delegates all HTTP to the C++ layer via [CoreJniBridge],
 * which uses the platform-injected OkHttp executor — no Ktor on this path.
 */
internal class JniBackendClient(
    private val baseUrl: String,
    private val apiKey: String,
) : BackendClient {

    private val json = Json { ignoreUnknownKeys = true }

    override suspend fun registerDevice(capabilityProfile: Map<String, Any?>): DeviceSession {
        val (keys, vals) = capabilityProfile.toJniArrays()
        val result = CoreJniBridge.registerDevice(baseUrl, apiKey, keys, vals)
            ?: error("registerDevice returned null — check API key and network")

        // Parse: {"device_id":"...","token":"...","expires_at_ms":...,"capability_tier":"..."}
        return json.decodeFromString<DeviceSessionDto>(result).toDeviceSession()
    }

    override suspend fun fetchManifest(deviceToken: String): ManifestResponse {
        val raw = CoreJniBridge.fetchManifest(baseUrl, deviceToken)
            ?: error("fetchManifest returned null — token may be expired")
        return json.decodeFromString<ManifestResponseDto>(raw).toManifestResponse()
    }

    override suspend fun ingestTelemetry(
        deviceToken: String,
        sessionId: String,
        events: List<TelemetryEvent>,
    ) {
        // JNI engine handles batching and delivery internally via its worker thread.
        // This path is only reached if a custom TelemetrySink is routed through this client.
        // No-op here — the C++ engine already sent these events.
    }

    override suspend fun refreshToken(session: DeviceSession): DeviceSession? {
        val newToken = CoreJniBridge.refreshToken(
            baseUrl         = baseUrl,
            deviceId        = session.deviceId,
            currentToken    = session.token,
            capabilityTier  = session.capabilityTier,
            expiresAtMs     = session.expiresAtMs,
        ) ?: return null
        return session.copy(
            token        = newToken,
            expiresAtMs  = System.currentTimeMillis() + TOKEN_LIFETIME_MS,
        )
    }

    override fun close() { /* C++ engine lifecycle managed separately */ }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun Map<String, Any?>.toJniArrays(): Pair<Array<String>, Array<String>> {
        val keys = mutableListOf<String>()
        val vals = mutableListOf<String>()
        forEach { (k, v) ->
            keys += k
            vals += when (v) {
                is String  -> "\"$v\""
                is Boolean -> v.toString()
                is Int     -> v.toString()
                is Long    -> v.toString()
                is Float   -> v.toString()
                is Double  -> v.toString()
                else       -> "\"${v?.toString() ?: "null"}\""
            }
        }
        return keys.toTypedArray() to vals.toTypedArray()
    }

    companion object {
        private const val TOKEN_LIFETIME_MS = 30L * 24 * 60 * 60 * 1000
    }
}

// ── Minimal DTOs for JSON parsing (JNI path returns simple JSON strings) ──────

@kotlinx.serialization.Serializable
private data class DeviceSessionDto(
    @kotlinx.serialization.SerialName("device_id")       val deviceId: String,
    val token: String,
    @kotlinx.serialization.SerialName("expires_at_ms")   val expiresAtMs: Long,
    @kotlinx.serialization.SerialName("capability_tier") val capabilityTier: String,
) {
    fun toDeviceSession() = DeviceSession(deviceId, token, expiresAtMs, capabilityTier)
}

@kotlinx.serialization.Serializable
private data class ManifestResponseDto(
    @kotlinx.serialization.SerialName("device_id")  val deviceId: String = "",
    @kotlinx.serialization.SerialName("app_id")     val appId: String = "",
    val tier: String = "mid",
    @kotlinx.serialization.SerialName("issued_at")  val issuedAt: String = "",
    @kotlinx.serialization.SerialName("expires_at") val expiresAt: String = "",
    val models: List<ManifestEntryDto> = emptyList(),
    val signature: String = "",
) {
    fun toManifestResponse() = ManifestResponse(
        deviceId  = deviceId,
        appId     = appId,
        tier      = tier,
        issuedAt  = issuedAt,
        expiresAt = expiresAt,
        models    = models.map { it.toManifestEntry() },
        signature = signature,
    )
}

@kotlinx.serialization.Serializable
private data class ManifestEntryDto(
    val module: String = "",
    @kotlinx.serialization.SerialName("model_id")   val modelId: String = "",
    val version: String = "",
    val sha256: String = "",
    @kotlinx.serialization.SerialName("size_bytes")  val sizeBytes: Long = 0,
    @kotlinx.serialization.SerialName("cdn_path")    val cdnPath: String = "",
    @kotlinx.serialization.SerialName("rollout_id")  val rolloutId: String = "",
) {
    fun toManifestEntry() = ManifestEntry(
        module    = module,
        modelId   = modelId,
        version   = version,
        sha256    = sha256,
        sizeBytes = sizeBytes,
        cdnPath   = cdnPath,
        rolloutId = rolloutId,
    )
}
