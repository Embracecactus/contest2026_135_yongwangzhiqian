// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.ConsoleEvent
import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.ConsoleMutation
import com.shaniu.companion.protocol.ConsoleMutationReceipt
import com.shaniu.companion.protocol.ConsoleWireException
import com.shaniu.companion.protocol.ConsoleWireV1
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.requireDeviceId
import java.io.IOException
import java.net.URI

class GatewayOrigin private constructor(val uri: URI) {
    fun resolve(relativePath: String): URI {
        require(!relativePath.startsWith('/'))
        return uri.resolve(relativePath)
    }

    fun resolveEventStream(relativePath: String): URI {
        val https = resolve(relativePath)
        return URI(
            "wss",
            null,
            https.host,
            https.port,
            https.path,
            https.query,
            null,
        )
    }

    internal fun owns(target: URI, expectedScheme: String): Boolean =
        target.scheme == expectedScheme &&
            target.userInfo == null &&
            target.fragment == null &&
            target.host.equals(uri.host, ignoreCase = true) &&
            effectivePort(target) == effectivePort(uri)

    override fun toString(): String = uri.toString()

    companion object {
        fun parse(value: String): GatewayOrigin {
            val parsed = try {
                URI(value)
            } catch (error: Exception) {
                throw IllegalArgumentException("Gateway origin is not a URI", error)
            }
            require(parsed.scheme == "https") { "Gateway origin must use HTTPS" }
            require(!parsed.host.isNullOrBlank()) { "Gateway origin must include a host" }
            require(parsed.userInfo == null) { "Gateway origin must not contain user info" }
            require(parsed.query == null && parsed.fragment == null)
            require(parsed.path.isNullOrEmpty() || parsed.path == "/") {
                "Gateway origin must not contain a path"
            }
            require(parsed.port == -1 || parsed.port in 1..65535)
            val normalized = URI(
                "https",
                null,
                parsed.host,
                parsed.port,
                "/",
                null,
                null,
            )
            return GatewayOrigin(normalized)
        }

        private fun effectivePort(value: URI): Int = when {
            value.port != -1 -> value.port
            value.scheme == "https" || value.scheme == "wss" -> 443
            else -> -1
        }
    }
}

enum class GatewayHttpMethod { GET, POST }

data class GatewayHttpRequest(
    val method: GatewayHttpMethod,
    val uri: URI,
    val headers: Map<String, String>,
    val body: String? = null,
) {
    init {
        require(uri.scheme == "https")
        require((method == GatewayHttpMethod.POST) == (body != null))
        require(headers.keys.none { name ->
            RESERVED_HEADERS.any { reserved -> name.equals(reserved, ignoreCase = true) }
        }) {
            "routing and credential headers belong to the authenticated transport"
        }
        require(headers.keys.none { it.isBlank() })
        require(headers.values.none { '\r' in it || '\n' in it })
    }

    companion object {
        private val RESERVED_HEADERS = setOf(
            "Authorization",
            "Cookie",
            "Host",
            "Proxy-Authorization",
        )
    }
}

data class GatewayHttpResponse(val statusCode: Int, val body: String) {
    init {
        require(statusCode in 100..599)
    }
}

/**
 * Authenticated HTTPS transport boundary. Implementations own the Android
 * Keystore-backed token handle and TLS policy; callers never pass or log a
 * bearer token. Implementations must not follow redirects to another origin.
 */
fun interface ConsoleGatewayTransport {
    @Throws(IOException::class)
    fun execute(request: GatewayHttpRequest): GatewayHttpResponse
}

enum class GatewayFailureReason {
    CREDENTIALS_UNAVAILABLE,
    TRANSPORT_UNAVAILABLE,
    AUTHORIZATION_REVOKED,
    DEVICE_NOT_FOUND,
    REVISION_CONFLICT,
    RATE_LIMITED,
    SERVER_ERROR,
    REQUEST_REJECTED,
    PROTOCOL_ERROR,
}

sealed interface GatewayCallResult<out T> {
    data class Success<T>(val value: T) : GatewayCallResult<T>

    data class Failure(
        val reason: GatewayFailureReason,
        val retryable: Boolean,
        val httpStatus: Int? = null,
    ) : GatewayCallResult<Nothing>
}

/**
 * console-v1 request builder and response verifier.
 *
 * This class is synchronous by design and must be called from a worker/coroutine.
 * It never retries mutations automatically; request_id provides the server-side
 * idempotency key for a caller-controlled retry after state reconciliation.
 */
