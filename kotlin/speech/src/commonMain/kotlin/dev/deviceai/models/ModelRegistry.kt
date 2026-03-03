package dev.deviceai.models

import io.ktor.client.*
import io.ktor.client.plugins.contentnegotiation.*
import io.ktor.serialization.kotlinx.json.*
import kotlinx.serialization.json.Json

/**
 * Central registry for discovering, downloading, and managing Whisper and Piper models.
 *
 * Models are fetched from HuggingFace public repos (no API key required):
 * - Whisper: ggerganov/whisper.cpp (GGML .bin files)
 * - Piper: rhasspy/piper-voices (ONNX .onnx + .json files)
 *
 * Usage:
 * ```kotlin
 * // On Android, initialize storage first:
 * // PlatformStorage.initialize(context)
 *
 * ModelRegistry.initialize()
 *
 * // Discover models
 * val whisperModels = ModelRegistry.getWhisperModels()
 * val piperVoices = ModelRegistry.getPiperVoices(language = "en")
 *
 * // Download with progress
 * val tiny = whisperModels.first { it.size == WhisperSize.TINY }
 * val local = ModelRegistry.download(tiny) { progress ->
 *     println("${progress.percentComplete}%")
 * }.getOrThrow()
 *
 * // Use with SpeechBridge
 * SpeechBridge.initStt(local.modelPath, SttConfig())
 * ```
 */
object ModelRegistry {
    private var initialized = false
    private var config = RegistryConfig()
    private lateinit var client: HttpClient
    private lateinit var whisperCatalog: WhisperCatalog
    private lateinit var piperCatalog: PiperCatalog
    private lateinit var downloader: ModelDownloader
    private lateinit var store: MetadataStore

    /**
     * Initialize the registry with optional configuration.
     * Must be called before any other methods.
     *
     * On Android, call [PlatformStorage.initialize] with a Context first.
     */
    fun initialize(config: RegistryConfig = RegistryConfig()) {
        this.config = config
        this.client = HttpClient {
            install(ContentNegotiation) {
                json(Json { ignoreUnknownKeys = true })
            }
        }
        this.whisperCatalog = WhisperCatalog(client, config)
        this.piperCatalog = PiperCatalog(client, config)

        // Wire up DI: MetadataStore and strategies depend on FileSystem + StoragePaths,
        // both implemented by PlatformStorage — injected here at the composition root.
        val fs: FileSystem = PlatformStorage
        val paths: StoragePaths = PlatformStorage
        this.store = MetadataStore(fs, paths)
        val http = HttpFileDownloader(client, config, fs)
        this.downloader = ModelDownloader(
            listOf(
                WhisperDownloadStrategy(http, fs, paths, store),
                PiperDownloadStrategy(http, fs, paths, store)
            )
        )
        initialized = true
    }

    private fun requireInitialized() {
        if (!initialized) throw IllegalStateException(
            "ModelRegistry not initialized. Call ModelRegistry.initialize() first."
        )
    }

    // ══════════════════════════════════════════════════════════════
    //                         DISCOVERY
    // ══════════════════════════════════════════════════════════════

    /**
     * Fetch available Whisper GGML models from HuggingFace.
     * Results are cached for the duration specified in [RegistryConfig].
     */
    suspend fun getWhisperModels(): List<WhisperModelInfo> {
        requireInitialized()
        return whisperCatalog.fetchModels()
    }

    /**
     * Fetch available Piper TTS voices from HuggingFace.
     *
     * @param language Filter by language code prefix (e.g. "en", "en_US"). Null returns all.
     * @param quality Filter by quality tier. Null returns all.
     */
    suspend fun getPiperVoices(
        language: String? = null,
        quality: PiperQuality? = null
    ): List<PiperVoiceInfo> {
        requireInitialized()
        return piperCatalog.fetchVoices(language, quality)
    }

    // ══════════════════════════════════════════════════════════════
    //                         DOWNLOAD
    // ══════════════════════════════════════════════════════════════

