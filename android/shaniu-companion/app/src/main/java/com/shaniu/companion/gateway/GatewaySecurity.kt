// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import java.io.IOException
import java.util.Base64

/** Reads a short-lived bearer credential without exposing persistence details. */
fun interface GatewayAccessTokenProvider {
    @Throws(IOException::class)
    fun readAccessToken(): String?
}

/** Provisioning-only extension implemented by the Android Keystore store. */
interface GatewayAccessTokenStore : GatewayAccessTokenProvider {
    @Throws(IOException::class)
    fun storeAccessToken(accessToken: String)

    @Throws(IOException::class)
    fun clearAccessToken()
}

/** RFC 6750 b64token subset used to keep credentials out of malformed headers. */
object GatewayTokenPolicy {
    const val MIN_TOKEN_CHARS = 16
    const val MAX_TOKEN_CHARS = 4096

    private val TOKEN_PATTERN = Regex("[A-Za-z0-9\\-._~+/]+=*")

    fun requireValid(accessToken: String): String {
        require(accessToken.length in MIN_TOKEN_CHARS..MAX_TOKEN_CHARS) {
            "Gateway access token length is invalid"
        }
        require(TOKEN_PATTERN.matches(accessToken)) {
            "Gateway access token contains invalid characters"
        }
        return accessToken
    }
}

/**
 * Explicit production transport input. A blank origin disables construction;
 * there is deliberately no baked-in development or production endpoint.
 */
class GatewayTransportConfiguration(
    val origin: GatewayOrigin,
    certificatePins: Set<String> = emptySet(),
) {
    val certificatePins: Set<String> = certificatePins.toSortedSet()
    val certificatePinningEnabled: Boolean get() = certificatePins.isNotEmpty()

    init {
        require(certificatePins.size <= MAX_CERTIFICATE_PINS) {
            "Too many Gateway certificate pins"
        }
        certificatePins.forEach(::requireCertificatePin)
    }

    companion object {
        const val MAX_CERTIFICATE_PINS = 8

        fun fromExplicit(
            originValue: String?,
            certificatePins: Set<String> = emptySet(),
        ): GatewayTransportConfiguration? {
            if (originValue.isNullOrBlank()) return null
            require(originValue == originValue.trim()) {
                "Gateway origin must not contain surrounding whitespace"
            }
            return GatewayTransportConfiguration(
                origin = GatewayOrigin.parse(originValue),
                certificatePins = certificatePins,
            )
        }

        private fun requireCertificatePin(pin: String) {
            require(pin.startsWith(SHA256_PREFIX)) {
                "Gateway certificate pin must use SHA-256"
            }
            val encodedDigest = pin.removePrefix(SHA256_PREFIX)
            val digest = try {
                Base64.getDecoder().decode(encodedDigest)
            } catch (error: IllegalArgumentException) {
                throw IllegalArgumentException("Gateway certificate pin is not base64", error)
            }
            try {
                require(digest.size == SHA256_BYTES) {
                    "Gateway certificate pin digest length is invalid"
                }
                require(Base64.getEncoder().encodeToString(digest) == encodedDigest) {
                    "Gateway certificate pin must use canonical base64"
                }
            } finally {
                digest.fill(0)
            }
        }

        private const val SHA256_PREFIX = "sha256/"
        private const val SHA256_BYTES = 32
    }
}

/** A content-free transport failure safe to surface through console-v1. */
class GatewayTransportException(
    val reason: GatewayFailureReason,
    val retryable: Boolean,
    cause: Throwable? = null,
) : IOException(reason.name, cause)
