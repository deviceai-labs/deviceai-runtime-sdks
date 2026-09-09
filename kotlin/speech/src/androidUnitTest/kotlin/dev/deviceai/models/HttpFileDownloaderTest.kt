package dev.deviceai.models

import io.ktor.client.HttpClient
import io.ktor.client.engine.mock.MockEngine
import io.ktor.client.engine.mock.respond
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.headersOf
import kotlinx.coroutines.test.runTest
import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * Each test is a failure mode observed on the Redmi Note 9 Pro (2026-09-04),
 * where the downloader promoted whatever it received into the model path:
 * a 24 MB prefix of a 63 MB Piper voice, and the 15-byte body of a 404.
 */
class HttpFileDownloaderTest {

    private val tmp: File = File(System.getProperty("java.io.tmpdir"), "dai-dl-${System.nanoTime()}").apply { mkdirs() }
    private val fs = JavaFileSystem()
    private val config = RegistryConfig()

    private fun client(handler: suspend (io.ktor.client.request.HttpRequestData) -> io.ktor.client.engine.mock.MockRequestHandleScope.() -> io.ktor.client.request.HttpResponseData) =
        HttpClient(MockEngine { req -> handler(req)(this) })

    private fun sha256(bytes: ByteArray) =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    @Test
    fun `complete download is promoted and reported`() = runTest {
        val body = ByteArray(50_000) { it.toByte() }
        val http = HttpFileDownloader(client { { respond(body, HttpStatusCode.OK, headersOf(HttpHeaders.ContentLength, body.size.toString())) } }, config, fs)
        val dest = File(tmp, "ok.bin").path

        http.download("https://x/ok.bin", dest)

        assertEquals(body.size.toLong(), File(dest).length())
        assertFalse(File("$dest.tmp").exists(), ".tmp must be moved into place")
    }

    @Test
    fun `truncated body is not promoted and the partial is kept for resume`() = runTest {
        // Server promises 63 MB-ish, delivers a prefix — the observed Piper failure.
        val promised = 100_000L
        val delivered = ByteArray(24_000) { 1 }
        val http = HttpFileDownloader(client { { respond(delivered, HttpStatusCode.OK, headersOf(HttpHeaders.ContentLength, promised.toString())) } }, config, fs)
        val dest = File(tmp, "short.bin").path

        val e = assertFailsWith<IncompleteDownloadException> { http.download("https://x/short.bin", dest) }

        assertEquals(promised, e.expectedBytes)
        assertEquals(delivered.size.toLong(), e.actualBytes)
        assertFalse(File(dest).exists(), "a short file must never appear at the final path")
        assertTrue(File("$dest.tmp").exists(), "partial stays so the next attempt resumes via Range")
    }

    @Test
    fun `non-2xx status writes nothing`() = runTest {
        // Hugging Face answers a missing file with a 15-byte "Entry not found".
        val body = "Entry not found".toByteArray()
        val http = HttpFileDownloader(client { { respond(body, HttpStatusCode.NotFound, headersOf(HttpHeaders.ContentLength, body.size.toString())) } }, config, fs)
        val dest = File(tmp, "missing.onnx").path

        val e = assertFailsWith<HttpDownloadException> { http.download("https://x/missing.onnx", dest) }

        assertEquals(404, e.status)
        assertFalse(File(dest).exists())
        assertFalse(File("$dest.tmp").exists(), "nothing may be written for an error response")
    }

    @Test
    fun `checksum mismatch deletes the file`() = runTest {
        val body = ByteArray(10_000) { 7 }
        val http = HttpFileDownloader(client { { respond(body, HttpStatusCode.OK, headersOf(HttpHeaders.ContentLength, body.size.toString())) } }, config, fs)
        val dest = File(tmp, "bad.bin").path

        assertFailsWith<ChecksumMismatchException> {
            http.download("https://x/bad.bin", dest, expectedSha256 = "0".repeat(64))
        }
        assertFalse(File(dest).exists())
        assertFalse(File("$dest.tmp").exists(), "wrong bytes are not resumable; drop them")
    }

    @Test
    fun `checksum match is accepted case-insensitively`() = runTest {
        val body = ByteArray(10_000) { 9 }
        val http = HttpFileDownloader(client { { respond(body, HttpStatusCode.OK, headersOf(HttpHeaders.ContentLength, body.size.toString())) } }, config, fs)
        val dest = File(tmp, "good.bin").path

        http.download("https://x/good.bin", dest, expectedSha256 = sha256(body).uppercase())

        assertEquals(body.size.toLong(), File(dest).length())
    }
}

/** Minimal java.io-backed FileSystem for unit tests. */
private class JavaFileSystem : FileSystem {
    override fun ensureDirectoryExists(path: String) = File(path).let { it.isDirectory || it.mkdirs() }
    override fun fileExists(path: String) = File(path).exists()
    override fun deleteFile(path: String) = File(path).delete()
    override fun moveFile(from: String, to: String) = File(from).renameTo(File(to))
    override fun fileSize(path: String) = File(path).let { if (it.exists()) it.length() else -1L }
    override fun readText(path: String) = File(path).takeIf { it.exists() }?.readText()
    override fun writeText(path: String, content: String) = runCatching { File(path).writeText(content) }.isSuccess
}
