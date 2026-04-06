package dev.deviceai.core.jni

import dev.deviceai.models.PlatformStorage

/**
 * PlatformStorage-backed storage executor injected into the C++ layer.
 *
 * Called from C++ (session_store.cpp) via JNI to persist and load DeviceSession.
 * Method signatures must match the JNI method ID lookups in dai_core_jni.cpp.
 */
internal class JniStorageExecutor {

    @Suppress("unused") // called via JNI
    fun readFile(path: String): String? =
        runCatching { PlatformStorage.readText(path) }.getOrNull()

    @Suppress("unused") // called via JNI
    fun writeFile(path: String, content: String): Boolean = runCatching {
        PlatformStorage.ensureDirectoryExists(PlatformStorage.getModelsDir())
        PlatformStorage.writeText(path, content)
        true
    }.getOrDefault(false)

    @Suppress("unused") // called via JNI
    fun deleteFile(path: String): Boolean =
        runCatching { PlatformStorage.deleteFile(path); true }.getOrDefault(false)

    @Suppress("unused") // called via JNI
    fun modelsDir(): String = PlatformStorage.getModelsDir()
}
