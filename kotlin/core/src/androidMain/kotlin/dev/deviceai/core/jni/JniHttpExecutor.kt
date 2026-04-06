package dev.deviceai.core.jni

import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException

/**
 * OkHttp-backed HTTP executor injected into the C++ layer.
 *
 * The C++ telemetry worker thread calls [httpPost] and [httpGet] synchronously
 * via JNI. OkHttp executes the request on the calling thread (already a background
 * thread managed by [dai_telemetry_engine_t]'s worker).
 *
 * Method signatures must match the JNI method ID lookups in dai_core_jni.cpp.
 */
internal class JniHttpExecutor {

    private val client = OkHttpClient()

    /**
     * Called from C++ worker thread via JNI.
     * Signature: (Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;I)
     *            Ldev/deviceai/core/jni/HttpResult;
     */
    @Suppress("unused") // called via JNI
    fun httpPost(
        url: String,
        body: String?,
        headerKeys: Array<String>,
        headerVals: Array<String>,
        headerCount: Int,
    ): HttpResult {
        return try {
            val requestBody = (body ?: "").toRequestBody(JSON_MEDIA_TYPE)
            val request = Request.Builder().url(url).post(requestBody)
                .applyHeaders(headerKeys, headerVals, headerCount)
                .build()
            client.newCall(request).execute().use { resp ->
                HttpResult(
                    statusCode = resp.code,
                    body       = resp.body?.string(),
                    error      = null,
                )
            }
        } catch (e: IOException) {
            HttpResult(statusCode = 0, body = null, error = e.message)
        }
    }

    /**
     * Called from C++ worker thread via JNI.
     * Signature: (Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;I)
     *            Ldev/deviceai/core/jni/HttpResult;
     */
    @Suppress("unused") // called via JNI
    fun httpGet(
        url: String,
        @Suppress("UNUSED_PARAMETER") body: String?,  // unused for GET, mirrors C++ signature
        headerKeys: Array<String>,
        headerVals: Array<String>,
        headerCount: Int,
    ): HttpResult {
        return try {
            val request = Request.Builder().url(url).get()
                .applyHeaders(headerKeys, headerVals, headerCount)
                .build()
            client.newCall(request).execute().use { resp ->
                HttpResult(
                    statusCode = resp.code,
                    body       = resp.body?.string(),
                    error      = null,
                )
            }
        } catch (e: IOException) {
            HttpResult(statusCode = 0, body = null, error = e.message)
        }
    }

    private fun Request.Builder.applyHeaders(
        keys: Array<String>, vals: Array<String>, count: Int,
    ): Request.Builder {
        for (i in 0 until count) {
            // Skip Authorization — C++ injects it; let OkHttp pass it through.
            // Skip Content-Type — set via RequestBody.
            if (keys[i].equals("Content-Type", ignoreCase = true)) continue
            addHeader(keys[i], vals[i])
        }
        return this
    }

    companion object {
        private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()
    }
}
