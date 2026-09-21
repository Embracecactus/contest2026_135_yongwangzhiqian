// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.StandardCharsets
import java.security.SecureRandom

/** Public SCS1 state and sensitive SCP1 encoder. No credential readback. */
internal object DeviceSettings {
    const val KIND = 7
    const val MAX_PUBLIC = 824
    data class Public(val state: Int, val revision: Long, val operation: String,
        val result: Int, val hasWifi: Boolean, val hasPassword: Boolean,
        val hasCloud: Boolean, val hasKey: Boolean, val port: Int, val dialect: Int,
        val ssid: String, val host: String, val path: String,
        val asr: String, val chat: String, val tts: String) {
        val baseUrl: String get() = if (!hasCloud) CloudSettings.MIMO_TOKEN_PLAN_URL
            else "https://$host${if (port == 443) "" else ":$port"}$path"
    }

    fun decode(bytes: ByteArray): Public {
        require(bytes.size in 56..MAX_PUBLIC)
        val b = ByteBuffer.wrap(bytes)
        require(b.int == 0x53435331)
        val state = b.int.also { require(it in 0..4) }
        val revision = b.long.also { require(it >= 0) }
        val operation = ByteArray(16).also { b.get(it) }.joinToString("") { "%02x".format(it.toInt() and 255) }
        val result = b.int.also { require(it <= 0) }
        val flags = b.int.also { require(it and 15 == it) }
        val port = b.short.toInt() and 65535
        val lengths = IntArray(6) { b.short.toInt() and 65535 }
        val dialect = b.short.toInt() and 65535
        require(lengths[0] <= 32 && lengths.drop(1).all { it <= 127 })
        require(lengths.sum() == b.remaining())
        val fields = lengths.map { n ->
            val data = ByteArray(n).also { b.get(it) }
            require(data.none { it == 0.toByte() })
            StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(data)).toString()
        }
        require(flags and 2 == 0 || flags and 1 != 0)
        require(flags and 8 == 0 || flags and 4 != 0)
        require(if (flags and 4 != 0) port in 1..65535 && dialect in 1..2 && fields.drop(1).all { it.isNotEmpty() }
            else port == 0 && dialect == 0 && fields.drop(1).all { it.isEmpty() })
        return Public(state, revision, operation, result, flags and 1 != 0, flags and 2 != 0,
            flags and 4 != 0, flags and 8 != 0, port, dialect,
            fields[0], fields[1], fields[2], fields[3], fields[4], fields[5])
    }

    fun operation(): ByteArray = ByteArray(16).also { SecureRandom().nextBytes(it) }

    fun patch(current: Public, operation: ByteArray, ssid: String? = null,
              password: CharArray? = null, cloud: ByteArray? = null,
              replaceKey: Boolean = false, endpoint: CloudEndpoint.Verified? = null,
              clearCloud: Boolean = false): ByteArray {
        require(operation.size == 16 && operation.any { it != 0.toByte() })
        require(current.revision < Long.MAX_VALUE)
        val name = ssid?.let { StandardCharsets.UTF_8.newEncoder().encode(CharBuffer.wrap(it)) }
        val secret = password?.let { StandardCharsets.UTF_8.newEncoder().encode(CharBuffer.wrap(it)) }
        try {
            if (ssid != null) {
                if (password != null) ProvisionSettings.inputError(ssid, password)?.let { error(it) }
                else require(ssid == current.ssid && name!!.remaining() in 1..32)
            } else require(password == null)
            require(!replaceKey || cloud != null)
            require(!clearCloud || cloud == null)
            require(endpoint == null || cloud != null)
            val ca = endpoint?.caDer ?: ByteArray(0)
            val address = endpoint?.address ?: ByteArray(4)
            require(ca.size <= 4096 && address.size == 4)
            val flags = (if (ssid != null) 1 else 0) or (if (cloud != null) 2 else 0) or
                (if (replaceKey) 4 else 0) or (if (password != null) 8 else 0) or (if (clearCloud) 16 else 0)
            require(flags != 0)
            val sizes = intArrayOf(name?.remaining() ?: 0, secret?.remaining() ?: 0, cloud?.size ?: 0, ca.size)
            require(52 + sizes.sum() <= 9216)
            return ByteBuffer.allocate(52 + sizes.sum()).apply {
                putInt(0x53435031); put(operation); putLong(current.revision); putInt(flags)
                putLong(System.currentTimeMillis() / 1000)
                sizes.forEach { putShort(it.toShort()) }; put(address)
                name?.let { put(it) }; secret?.let { put(it) }; cloud?.let { put(it) }; put(ca)
            }.array()
        } finally {
            if (name?.hasArray() == true) name.array().fill(0)
            if (secret?.hasArray() == true) secret.array().fill(0)
        }
    }
}
