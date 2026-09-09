package dev.deviceai.models

import java.io.File
import java.io.FileOutputStream

actual fun appendToFile(path: String, data: ByteArray, length: Int) {
    FileOutputStream(File(path), true).use { fos ->
        fos.write(data, 0, length)
    }
}

actual fun sha256File(path: String): String? {
    val file = File(path)
    if (!file.isFile) return null
    val digest = java.security.MessageDigest.getInstance("SHA-256")
    val buffer = ByteArray(64 * 1024)
    java.io.FileInputStream(file).use { input ->
        while (true) {
            val n = input.read(buffer)
            if (n <= 0) break
            digest.update(buffer, 0, n)
        }
    }
    return digest.digest().joinToString("") { "%02x".format(it) }
}
