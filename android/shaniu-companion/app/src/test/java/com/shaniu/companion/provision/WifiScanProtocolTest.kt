// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class WifiScanProtocolTest {
    @Test fun authenticatedControlScanUsesDeviceResultsAndRejectsMalformedRecords() {
        val wire = ByteBuffer.allocate(48).put("WFS1".toByteArray())
            .put(1.toByte()).put(0.toByte()).put(ByteArray(6))
            .put(record("附近网络")).array()
        val decoded = WifiScanProtocol.decodeControl(wire)
        assertEquals("附近网络", decoded.networks.single().ssid)
        assertEquals(-60, decoded.networks.single().rssi)
        assertFalse(decoded.truncated)
        assertThrows(IllegalArgumentException::class.java) {
            WifiScanProtocol.decodeControl(wire.copyOf(47))
        }
        assertThrows(IllegalArgumentException::class.java) {
            WifiScanProtocol.decodeControl(wire.copyOf().also { it[12] = 33 })
        }
        assertThrows(IllegalArgumentException::class.java) {
            WifiScanProtocol.decodeControl(wire.copyOf().also { it[6] = 1 })
        }
    }

    private fun bootstrap() = ProvisionBootstrap.parse(("""{"protocol":"provision-bootstrap-v1","device_id":"scan-device","certificate_sha256":"${"ab".repeat(32)}","possession_secret":"${Base64.getEncoder().encodeToString(ByteArray(32) { 7 })}"}""").toCharArray())

    private fun frame(type: Int, sequence: Int, transaction: ByteArray, payload: ByteArray): ByteArray =
        ByteBuffer.allocate(32 + payload.size).putInt(0x53505631).put(type.toByte())
            .put(byteArrayOf(0, 0, 0)).putInt(sequence).put(transaction)
            .putInt(payload.size).put(payload).array()

    private fun transaction(request: ByteArray) = request.copyOfRange(12, 28)

    private fun status(request: ByteArray, state: Int = 3, result: Int = 0) =
        frame(128, 0, transaction(request), ByteBuffer.allocate(8).putInt(state).putInt(result).array())

    private fun scan(request: ByteArray, records: List<ByteArray>, truncated: Int = 0,
                     status: Int = 0): ByteArray {
        val payload = ByteBuffer.allocate(8 + records.sumOf { it.size })
            .putInt(status).put(records.size.toByte()).put(truncated.toByte()).putShort(0)
        records.forEach { payload.put(it) }
        return frame(129, 1, transaction(request), payload.array())
    }

    private fun record(name: String, rssi: Int = -60, channel: Int = 6, security: Int = 2): ByteArray {
        val bytes = name.toByteArray(Charsets.UTF_8)
        require(bytes.size in 1..32)
        return ByteBuffer.allocate(36).put(bytes.size.toByte()).put(rssi.toByte())
            .put(channel.toByte()).put(security.toByte()).put(bytes).put(ByteArray(32 - bytes.size)).array()
    }

    @Test fun scanResponseAcceptsFragmentsAndPreservesUtf8Bytes() {
        val sent = mutableListOf<ByteArray>()
        val results = mutableListOf<WifiScanProtocol.Result>()
        val protocol = WifiScanProtocol(bootstrap(), { sent += it.copyOf() }, { results += it })
        protocol.tick() // TLS has not established, so no scan deadline exists yet.
        protocol.start()
        val local = status(sent.last(), state = 2)
        local.toList().chunked(3).forEach { protocol.receive(it.toByteArray()) }
        assertEquals(1, sent.size)
        assertTrue(results.isEmpty())
        val ready = status(sent.last())
        ready.toList().chunked(3).forEach { protocol.receive(it.toByteArray()) }
        assertEquals(6, sent.last()[4].toInt() and 0xff)
        val reply = scan(sent.last(), listOf(record("咖啡馆 Wi-Fi"), record("lab")), truncated = 1)
        reply.toList().chunked(7).forEach { protocol.receive(it.toByteArray()) }
        assertEquals(1, results.size)
        assertEquals(0, results.single().status)
        assertTrue(results.single().truncated)
        assertEquals("咖啡馆 Wi-Fi", results.single().networks.first().ssid)
        assertEquals(-60, results.single().networks.first().rssi)
        protocol.close()
    }

    @Test fun wrongSequenceAndOverLimitReplyFailClosed() {
        fun scanning(): Pair<WifiScanProtocol, MutableList<ByteArray>> {
            val sent = mutableListOf<ByteArray>()
            val protocol = WifiScanProtocol(bootstrap(), { sent += it.copyOf() }, { })
            protocol.start(); protocol.receive(status(sent.last()))
            return protocol to sent
        }
        val (wrongSequence, sent) = scanning()
        val mismatch = scan(sent.last(), emptyList()).also { ByteBuffer.wrap(it).putInt(8, 2) }
        assertThrows(IllegalStateException::class.java) { wrongSequence.receive(mismatch) }
        assertTrue(wrongSequence.closed)

        val (overLimit, overLimitSent) = scanning()
        val records = List(25) { record("n$it") }
        assertThrows(IllegalStateException::class.java) { overLimit.receive(scan(overLimitSent.last(), records)) }
        assertTrue(overLimit.closed)
    }

    @Test fun cancelSuppressesLateResult() {
        val sent = mutableListOf<ByteArray>()
        val results = mutableListOf<WifiScanProtocol.Result>()
        val protocol = WifiScanProtocol(bootstrap(), { sent += it.copyOf() }, { results += it })
        protocol.start(); protocol.receive(status(sent.last()))
        val late = scan(sent.last(), listOf(record("late")))
        protocol.close()
        assertThrows(IllegalStateException::class.java) { protocol.receive(late) }
        assertTrue(results.isEmpty())
    }
}
