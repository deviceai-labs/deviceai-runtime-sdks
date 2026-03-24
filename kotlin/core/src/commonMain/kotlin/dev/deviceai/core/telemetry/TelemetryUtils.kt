package dev.deviceai.core.telemetry

import dev.deviceai.models.currentTimeMillis

// ── FNV-1a 32-bit hash ────────────────────────────────────────────────────────
// Deterministic across all Kotlin platforms. Used to anonymise install IDs.

internal fun fnv1a32(input: String): String {
    var hash = 2166136261L                           // FNV offset basis
    for (ch in input) {
        hash = (hash xor ch.code.toLong()) * 16777619L and 0xFFFFFFFFL  // FNV prime
    }
    return hash.toString(16).padStart(8, '0')
}

// ── Month key from epoch ms ───────────────────────────────────────────────────
// Returns "YYYY-MM" without kotlinx-datetime dependency.

internal fun currentMonthKey(): String = monthKeyFromMs(currentTimeMillis())

internal fun monthKeyFromMs(ms: Long): String {
    val days = ms / 86_400_000L
    var year = 1970
    var remaining = days
    while (true) {
        val daysInYear = if (isLeapYear(year)) 366L else 365L
        if (remaining < daysInYear) break
        remaining -= daysInYear
        year++
    }
    val monthLengths = intArrayOf(
        31, if (isLeapYear(year)) 29 else 28,
        31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    )
    var month = 1
    for (d in monthLengths) {
        if (remaining < d) break
        remaining -= d
        month++
    }
    return "$year-${month.toString().padStart(2, '0')}"
}

private fun isLeapYear(y: Int) = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)
