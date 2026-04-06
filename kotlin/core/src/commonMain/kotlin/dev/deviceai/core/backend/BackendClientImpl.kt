package dev.deviceai.core.backend

import dev.deviceai.core.CoreSDKLogger
import dev.deviceai.core.telemetry.TelemetryEvent
import dev.deviceai.models.currentTimeMillis
import io.ktor.client.*
import io.ktor.client.call.*
import io.ktor.client.plugins.contentnegotiation.*
import io.ktor.client.request.*
import io.ktor.http.*
import io.ktor.serialization.kotlinx.json.*
import kotlinx.serialization.json.*

/**
 * Ktor-based [BackendClient] for iOS (and any platform without JNI).
 *
 * Handles the DeviceAI control-plane endpoints:
 * - `POST /v1/devices/register`
 * - `GET  /v1/manifest`
 * - `POST /v1/telemetry/batch`
 * - `POST /v1/devices/refresh`
 */
internal class BackendClientImpl(
    private val baseUrl: String,
    private val apiKey: String,
) : BackendClient {

    private val http = HttpClient {
        install(ContentNegotiation) { json(Json { ignoreUnknownKeys = true }) }
    }

    private val jsonCodec = Json { ignoreUnknownKeys = true }

    override suspend fun registerDevice(capabilityProfile: Map<String, Any?>): DeviceSession {
        val response = http.post("$baseUrl/v1/devices/register") {
            bearerAuth(apiKey)
            contentType(ContentType.Application.Json)
            setBody(buildJsonObject {
                put("capability_profile", capabilityProfile.toJsonObject())
            })
        }
        val body = response.body<JsonObject>()
        return DeviceSession(
            deviceId       = body["device_id"]?.jsonPrimitive?.content ?: "",
            token          = body["token"]?.jsonPrimitive?.content ?: "",
            expiresAtMs    = currentTimeMillis() + TOKEN_LIFETIME_MS,
            capabilityTier = body["capability_tier"]?.jsonPrimitive?.content ?: "mid",
        )
    }

    override suspend fun fetchManifest(deviceToken: String): ManifestResponse {
        val response = http.get("$baseUrl/v1/manifest") { bearerAuth(deviceToken) }
        return jsonCodec.decodeFromString<ManifestResponse>(response.body())
    }

    override suspend fun ingestTelemetry(
        deviceToken: String,
        sessionId: String,
        events: List<TelemetryEvent>,
    ) {
        val body = buildJsonObject {
            put("session_id", sessionId)
            put("events", buildJsonArray { events.take(500).forEach { add(it.toJsonObject()) } })
        }
        http.post("$baseUrl/v1/telemetry/batch") {
            bearerAuth(deviceToken)
            contentType(ContentType.Application.Json)
            setBody(body)
        }
    }

    override suspend fun refreshToken(session: DeviceSession): DeviceSession? = try {
        val response = http.post("$baseUrl/v1/devices/refresh") { bearerAuth(session.token) }
        val body = response.body<JsonObject>()
        val newToken = body["token"]?.jsonPrimitive?.content ?: return null
        session.copy(token = newToken, expiresAtMs = currentTimeMillis() + TOKEN_LIFETIME_MS)
    } catch (e: Exception) {
        CoreSDKLogger.warn("BackendClientImpl", "token refresh failed: ${e.message}")
        null
    }

    override fun close() = http.close()

    companion object {
        private const val TOKEN_LIFETIME_MS = 30L * 24 * 60 * 60 * 1000
    }
}

private fun Map<String, Any?>.toJsonObject(): JsonObject = buildJsonObject {
    forEach { (k, v) ->
        when (v) {
            is String  -> put(k, v)
            is Boolean -> put(k, v)
            is Int     -> put(k, v)
            is Long    -> put(k, v)
            is Float   -> put(k, v)
            is Double  -> put(k, v)
            else       -> v?.let { put(k, it.toString()) }
        }
    }
}
