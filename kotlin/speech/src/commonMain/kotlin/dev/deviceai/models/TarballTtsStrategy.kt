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
 * The archive is deleted after a successful extraction; a failed extraction
 * removes the partial tree so the next attempt starts clean.
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

        val extracted = SpeechBridge.extractTarBz2(archivePath, voiceDir)
        if (extracted <= 0) {
            fs.deleteFile(archivePath)
            throw ArchiveExtractException(model.id, extracted)
        }
        fs.deleteFile(archivePath)

        val root       = "$voiceDir/${model.topDir}"
        val modelPath  = "$root/${model.modelFile}"
        val tokensPath = "$root/tokens.txt"
        if (!fs.fileExists(modelPath) || !fs.fileExists(tokensPath)) {
            throw ArchiveExtractException(model.id, extracted, "expected $modelPath and $tokensPath after extraction")
        }

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
