// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.ConsoleWireException
import com.shaniu.companion.protocol.ConsoleWireV1
import com.shaniu.companion.protocol.requireDeviceId
import okhttp3.CertificatePinner
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import okhttp3.ResponseBody
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import java.io.Closeable
import java.io.IOException
import java.net.URI
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

interface GatewayEventObserver {
    /** Callbacks run on the OkHttp dispatcher and must be non-blocking and non-throwing. */
    fun onOpen()

    fun onEvent(event: ConsoleEventEnvelope)

    fun onClosed()

    fun onFailure(failure: GatewayCallResult.Failure)
}

fun interface GatewayEventConnection {
    fun close(): Boolean
}

/**
 * Concrete HTTPS/WSS console-v1 session. It uses the platform trust manager,
 * optional explicit SPKI pins, no redirects, no transparent retries, and no
 * logging interceptor. A token is required before any socket is opened.
 */
class OkHttpConsoleGatewaySession internal constructor(
    private val configuration: GatewayTransportConfiguration,
    private val tokenProvider: GatewayAccessTokenProvider,
    private val client: OkHttpClient,
) : ConsoleGatewayTransport, Closeable {
    constructor(
        configuration: GatewayTransportConfiguration,
        tokenProvider: GatewayAccessTokenProvider,
    ) : this(configuration, tokenProvider, GatewayOkHttpClientFactory.create(configuration))

    override fun execute(request: GatewayHttpRequest): GatewayHttpResponse {
        requireOwnedHttps(request.uri)
        val token = requireAccessToken()
        val body = request.body?.toRequestBody(
            request.headers["Content-Type"]?.toMediaTypeOrNull(),
        )
        val builder = Request.Builder().url(request.uri.toString())
        request.headers.forEach { (name, value) -> builder.header(name, value) }
        builder.header(AUTHORIZATION, "Bearer $token")
        when (request.method) {
            GatewayHttpMethod.GET -> builder.get()
            GatewayHttpMethod.POST -> builder.post(requireNotNull(body))
        }

        return client.newCall(builder.build()).execute().use { response ->
            GatewayHttpResponse(
                statusCode = response.code,
                body = response.body?.readBoundedUtf8().orEmpty(),
            )
        }
    }

    /** Opens one event stream. Reconnect and cursor reconciliation belong to the caller. */
    fun openEventStream(
        deviceId: String,
        generation: Long,
        afterSequence: Long,
        observer: GatewayEventObserver,
    ): GatewayEventConnection {
        requireDeviceId(deviceId)
        require(generation > 0)
        require(afterSequence >= 0)
        val token = requireAccessToken()
        val uri = configuration.origin.resolveEventStream(
            "console/v1/devices/$deviceId/events" +
                "?generation=$generation&after_sequence=$afterSequence",
        )
        requireOwnedWss(uri)
        val request = Request.Builder()
            .url(uri.toString())
            .header(AUTHORIZATION, "Bearer $token")
            .header("Cache-Control", "no-store")
            .header("X-Console-Protocol", "console-v1")
            .build()
        val listener = ConsoleWebSocketListener(deviceId, generation, observer)
        val webSocket = client.newWebSocket(request, listener)
        return GatewayEventConnection { webSocket.close(NORMAL_CLOSE, null) }
    }

    override fun close() {
        client.dispatcher.executorService.shutdown()
        client.connectionPool.evictAll()
        client.cache?.close()
    }

    private fun requireAccessToken(): String {
        val token = try {
            tokenProvider.readAccessToken()
        } catch (error: IOException) {
            throw GatewayTransportException(
                GatewayFailureReason.CREDENTIALS_UNAVAILABLE,
                retryable = false,
                cause = error,
            )
        }
        if (token == null) {
            throw GatewayTransportException(
                GatewayFailureReason.CREDENTIALS_UNAVAILABLE,
                retryable = false,
            )
        }
        return try {
            GatewayTokenPolicy.requireValid(token)
        } catch (error: IllegalArgumentException) {
            throw GatewayTransportException(
                GatewayFailureReason.CREDENTIALS_UNAVAILABLE,
                retryable = false,
                cause = error,
            )
        }
    }

    private fun requireOwnedHttps(target: URI) {
        if (!configuration.origin.owns(target, "https")) {
            throw GatewayTransportException(
                GatewayFailureReason.PROTOCOL_ERROR,
                retryable = false,
            )
        }
    }

    private fun requireOwnedWss(target: URI) {
        if (!configuration.origin.owns(target, "wss")) {
            throw GatewayTransportException(
                GatewayFailureReason.PROTOCOL_ERROR,
                retryable = false,
            )
        }
    }

    private class ConsoleWebSocketListener(
        private val expectedDeviceId: String,
        private val expectedGeneration: Long,
        private val observer: GatewayEventObserver,
    ) : WebSocketListener() {
        private val terminal = AtomicBoolean(false)

        override fun onOpen(webSocket: WebSocket, response: Response) {
            observer.onOpen()
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            if (terminal.get()) return
            val event = try {
                decodeEvent(text)
            } catch (_: ConsoleWireException) {
                protocolFailure(webSocket)
                return
            } catch (_: IllegalArgumentException) {
                protocolFailure(webSocket)
                return
            }
            observer.onEvent(event)
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
            protocolFailure(webSocket, UNSUPPORTED_DATA_CLOSE)
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            webSocket.close(code, null)
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            if (!terminal.compareAndSet(false, true)) return
            if (code == NORMAL_CLOSE) {
                observer.onClosed()
            } else {
                observer.onFailure(failureForCloseCode(code))
            }
        }

        override fun onFailure(webSocket: WebSocket, error: Throwable, response: Response?) {
            response?.close()
            if (!terminal.compareAndSet(false, true)) return
            observer.onFailure(failureForStatus(response?.code))
        }

        private fun decodeEvent(text: String): ConsoleEventEnvelope {
            if (text.length > ConsoleWireV1.MAX_MESSAGE_BYTES) {
                throw IllegalArgumentException("Event message is too large")
            }
            val encoded = text.toByteArray(StandardCharsets.UTF_8)
            try {
                require(encoded.size <= ConsoleWireV1.MAX_MESSAGE_BYTES)
            } finally {
                encoded.fill(0)
            }
            return ConsoleWireV1.decodeEvent(text).also { event ->
                require(event.deviceId == expectedDeviceId)
                require(event.generation == expectedGeneration)
            }
        }

        private fun protocolFailure(webSocket: WebSocket, closeCode: Int = INVALID_DATA_CLOSE) {
            webSocket.close(closeCode, null)
            if (terminal.compareAndSet(false, true)) {
                observer.onFailure(
                    GatewayCallResult.Failure(
                        reason = GatewayFailureReason.PROTOCOL_ERROR,
                        retryable = false,
                    ),
                )
            }
        }
    }

    companion object {
        private const val AUTHORIZATION = "Authorization"
        private const val NORMAL_CLOSE = 1000
        private const val UNSUPPORTED_DATA_CLOSE = 1003
        private const val INVALID_DATA_CLOSE = 1007

        private fun failureForStatus(status: Int?): GatewayCallResult.Failure = when (status) {
            401, 403 -> GatewayCallResult.Failure(
                GatewayFailureReason.AUTHORIZATION_REVOKED,
                retryable = false,
                httpStatus = status,
            )
            426 -> GatewayCallResult.Failure(
                GatewayFailureReason.PROTOCOL_ERROR,
                retryable = false,
                httpStatus = status,
            )
            in 500..599 -> GatewayCallResult.Failure(
                GatewayFailureReason.SERVER_ERROR,
                retryable = true,
                httpStatus = status,
            )
            null -> GatewayCallResult.Failure(
                GatewayFailureReason.TRANSPORT_UNAVAILABLE,
                retryable = true,
            )
            else -> GatewayCallResult.Failure(
                GatewayFailureReason.REQUEST_REJECTED,
                retryable = false,
                httpStatus = status,
            )
        }

        private fun failureForCloseCode(code: Int): GatewayCallResult.Failure = when (code) {
            1001, 1012, 1013 -> GatewayCallResult.Failure(
                GatewayFailureReason.TRANSPORT_UNAVAILABLE,
                retryable = true,
            )
            1011 -> GatewayCallResult.Failure(
                GatewayFailureReason.SERVER_ERROR,
                retryable = true,
            )
            1008 -> GatewayCallResult.Failure(
                GatewayFailureReason.REQUEST_REJECTED,
                retryable = false,
            )
            else -> GatewayCallResult.Failure(
                GatewayFailureReason.PROTOCOL_ERROR,
                retryable = false,
            )
        }
    }
}

