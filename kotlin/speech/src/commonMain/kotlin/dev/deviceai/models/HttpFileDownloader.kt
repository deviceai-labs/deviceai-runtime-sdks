package dev.deviceai.models

import io.ktor.client.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.*
import io.ktor.utils.io.*
import kotlinx.coroutines.ensureActive
import kotlin.coroutines.coroutineContext

/**
 * Downloads a single file over HTTP with resume support and progress reporting.
 *
 * - Streams bytes to a `.tmp` file, then atomically moves it to [destPath].
 * - Partial downloads resume automatically via HTTP Range headers.
 * - Uses [FileSystem] for directory/file management; byte-appending during download
 *   goes through a platform `appendToFile` helper (expect/actual).
 */
internal class HttpFileDownloader(
    private val client: HttpClient,
    private val config: RegistryConfig,
    private val fs: FileSystem
) {
    suspend fun download(
        url: String,
        destPath: String,
        onProgress: (DownloadProgress) -> Unit = {},
        expectedSha256: String? = null,
    ) {
        val destDir = destPath.substringBeforeLast('/')
        fs.ensureDirectoryExists(destDir)

        val tempPath = "$destPath.tmp"
        val existingBytes = fs.fileSize(tempPath).let { if (it > 0) it else 0L }

        onProgress(DownloadProgress.pending())

        var expectedTotal = 0L

        client.prepareGet(url) {
            if (existingBytes > 0) {
                header(HttpHeaders.Range, "bytes=$existingBytes-")
            }
        }.execute { httpResponse ->
            // A 404 body is fifteen bytes of "Entry not found"; without this it
            // was written to disk and handed to the engine as a model.
            val status = httpResponse.status.value
            if (status !in 200..299) {
                throw HttpDownloadException(url, status)
            }
            val isResuming = status == 206 && existingBytes > 0
            val contentLength = httpResponse.contentLength() ?: 0L
            val totalBytes = if (isResuming) contentLength + existingBytes else contentLength
            expectedTotal = totalBytes

            val channel: ByteReadChannel = httpResponse.bodyAsChannel()
            val buffer = ByteArray(config.downloadBufferSize)
            var bytesDownloaded = if (isResuming) existingBytes else 0L

            if (!isResuming && existingBytes > 0) {
                fs.deleteFile(tempPath)
            }

            while (!channel.isClosedForRead) {
                coroutineContext.ensureActive()

                val bytesRead = channel.readAvailable(buffer, 0, buffer.size)
                if (bytesRead <= 0) break

                appendToFile(tempPath, buffer, bytesRead)
                bytesDownloaded += bytesRead

                val percent = if (totalBytes > 0) {
                    (bytesDownloaded.toFloat() / totalBytes * 100f).coerceIn(0f, 100f)
                } else 0f

                onProgress(
                    DownloadProgress(
                        bytesDownloaded = bytesDownloaded,
                        totalBytes = totalBytes,
                        percentComplete = percent,
                        state = DownloadState.DOWNLOADING
                    )
                )
            }
        }

        // The read loop ends on a closed channel OR a short read, and a dropped
        // connection looks exactly like completion from here. Without this check
        // a truncated .tmp was promoted to the real file — observed as a 24 MB
        // Piper voice (should be 63 MB) that ONNX Runtime then aborted on with
        // "protobuf parsing failed". Keep the .tmp so the Range resume on the
        // next attempt picks up where this one stopped.
        val expected = expectedTotal
        val actual = fs.fileSize(tempPath)
        if (expected > 0 && actual != expected) {
            throw IncompleteDownloadException(url, expected, actual)
        }

        if (expectedSha256 != null) {
            val actualHash = sha256File(tempPath)
            if (actualHash == null || !actualHash.equals(expectedSha256, ignoreCase = true)) {
                // Wrong bytes, not short bytes: a resume would not help. Drop it.
                fs.deleteFile(tempPath)
                throw ChecksumMismatchException(url, expectedSha256, actualHash)
            }
        }

        fs.deleteFile(destPath)
        if (!fs.moveFile(tempPath, destPath)) {
            throw RuntimeException("Failed to move downloaded file to $destPath")
        }

        onProgress(DownloadProgress.completed(fs.fileSize(destPath)))
    }
}

/**
 * The server closed the connection before sending every byte it promised.
 * The partial `.tmp` is left on disk so the next call resumes via Range.
 */
class IncompleteDownloadException(
    val url: String,
    val expectedBytes: Long,
    val actualBytes: Long,
) : RuntimeException(
    "Download of $url stopped at $actualBytes of $expectedBytes bytes (${expectedBytes - actualBytes} short)"
)

/** The server answered with a non-2xx status; nothing was written. */
class HttpDownloadException(val url: String, val status: Int) :
    RuntimeException("Download of $url failed: HTTP $status")

/** The file arrived complete but its SHA-256 does not match the catalog. Deleted. */
class ChecksumMismatchException(val url: String, val expected: String, val actual: String?) :
    RuntimeException("Checksum mismatch for $url: expected $expected, got ${actual ?: "unreadable"}")
