// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.util.Base64

/** Versioned first-use claim code carried by the device's own screen.
 *
 * `SN1:<locator-11>:<leaf-fingerprint-43>:<window-secret-43>` with every binary
 * field unpadded base64url. The locator only finds the device, the fingerprint
 * authenticates the pinned TLS peer, and the secret proves the holder is looking
 * at this claim window. A short or altered field is rejected instead of being
 * padded or truncated.
 */
object ClaimCode {
    const val PREFIX = "SN1:"
    private const val LOCATOR_CHARS = 11
    private const val DIGEST_CHARS = 43
    private const val SECRET_CHARS = 43
    private const val MAX_CHARS = 4 + LOCATOR_CHARS + 1 + DIGEST_CHARS + 1 + SECRET_CHARS

    class Decoded(val locator: ByteArray, val fingerprint: ByteArray, val secret: ByteArray) : AutoCloseable {
        override fun close() {
            locator.fill(0)
            secret.fill(0)
        }
    }

    fun parse(text: String): Decoded {
        require(text.length == MAX_CHARS && text.startsWith(PREFIX)) { "Unsupported claim code" }
        val parts = text.removePrefix(PREFIX).split(':')
        require(parts.size == 3) { "Malformed claim code" }
        require(parts[0].length == LOCATOR_CHARS && parts[1].length == DIGEST_CHARS &&
                parts[2].length == SECRET_CHARS) { "Malformed claim code" }
        val locator = decode(parts[0], 8)
        val fingerprint = decode(parts[1], 32)
        val secret = decode(parts[2], 32)
        return Decoded(locator, fingerprint, secret)
    }

    private fun decode(value: String, expected: Int): ByteArray {
        val bytes = try {
            Base64.decode(value, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)
        } catch (_: IllegalArgumentException) {
            throw IllegalArgumentException("Malformed claim code")
        }
        require(bytes.size == expected) { "Malformed claim code" }
        require(Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP) == value) {
            "Malformed claim code"
        }
        return bytes
    }
}
