// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

private const val MAX_FIRMWARE_RELEASES = 32
private const val MAX_FIRMWARE_PACKAGE_BYTES = 64L * 1024L * 1024L

/** Gateway-projected, read-only summary of one verified product release. */
data class FirmwareRelease(
    val manifestSha256: String,
    val targetVersion: String,
    val requiredSourceVersion: String,
    val requiredSourceRootSha256: String,
    val boardFamily: String,
    val physicalBoard: String,
    val layoutIdentity: String,
    val layoutSha256: String,
    val packageSha256: String,
    val packageSizeBytes: Long,
) {
    init {
        require(SHA256.matches(manifestSha256))
        require(FIRMWARE_VERSION.matches(targetVersion))
        require(FIRMWARE_VERSION.matches(requiredSourceVersion))
        require(versionGeneration(requiredSourceVersion) < versionGeneration(targetVersion))
        require(SHA256.matches(requiredSourceRootSha256))
        require(boardFamily == "bk7258")
        require(PHYSICAL_BOARD.matches(physicalBoard))
        require(layoutIdentity.isNotBlank() && layoutIdentity.length <= 128)
        require(SHA256.matches(layoutSha256))
        require(SHA256.matches(packageSha256))
        require(packageSizeBytes in 1..MAX_FIRMWARE_PACKAGE_BYTES)
    }
}

data class FirmwareReleaseCatalog(
    val protocol: String = CONSOLE_V1,
    val deviceId: String,
    val generation: Long,
    val releases: List<FirmwareRelease>,
) {
    init {
        requireDeviceId(deviceId)
        require(generation > 0)
        require(releases.size <= MAX_FIRMWARE_RELEASES)
        require(releases.map { it.manifestSha256 }.distinct().size == releases.size)
        require(releases.map { it.targetVersion }.distinct().size == releases.size)
    }
}

enum class MutationReceiptStatus { ACCEPTED, REJECTED }

/**
 * An accepted receipt means only that Gateway admitted the idempotent request.
 * OTA success is authoritative only after reported-state events reach CONFIRMED.
 */
data class ConsoleMutationReceipt(
    val protocol: String = CONSOLE_V1,
    val requestId: String,
    val deviceId: String,
    val generation: Long,
    val revision: Long,
    val status: MutationReceiptStatus,
    val error: ConsoleErrorCode? = null,
) {
    init {
        requireRequestId(requestId)
        requireDeviceId(deviceId)
        require(generation > 0)
        require(revision >= 0)
        require((status == MutationReceiptStatus.ACCEPTED) == (error == null))
    }
}

enum class ConsoleWireFailure {
    MESSAGE_TOO_LARGE,
    MALFORMED_JSON,
    SCHEMA_MISMATCH,
    UNSUPPORTED_PROTOCOL,
    UNSUPPORTED_TYPE,
}

/** Deliberately excludes the rejected wire body from diagnostics. */
class ConsoleWireException(
    val failure: ConsoleWireFailure,
    message: String,
    cause: Throwable? = null,
) : IllegalArgumentException(message, cause)

internal val SHA256 = Regex("^[0-9a-f]{64}$")
internal val FIRMWARE_VERSION = Regex(
    "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\+([1-9][0-9]*)$",
)
private val DEVICE_ID = Regex("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
private val REQUEST_ID = Regex("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
private val PHYSICAL_BOARD = Regex("^[a-z][a-z0-9_]*$")

internal fun requireDeviceId(value: String) {
    require(DEVICE_ID.matches(value))
}

internal fun requireRequestId(value: String) {
    require(REQUEST_ID.matches(value))
}

private fun versionGeneration(value: String): Long =
    value.substringAfterLast('+').toLong()
