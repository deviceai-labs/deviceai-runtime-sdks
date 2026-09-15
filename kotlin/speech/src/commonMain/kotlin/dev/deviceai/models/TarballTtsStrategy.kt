package dev.deviceai.models

import dev.deviceai.SpeechBridge

/**
 * Downloads a sherpa-onnx TTS voice tarball, verifies it, extracts it, and
 * registers the model. Files land at:
 *
 *   {models}/tts/{id}/{topDir}/{modelFile}        → [LocalModel.modelPath]
 *   {models}/tts/{id}/{topDir}/tokens.txt         → [LocalModel.configPath]
 *   {models}/tts/{id}/{topDir}/espeak-ng-data/    → [LocalModel.ttsDataDir]
 *
 * The archive is deleted after a successful extraction. A failed extraction
 * deletes both the archive and the partially extracted `{topDir}` so the next
 * attempt re-downloads and starts clean.
 */
internal class TarballTtsStrategy(
    private val http: HttpFileDownloader,
    private val fs: FileSystem,
    private val paths: StoragePaths,
    private val store: MetadataStore,
) : ModelDownloadStrategy {

    override fun supports(model: ModelInfo): Boolean = model is TtsTarballInfo

    override suspend fun download(
        model: ModelInfo,
        onProgress: (DownloadProgress) -> Unit,
    ): LocalModel {
        model as TtsTarballInfo
        val voiceDir    = "${paths.getModelsDir()}/tts/${model.id}"
        val archivePath = "$voiceDir/${model.archiveUrl.substringAfterLast('/')}"
        fs.ensureDirectoryExists(voiceDir)

        http.download(model.archiveUrl, archivePath, onProgress, expectedSha256 = model.archiveSha256)

        val root       = "$voiceDir/${model.topDir}"
        val modelPath  = "$root/${model.modelFile}"
        val tokensPath = "$root/tokens.txt"

        val extracted = SpeechBridge.extractTarBz2(archivePath, voiceDir)
        if (extracted <= 0 || !fs.fileExists(modelPath) || !fs.fileExists(tokensPath)) {
            // Start the next attempt clean: drop the archive and whatever was
            // written before the failure (deleteFile recurses on directories).
            fs.deleteFile(archivePath)
            fs.deleteFile(root)
            throw ArchiveExtractException(
                model.id, extracted,
                if (extracted > 0) "expected $modelPath and $tokensPath after extraction" else null,
            )
        }
        fs.deleteFile(archivePath)

        val localModel = LocalModel(
            modelId      = model.id,
            modelType    = LocalModelType.TTS,
            modelPath    = modelPath,
            configPath   = tokensPath,
            downloadedAt = currentTimeMillis(),
        )
        store.addModel(localModel)
        return localModel
    }
}

class ArchiveExtractException(voiceId: String, result: Int, detail: String? = null) :
    RuntimeException("Extracting voice $voiceId failed (extractor returned $result)" + (detail?.let { ": $it" } ?: ""))
