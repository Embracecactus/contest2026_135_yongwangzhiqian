// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.CharBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets
import java.security.SecureRandom

/** Read-only SPV1 Wi-Fi scan session.  It owns no binding transaction and
 * never sends configuration data.  Calls run on the GATT worker.
 */
internal class WifiScanProtocol(
    bootstrap: ProvisionBootstrap,
    private val send: (ByteArray) -> Unit,
    private val complete: (Result) -> Unit,
    private val nowMs: () -> Long = { System.nanoTime() / 1_000_000 },
) : AutoCloseable {
    data class Network(val ssid: String, val rssi: Int, val channel: Int,
                       val security: Int)
    data class Result(val status: Int, val networks: List<Network>,
                      val truncated: Boolean)

    private val secret = bootstrap.usePossessionSecret { it.copyOf() }
    private val transaction = ByteArray(16).also { SecureRandom().nextBytes(it) }
    private var input = ByteArray(1024)
    private var used = 0
    private var expected = -1
    private var sequence = 0
    private var deadline = 0L
    private var lastNow = nowMs()
    private var started = false
    private var completed = false
    var closed = false
        private set

    fun start() {
        check(!closed && !started && sequence == 0)
        started = true
        try { transmit(1, secret) } finally { secret.fill(0) }
    }

    fun tick() {
        if (closed || !started) return
        val now = nowMs()
        if (now < lastNow || now >= deadline) fail()
        lastNow = now
    }

    fun receive(bytes: ByteArray) {
        check(!closed)
        try {
            tick()
            var offset = 0
            while (offset < bytes.size) {
                val needed = if (expected < 0) 32 else expected
                val count = minOf(needed - used, bytes.size - offset)
                bytes.copyInto(input, used, offset, offset + count)
                used += count
                offset += count
                if (expected < 0 && used == 32) {
                    val header = ByteBuffer.wrap(input, 0, 32).order(ByteOrder.BIG_ENDIAN)
                    require(header.int == MAGIC)
                    val type = header.get().toInt() and 0xff
                    require(header.get() == 0.toByte() && header.get() == 0.toByte() &&
                        header.get() == 0.toByte())
                    require(type == if (sequence == 0) STATUS else SCAN_RESULT)
                    require(header.int == sequence)
                    val receivedTransaction = ByteArray(16).also { header.get(it) }
                    try { require(receivedTransaction.contentEquals(transaction)) }
                    finally { receivedTransaction.fill(0) }
                    val length = header.int
                    require(length in 0..MAX_PAYLOAD)
                    expected = 32 + length
                    if (expected > input.size) input = input.copyOf(expected)
                }
                if (expected >= 0 && used == expected) {
                    response(expected - 32)
                    input.fill(0)
                    used = 0
                    expected = -1
                }
            }
        } catch (_: Exception) {
            fail()
            throw IllegalStateException("Invalid Wi-Fi scan response")
        }
    }

    private fun response(length: Int) {
        val payload = ByteBuffer.wrap(input, 32, length).order(ByteOrder.BIG_ENDIAN)
        if (sequence == 0) {
            require(length == 8)
            val state = payload.int
            val status = payload.int
            require(state == LOCAL || state == READY)
            require(status <= 0)
            if (status < 0) {
                finish(Result(status, emptyList(), false))
                return
            }
            if (state == LOCAL) return
            sequence = 1
            transmit(SCAN, ByteArray(0))
            return
        }

        require(length >= 8)
        val status = payload.int
        val count = payload.get().toInt() and 0xff
        val truncated = payload.get().toInt() and 0xff
        require(truncated in 0..1 && payload.short.toInt() == 0)
        require(status <= 0)
        require(count <= MAX_NETWORKS && length == 8 + count * RECORD_SIZE)
        if (status != 0) require(count == 0)
        val networks = ArrayList<Network>(count)
        repeat(count) {
            val ssidLength = payload.get().toInt() and 0xff
            val rssi = payload.get().toInt()
            val channel = payload.get().toInt() and 0xff
            val security = payload.get().toInt() and 0xff
            val padded = ByteArray(32).also { payload.get(it) }
            try {
                val name = if (ssidLength in 1..32 &&
                    padded.copyOfRange(ssidLength, padded.size).all { it == 0.toByte() }) {
                    val bytes = padded.copyOf(ssidLength)
                    try { decodeSsid(bytes) } finally { bytes.fill(0) }
                } else null
                if (name != null) networks += Network(name, rssi, channel, security)
            } finally { padded.fill(0) }
        }
        finish(Result(status, networks, truncated != 0))
    }

    private fun transmit(type: Int, payload: ByteArray) {
        val frame = ByteBuffer.allocate(32 + payload.size).order(ByteOrder.BIG_ENDIAN)
            .putInt(MAGIC).put(type.toByte()).put(byteArrayOf(0, 0, 0))
            .putInt(sequence).put(transaction).putInt(payload.size).put(payload).array()
        lastNow = nowMs()
        deadline = lastNow + TIMEOUT_MS
        try { send(frame) } finally { frame.fill(0) }
    }

    private fun finish(result: Result) {
        if (!completed && !closed) {
            completed = true
            complete(result)
        }
    }

    private fun fail() {
        if (!closed) finish(Result(-1, emptyList(), false))
        close()
    }

    override fun close() {
        closed = true
        secret.fill(0)
        transaction.fill(0)
        input.fill(0)
    }

    companion object {
        private const val MAGIC = 0x53505631
        private const val STATUS = 128
        private const val SCAN = 6
        private const val SCAN_RESULT = 129
        private const val LOCAL = 2
        private const val READY = 3
        private const val RECORD_SIZE = 36
        private const val MAX_NETWORKS = 24
        private const val MAX_PAYLOAD = 8 + MAX_NETWORKS * RECORD_SIZE
        private const val TIMEOUT_MS = 30_000L

        private fun decodeSsid(bytes: ByteArray): String? = try {
            val value = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(bytes)).toString()
            val roundTrip = StandardCharsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .encode(CharBuffer.wrap(value)).let { encoded ->
                    ByteArray(encoded.remaining()).also { encoded.get(it) }
                }
            try {
                if (!roundTrip.contentEquals(bytes) || value.indexOf('\u0000') >= 0) null else value
            } finally { roundTrip.fill(0) }
        } catch (_: CharacterCodingException) {
            null
        }
    }
}
