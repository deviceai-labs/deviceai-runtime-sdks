package dev.deviceai.core

/**
 * Auto-detected device hardware capabilities.
 *
 * Each platform SDK implements [detectCapabilities] to read hardware info
 * from OS APIs. The result is merged into [CloudConfig.capabilityProfile]
 * and sent to the backend at device registration for capability tier scoring.
 *
 * Developers can override any auto-detected value via [CloudConfig.Builder.capabilityProfile].
 */
data class DeviceCapabilities(
    /** Total device RAM in GB (e.g. 8.0). */
    val ramGb: Double,
    /** Logical CPU core count (e.g. 8). */
    val cpuCores: Int,
    /** True if the device has a dedicated neural processing unit (NNAPI on Android, ANE on iOS). */
    val hasNpu: Boolean,
    /** SoC model name (e.g. "SM8550", "Apple A17 Pro"). Null if unavailable. */
    val socModel: String?,
    /** GPU renderer string (e.g. "Adreno (TM) 740", "Apple GPU"). Null if unavailable. */
    val gpuRenderer: String?,
    /** Available storage in MB. Null if unavailable. */
    val storageAvailableMb: Long?,
) {
    /** Convert to a Map suitable for merging into capabilityProfile. */
    fun toMap(): Map<String, Any?> = buildMap {
        put("ram_gb", ramGb)
        put("cpu_cores", cpuCores)
        put("has_npu", hasNpu)
        socModel?.let { put("soc_model", it) }
        gpuRenderer?.let { put("gpu_renderer", it) }
        storageAvailableMb?.let { put("storage_available_mb", it) }
    }
}

/**
 * Detect device hardware capabilities from OS APIs.
 *
 * @param context Android Context (required on Android, ignored on other platforms).
 */
expect fun detectCapabilities(context: Any?): DeviceCapabilities
