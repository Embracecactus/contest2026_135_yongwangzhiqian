// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import com.shaniu.companion.provision.ProvisionBootstrap
import java.io.CharArrayReader

/**
 * Owner-delivered console credentials, separate from board provisioning data.
 * The token is retained only as wipeable characters and is never included in a
 * generated string representation.
 */
class ConsoleEnrollment private constructor(
    val deviceId: String,
    val gatewayOrigin: GatewayOrigin,
    val certificatePins: Set<String>,
    private val accessToken: CharArray,
    val expiresAtMs: Long,
) : AutoCloseable {
    private var closed = false

    /** Gives a temporary, wipeable token copy to the importing owner. */
    @Synchronized
    fun <T> useAccessToken(consume: (CharArray) -> T): T {
        check(!closed) { "Console enrollment is closed" }
        val temporary = accessToken.copyOf()
        return try {
            consume(temporary)
        } finally {
            temporary.fill('\u0000')
        }
    }

    @Synchronized
    override fun close() {
        accessToken.fill('\u0000')
        closed = true
    }

    override fun toString(): String = "ConsoleEnrollment(redacted)"

    companion object {
        const val MAX_BYTES = 8192
        private const val PROTOCOL = "shaniu.console-enrollment/1"
        private val fields = setOf(
            "protocol", "device_id", "gateway_origin", "certificate_pins",
            "access_token", "expires_at_ms",
        )

        /**
         * Strictly parses one owner package. [nowMs] is injectable for tests;
         * the Gateway remains the authorization expiry authority at connection.
         */
        fun parse(
            input: CharArray,
            nowMs: Long = System.currentTimeMillis(),
            expectedDeviceId: String? = null,
        ): ConsoleEnrollment {
            var token: CharArray? = null
            try {
                require(input.size in 1..MAX_BYTES)
                val values = mutableMapOf<String, String>()
                var pins: Set<String>? = null
                var expiresAtMs: Long? = null
                JsonReader(CharArrayReader(input)).use { reader ->
                    reader.strictness = Strictness.STRICT
                    reader.beginObject()
                    while (reader.hasNext()) {
                        val name = reader.nextName()
                        require(name in fields && name !in values)
                        when (name) {
                            "certificate_pins" -> {
                                require(pins == null && reader.peek() == JsonToken.BEGIN_ARRAY)
                                val supplied = linkedSetOf<String>()
                                reader.beginArray()
                                while (reader.hasNext()) {
                                    require(reader.peek() == JsonToken.STRING)
                                    val pin = reader.nextString()
                                    require(supplied.add(pin))
                                }
                                reader.endArray()
                                pins = supplied
                            }
                            "expires_at_ms" -> {
                                require(expiresAtMs == null && reader.peek() == JsonToken.NUMBER)
                                expiresAtMs = reader.nextLong()
                            }
                            else -> {
                                require(reader.peek() == JsonToken.STRING)
                                values[name] = reader.nextString()
                            }
                        }
                    }
                    reader.endObject()
                    require(reader.peek() == JsonToken.END_DOCUMENT)
                }
                require(values.keys + "certificate_pins" + "expires_at_ms" == fields)
                require(values["protocol"] == PROTOCOL)
                val deviceId = ProvisionBootstrap.validDeviceId(values["device_id"])
                    ?: throw IllegalArgumentException()
                require(expectedDeviceId == null || deviceId == expectedDeviceId)
                val validPins = checkNotNull(pins)
                require(validPins.isNotEmpty())
                val configuration = GatewayTransportConfiguration.fromExplicit(
                    values["gateway_origin"], validPins,
                ) ?: throw IllegalArgumentException()
                val expiry = checkNotNull(expiresAtMs)
                require(expiry > nowMs)
                token = values.getValue("access_token").toCharArray()
                GatewayTokenPolicy.requireValid(String(token))
                return ConsoleEnrollment(deviceId, configuration.origin, configuration.certificatePins,
                    token, expiry).also { token = null }
            } catch (_: Exception) {
                throw IllegalArgumentException("Invalid console enrollment")
            } finally {
                token?.fill('\u0000')
            }
        }
    }
}