internal object GatewayOkHttpClientFactory {
    fun create(configuration: GatewayTransportConfiguration): OkHttpClient {
        val builder = OkHttpClient.Builder()
            .followRedirects(false)
            .followSslRedirects(false)
            .retryOnConnectionFailure(false)
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .writeTimeout(20, TimeUnit.SECONDS)
            .callTimeout(30, TimeUnit.SECONDS)

        if (configuration.certificatePinningEnabled) {
            val pinner = CertificatePinner.Builder().apply {
                configuration.certificatePins.forEach { pin ->
                    add(configuration.origin.uri.host, pin)
                }
            }.build()
            builder.certificatePinner(pinner)
        }
        return builder.build()
    }
}

private fun ResponseBody.readBoundedUtf8(): String {
    if (contentLength() > ConsoleWireV1.MAX_MESSAGE_BYTES) {
        throw GatewayTransportException(GatewayFailureReason.PROTOCOL_ERROR, retryable = false)
    }
    val source = source()
    source.request(ConsoleWireV1.MAX_MESSAGE_BYTES.toLong() + 1)
    if (source.buffer.size > ConsoleWireV1.MAX_MESSAGE_BYTES) {
        throw GatewayTransportException(GatewayFailureReason.PROTOCOL_ERROR, retryable = false)
    }
    val bytes = source.readByteArray()
    return try {
        StandardCharsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes))
            .toString()
    } catch (error: Exception) {
        throw GatewayTransportException(
            GatewayFailureReason.PROTOCOL_ERROR,
            retryable = false,
            cause = error,
        )
    } finally {
        bytes.fill(0)
    }
}
