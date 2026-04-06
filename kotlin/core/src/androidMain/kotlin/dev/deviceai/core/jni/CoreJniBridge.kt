package dev.deviceai.core.jni

/**
 * JNI bridge to the C++ deviceai_core_jni shared library (wraps cpp/core).
 *
 * Call [init] once at process start, then use the typed methods to call
 * through to the C++ BackendClient, TelemetryEngine, and SessionStore.
 *
 * All methods are thread-safe — the C++ layer handles internal synchronization.
 */
internal object CoreJniBridge {

    init {
        System.loadLibrary("deviceai_core_jni")
    }

    /**
     * Register platform callbacks with the C++ layer. Must be called before
     * any backend or telemetry operations.
     */
    fun init(
        httpExecutor: JniHttpExecutor,
        storageExecutor: JniStorageExecutor,
    ) = nativeInit(httpExecutor, storageExecutor)

    // ── Backend ───────────────────────────────────────────────────────────────

    /**
     * Register this device. Returns JSON session string on success, null on failure.
     * JSON: {"device_id":"...","token":"...","expires_at_ms":...,"capability_tier":"..."}
     */
    fun registerDevice(
        baseUrl: String,
        apiKey: String,
        profileKeys: Array<String>,
        profileVals: Array<String>,
    ): String? = nativeRegisterDevice(baseUrl, apiKey, profileKeys, profileVals)

    /**
     * Fetch the model manifest. Returns raw manifest JSON, or null on failure.
     */
    fun fetchManifest(baseUrl: String, deviceToken: String): String? =
        nativeFetchManifest(baseUrl, deviceToken)

    /**
     * Refresh a device token. Returns the new token string, or null on failure.
     */
    fun refreshToken(
        baseUrl: String,
        deviceId: String,
        currentToken: String,
        capabilityTier: String,
        expiresAtMs: Long,
    ): String? = nativeRefreshToken(baseUrl, deviceId, currentToken, capabilityTier, expiresAtMs)

    // ── TelemetryEngine lifecycle ─────────────────────────────────────────────

    /**
     * Create a C++ TelemetryEngine. Returns an opaque handle (pointer cast to Long).
     * Must be destroyed via [destroyEngine] at shutdown.
     *
     * @param level 0=Off, 1=Minimal, 2=Full
     * @param hasWifiChecker true if the app provided an [isOnWifi] lambda
     * @param hasDataSaver   true if the app provided an [isDataSaver] lambda
     */
    fun createEngine(
        level: Int,
        hasWifiChecker: Boolean,
        hasDataSaver: Boolean,
        dataSaverMultiplier: Int,
        baseUrl: String,
    ): Long = nativeCreateEngine(level, hasWifiChecker, hasDataSaver, dataSaverMultiplier, baseUrl)

    fun destroyEngine(handle: Long) = nativeDestroyEngine(handle)

    /**
     * Attach a device token so the C++ engine can authenticate telemetry batches.
     * Call after a successful [registerDevice].
     */
    fun setSession(handle: Long, deviceToken: String, sessionId: String) =
        nativeSetSession(handle, deviceToken, sessionId)

    fun flushEngine(handle: Long) = nativeFlushEngine(handle)

    // ── Event recording ───────────────────────────────────────────────────────

    fun recordModelLoad(
        handle: Long, timestampMs: Long, module: String, modelId: String, durationMs: Long,
    ) = nativeRecordModelLoad(handle, timestampMs, module, modelId, durationMs)

    fun recordModelUnload(
        handle: Long, timestampMs: Long, module: String, modelId: String,
    ) = nativeRecordModelUnload(handle, timestampMs, module, modelId)

    fun recordInferenceComplete(
        handle: Long,
        timestampMs: Long,
        module: String,
        modelId: String,
        latencyMs: Long,
        ttftMs: Long,             hasTtft: Boolean,
        tokensPerSec: Float,      hasTps: Boolean,
        inputTokenCount: Int,     hasInputTokens: Boolean,
        outputTokenCount: Int,    hasOutputTokens: Boolean,
        inputLengthMs: Int,       hasInputLength: Boolean,
        outputChars: Int,         hasOutputChars: Boolean,
        finishReason: String,
    ) = nativeRecordInferenceComplete(
        handle, timestampMs, module, modelId, latencyMs,
        ttftMs, hasTtft, tokensPerSec, hasTps,
        inputTokenCount, hasInputTokens,
        outputTokenCount, hasOutputTokens,
        inputLengthMs, hasInputLength,
        outputChars, hasOutputChars,
        finishReason,
    )

    fun recordManifestSync(
        handle: Long, timestampMs: Long, success: Boolean, modelCount: Int, errorCode: String,
    ) = nativeRecordManifestSync(handle, timestampMs, success, modelCount, errorCode)

    fun recordControlPlaneAlert(
        handle: Long, timestampMs: Long, alertType: String, modelId: String, rolloutId: String,
    ) = nativeRecordControlPlaneAlert(handle, timestampMs, alertType, modelId, rolloutId)

    // ── Native declarations ───────────────────────────────────────────────────

    private external fun nativeInit(http: JniHttpExecutor, storage: JniStorageExecutor)

    private external fun nativeRegisterDevice(
        baseUrl: String, apiKey: String,
        profileKeys: Array<String>, profileVals: Array<String>,
    ): String?

    private external fun nativeFetchManifest(baseUrl: String, deviceToken: String): String?

    private external fun nativeRefreshToken(
        baseUrl: String, deviceId: String, currentToken: String,
        capabilityTier: String, expiresAtMs: Long,
    ): String?

    private external fun nativeCreateEngine(
        level: Int,
        hasWifiChecker: Boolean,
        hasDataSaver: Boolean,
        dataSaverMultiplier: Int,
        baseUrl: String,
    ): Long

    private external fun nativeDestroyEngine(handle: Long)
    private external fun nativeSetSession(handle: Long, token: String, sessionId: String)
    private external fun nativeFlushEngine(handle: Long)

    private external fun nativeRecordModelLoad(
        handle: Long, timestampMs: Long, module: String, modelId: String, durationMs: Long,
    )

    private external fun nativeRecordModelUnload(
        handle: Long, timestampMs: Long, module: String, modelId: String,
    )

    private external fun nativeRecordInferenceComplete(
        handle: Long,
        timestampMs: Long, module: String, modelId: String, latencyMs: Long,
        ttftMs: Long,          hasTtft: Boolean,
        tokensPerSec: Float,   hasTps: Boolean,
        inputTokenCount: Int,  hasInputTokens: Boolean,
        outputTokenCount: Int, hasOutputTokens: Boolean,
        inputLengthMs: Int,    hasInputLength: Boolean,
        outputChars: Int,      hasOutputChars: Boolean,
        finishReason: String,
    )

    private external fun nativeRecordManifestSync(
        handle: Long, timestampMs: Long, success: Boolean, modelCount: Int, errorCode: String,
    )

    private external fun nativeRecordControlPlaneAlert(
        handle: Long, timestampMs: Long, alertType: String, modelId: String, rolloutId: String,
    )
}
