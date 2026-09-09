package dev.deviceai.models

/** Platform-specific file append used during streaming downloads. */
expect fun appendToFile(path: String, data: ByteArray, length: Int)

/**
 * Lowercase hex SHA-256 of the file at [path], computed by streaming so a
 * multi-hundred-MB model never has to fit in memory. Returns null if the
 * file cannot be read.
 */
expect fun sha256File(path: String): String?
