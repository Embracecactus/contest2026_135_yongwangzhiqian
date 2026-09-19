// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

/** Host-side structural validation for shaniu-eye-pack-v1 before CONFIG upload. */
internal data class EyePack(val bytes: ByteArray, val revision: Long, val packId: String, val sourceSha256: ByteArray) {
    companion object {
        fun parse(bytes: ByteArray): EyePack? = runCatching {
            require(bytes.size in 128..131072)
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            require(bytes.copyOfRange(0, 8).contentEquals("SHNEYE1\u0000".toByteArray()))
            require(b.getShort(8).toInt() == 1 && b.getShort(10).toInt() == 128)
            require(b.getShort(12).toInt() == 64 && b.getShort(16).toInt() == 160 && b.getShort(18).toInt() == 160)
            val entryCount = b.getShort(14).toInt()
            require(entryCount in 1..64)
            val idBytes = bytes.copyOfRange(48, 80)
            val zero = idBytes.indexOfFirst { it.toInt() == 0 }
            require(zero in 1..31 && idBytes.copyOfRange(zero + 1, idBytes.size).all { it.toInt() == 0 })
            val packId = idBytes.copyOfRange(0, zero).toString(Charsets.US_ASCII)
            require(packId.matches(Regex("[a-z0-9][a-z0-9._-]{0,30}")))
            val source = bytes.copyOfRange(80, 112)
            require(source.any { it.toInt() != 0 })
            require(b.getInt(36) == bytes.size)
            // Validate the payload CRC declared by the fixed v1 header.
            val tocOffset = b.getInt(28); val payloadOffset = b.getInt(32)
            val tocSize = entryCount * 64
            require(tocOffset == 128 && payloadOffset >= tocOffset + tocSize && payloadOffset <= bytes.size)
            val tocCrc = CRC32().apply { update(bytes, tocOffset, tocSize) }
            require(tocCrc.value == b.getInt(40).toLong() and 0xffffffffL)
            val crc = CRC32().apply { update(bytes, payloadOffset, bytes.size - payloadOffset) }
            require(crc.value == b.getInt(44).toLong() and 0xffffffffL)
            EyePack(bytes.copyOf(), b.getInt(24).toLong() and 0xffffffffL, packId, source)
        }.getOrNull()
    }
}
