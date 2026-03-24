package dev.deviceai.models

import android.annotation.SuppressLint
import android.content.Context
import java.io.File

@Suppress("EXPECT_ACTUAL_CLASSIFIERS_ARE_IN_BETA_WARNING")
actual object PlatformStorage : StoragePaths, FileSystem {

    @SuppressLint("StaticFieldLeak")
    private var appContext: Context? = null

    /**
     * Must be called before using ModelRegistry on Android.
     * Typically called from Application.onCreate() or Activity.
     */
    fun initialize(context: Context) {
        appContext = context.applicationContext
    }

    private fun requireContext(): Context =
        appContext ?: throw IllegalStateException(
            "PlatformStorage not initialized. Call PlatformStorage.initialize(context) first."
        )

    actual override fun getModelsDir(): String =
        File(requireContext().filesDir, "speechkmp_models").absolutePath

    actual override fun getTempDir(): String =
        File(requireContext().cacheDir, "speechkmp_tmp").absolutePath

    actual override fun ensureDirectoryExists(path: String): Boolean =
        File(path).let { it.exists() || it.mkdirs() }

    actual override fun fileExists(path: String): Boolean =
        File(path).exists()

    actual override fun deleteFile(path: String): Boolean =
        File(path).let { if (it.isDirectory) it.deleteRecursively() else it.delete() }

    actual override fun moveFile(from: String, to: String): Boolean =
        File(from).renameTo(File(to))

    actual override fun fileSize(path: String): Long =
        File(path).let { if (it.exists()) it.length() else -1L }

    actual override fun readText(path: String): String? =
        try { File(path).takeIf { it.exists() }?.readText() } catch (_: Exception) { null }

    actual override fun writeText(path: String, content: String): Boolean =
        try { File(path).writeText(content); true } catch (_: Exception) { false }

    /** Returns the app's private files directory, or null if context is not yet initialised. */
    internal fun tryGetFilesDir(): String? = appContext?.filesDir?.absolutePath
}