    /**
     * Download a model to local storage.
     *
     * Supports resume (partial downloads are preserved) and cancellation.
     * Returns a [LocalModel] with paths ready for use with [SpeechBridge].
     *
     * @param model The model or voice to download
     * @param onProgress Called with download progress updates
     * @return [Result] containing the [LocalModel] on success
     */
    suspend fun download(
        model: ModelInfo,
        onProgress: (DownloadProgress) -> Unit = {}
    ): Result<LocalModel> {
        requireInitialized()

        // Return cached model immediately if already downloaded (all required files present).
        val existing = store.getModel(model.id)
        val existingConfig = existing?.configPath
        if (existing != null
            && PlatformStorage.fileExists(existing.modelPath)
            && (existingConfig == null || PlatformStorage.fileExists(existingConfig))
        ) {
            onProgress(DownloadProgress.completed(PlatformStorage.fileSize(existing.modelPath)))
            return Result.success(existing)
        }

        return try {
            Result.success(downloader.download(model, onProgress))
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /**
     * Download a file from an arbitrary URL into the models directory.
     *
     * Intended for modules (e.g. runtime-llm) that have their own model catalogs
     * but want to reuse the shared HTTP + metadata infrastructure.
     *
     * @param modelId  Unique identifier used to cache / look up the model
     * @param url      Direct download URL (e.g. HuggingFace resolve URL)
     * @param modelType  Type discriminator stored in metadata (e.g. LocalModelType("LLM"))
     * @param onProgress Called with download progress updates
     * @return [Result] containing the [LocalModel] on success
     */
    suspend fun downloadRawFile(
        modelId: String,
        url: String,
        modelType: LocalModelType,
        onProgress: (DownloadProgress) -> Unit = {}
    ): Result<LocalModel> {
        requireInitialized()

        // Return cached model immediately if already downloaded.
        val existing = store.getModel(modelId)
        if (existing != null && PlatformStorage.fileExists(existing.modelPath)) {
            onProgress(DownloadProgress.completed(PlatformStorage.fileSize(existing.modelPath)))
            return Result.success(existing)
        }

        val filename = url.substringAfterLast('/')
        val destDir  = "${PlatformStorage.getModelsDir()}/${modelType.id.lowercase()}"
        val destPath = "$destDir/$filename"

        return try {
            HttpFileDownloader(client, config, PlatformStorage).download(url, destPath, onProgress)
            val local = LocalModel(
                modelId      = modelId,
                modelType    = modelType,
                modelPath    = destPath,
                downloadedAt = currentTimeMillis()
            )
            store.addModel(local)
            Result.success(local)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /**
     * Cancel an in-progress download.
     */
    fun cancelDownload(modelId: String) {
        requireInitialized()
        downloader.cancelDownload(modelId)
    }

    // ══════════════════════════════════════════════════════════════
    //                      LOCAL MANAGEMENT
    // ══════════════════════════════════════════════════════════════

    /**
     * Get all models that have been downloaded to local storage.
     */
    fun getDownloadedModels(): List<LocalModel> {
        requireInitialized()
        return store.loadDownloadedModels().filter {
            val configPath = it.configPath
            PlatformStorage.fileExists(it.modelPath) &&
                (configPath == null || PlatformStorage.fileExists(configPath))
        }
    }

    /**
     * Get a specific downloaded model by ID, or null if not found.
     */
    fun getLocalModel(modelId: String): LocalModel? {
        requireInitialized()
        val model = store.getModel(modelId) ?: return null
        return if (PlatformStorage.fileExists(model.modelPath)) model else null
    }

    /**
     * Delete a downloaded model and its files.
     *
     * @return true if the model was found and deleted
     */
    fun deleteModel(modelId: String): Boolean {
        requireInitialized()
        val model = store.getModel(modelId) ?: return false

        PlatformStorage.deleteFile(model.modelPath)
        model.configPath?.let { PlatformStorage.deleteFile(it) }

        if (model.modelType == LocalModelType.PIPER) {
            val voiceDir = model.modelPath.substringBeforeLast('/')
            PlatformStorage.deleteFile(voiceDir)
        }

        store.removeModel(modelId)
        return true
    }

    // ══════════════════════════════════════════════════════════════
    //                           CACHE
    // ══════════════════════════════════════════════════════════════

    /**
     * Force re-fetch of all catalogs from HuggingFace, ignoring cache.
     */
    suspend fun refreshCatalog() {
        requireInitialized()
        whisperCatalog.clearCache()
        piperCatalog.clearCache()
        whisperCatalog.fetchModels()
        piperCatalog.fetchVoices()
    }

    /**
     * Clear cached catalog data. Next call to getWhisperModels/getPiperVoices will re-fetch.
     */
    fun clearCatalogCache() {
        requireInitialized()
        whisperCatalog.clearCache()
        piperCatalog.clearCache()
    }
}