class ConsoleGatewayClient(
    private val origin: GatewayOrigin,
    private val transport: ConsoleGatewayTransport,
) {
    fun fetchSnapshot(deviceId: String): GatewayCallResult<ConsoleEventEnvelope> {
        val response = when (val result = execute(
            GatewayHttpRequest(
                method = GatewayHttpMethod.GET,
                uri = origin.resolve("console/v1/devices/${deviceSegment(deviceId)}/snapshot"),
                headers = readHeaders(),
            ),
        )) {
            is GatewayCallResult.Success -> result.value
            is GatewayCallResult.Failure -> return result
        }
        if (response.statusCode != 200) return httpFailure(response.statusCode)
        return decodeResult {
            ConsoleWireV1.decodeEvent(response.body).also { envelope ->
                require(envelope.deviceId == deviceId)
                require(envelope.event is ConsoleEvent.StateSnapshot)
            }
        }
    }

    fun fetchFirmwareReleases(
        deviceId: String,
        generation: Long,
    ): GatewayCallResult<FirmwareReleaseCatalog> {
        require(generation > 0)
        val response = when (val result = execute(
            GatewayHttpRequest(
                method = GatewayHttpMethod.GET,
                uri = origin.resolve(
                    "console/v1/devices/${deviceSegment(deviceId)}/firmware/releases" +
                        "?generation=$generation",
                ),
                headers = readHeaders(),
            ),
        )) {
            is GatewayCallResult.Success -> result.value
            is GatewayCallResult.Failure -> return result
        }
        if (response.statusCode != 200) return httpFailure(response.statusCode)
        return decodeResult {
            ConsoleWireV1.decodeReleaseCatalog(response.body).also { catalog ->
                require(catalog.deviceId == deviceId)
                require(catalog.generation == generation)
            }
        }
    }

    fun submitMutation(mutation: ConsoleMutation): GatewayCallResult<ConsoleMutationReceipt> {
        val response = when (val result = execute(
            GatewayHttpRequest(
                method = GatewayHttpMethod.POST,
                uri = origin.resolve(
                    "console/v1/devices/${deviceSegment(mutation.deviceId)}/mutations",
                ),
                headers = writeHeaders(),
                body = ConsoleWireV1.encodeMutation(mutation),
            ),
        )) {
            is GatewayCallResult.Success -> result.value
            is GatewayCallResult.Failure -> return result
        }
        if (response.statusCode !in setOf(200, 202)) return httpFailure(response.statusCode)
        return decodeResult {
            ConsoleWireV1.decodeReceipt(response.body).also { receipt ->
                require(receipt.requestId == mutation.requestId)
                require(receipt.deviceId == mutation.deviceId)
                require(receipt.generation == mutation.generation)
            }
        }
    }

    fun eventStreamUri(deviceId: String, generation: Long, afterSequence: Long): URI {
        require(generation > 0)
        require(afterSequence >= 0)
        return origin.resolveEventStream(
            "console/v1/devices/${deviceSegment(deviceId)}/events" +
                "?generation=$generation&after_sequence=$afterSequence",
        )
    }

    private fun execute(request: GatewayHttpRequest): GatewayCallResult<GatewayHttpResponse> = try {
        GatewayCallResult.Success(transport.execute(request))
    } catch (error: GatewayTransportException) {
        GatewayCallResult.Failure(error.reason, error.retryable)
    } catch (_: IOException) {
        GatewayCallResult.Failure(
            GatewayFailureReason.TRANSPORT_UNAVAILABLE,
            retryable = true,
        )
    }

    private inline fun <T> decodeResult(block: () -> T): GatewayCallResult<T> = try {
        GatewayCallResult.Success(block())
    } catch (_: ConsoleWireException) {
        protocolFailure()
    } catch (_: IllegalArgumentException) {
        protocolFailure()
    }

    private fun deviceSegment(deviceId: String): String {
        requireDeviceId(deviceId)
        return deviceId
    }

    private fun readHeaders(): Map<String, String> = linkedMapOf(
        "Accept" to MEDIA_TYPE,
        "Cache-Control" to "no-store",
        "X-Console-Protocol" to "console-v1",
    )

    private fun writeHeaders(): Map<String, String> = readHeaders() +
        ("Content-Type" to MEDIA_TYPE)

    private fun httpFailure(statusCode: Int): GatewayCallResult.Failure = when (statusCode) {
        401, 403 -> GatewayCallResult.Failure(
            GatewayFailureReason.AUTHORIZATION_REVOKED,
            retryable = false,
            httpStatus = statusCode,
        )
        404 -> GatewayCallResult.Failure(
            GatewayFailureReason.DEVICE_NOT_FOUND,
            retryable = false,
            httpStatus = statusCode,
        )
        409, 412 -> GatewayCallResult.Failure(
            GatewayFailureReason.REVISION_CONFLICT,
            retryable = false,
            httpStatus = statusCode,
        )
        429 -> GatewayCallResult.Failure(
            GatewayFailureReason.RATE_LIMITED,
            retryable = true,
            httpStatus = statusCode,
        )
        426 -> GatewayCallResult.Failure(
            GatewayFailureReason.PROTOCOL_ERROR,
            retryable = false,
            httpStatus = statusCode,
        )
        in 500..599 -> GatewayCallResult.Failure(
            GatewayFailureReason.SERVER_ERROR,
            retryable = true,
            httpStatus = statusCode,
        )
        else -> GatewayCallResult.Failure(
            GatewayFailureReason.REQUEST_REJECTED,
            retryable = false,
            httpStatus = statusCode,
        )
    }

    private fun protocolFailure() = GatewayCallResult.Failure(
        GatewayFailureReason.PROTOCOL_ERROR,
        retryable = false,
    )

    companion object {
        const val MEDIA_TYPE = "application/vnd.shaniu.console-v1+json"
    }
}
