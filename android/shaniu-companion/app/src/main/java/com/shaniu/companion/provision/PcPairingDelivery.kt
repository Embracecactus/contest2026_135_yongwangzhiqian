// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.MessageDigest
import java.security.SecureRandom

/** Retains encrypted delivery and public receipt metadata, never a persisted key.
 * The authorization controller remains the sole protocol state machine.
 */
internal class PcPairingDelivery private constructor(
    val createdAt: Long, val expiresAt: Long, val target: PcAuthorizationController.Target,
    val transaction: String, val certificatePin: String, private val encrypted: ByteArray,
) {
    fun response(state: PcAuthorizationController.State, identity: ProvisionPeerIdentity?, now: Long): ByteArray? {
        val view = state.snapshot ?: return null
        return if (!state.busy && state.outcome == PcAuthorizationController.Outcome.CONFIRMED &&
            state.transaction == transaction && state.target == target && view.active &&
            view.transaction == transaction && view.client == target.client &&
            view.grantRevision == target.revision && view.capabilities == target.capabilities &&
            identity?.sha256 == certificatePin && now >= createdAt && now < expiresAt) encrypted.copyOf() else null
    }
    fun encode(): ByteArray = ByteBuffer.allocate(100 + encrypted.size).putInt(0x53504431)
        .putLong(createdAt).putLong(expiresAt).putLong(target.revision.toLong()).putInt(target.capabilities)
        .put(unhex(target.client)).put(unhex(transaction)).put(unhex(certificatePin))
        .putInt(encrypted.size).put(encrypted).array()

    class Prepared internal constructor(val delivery: PcPairingDelivery,
        private val expected: PcAuthorizationController.Snapshot, private val key: ByteArray) : AutoCloseable {
        private var active = true
        /** Save must finish durably before any grant command. Failure never retries. */
        fun submit(controller: PcAuthorizationController, now: Long, save: (PcPairingDelivery) -> Boolean): Boolean {
            if (!active) return false
            active = false
            return try {
                if (now < delivery.createdAt || now >= delivery.expiresAt || !save(delivery)) false else controller.grant(expected, unhex(delivery.target.client), key,
                    delivery.target.capabilities, unhex(delivery.transaction))
            } finally { key.fill(0) }
        }
        override fun close() { active = false; key.fill(0) }
    }
    companion object {
        private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
        private fun unhex(text: String) = text.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        fun prepare(request: PcPairingExchange.Request, identity: ProvisionPeerIdentity,
                    expected: PcAuthorizationController.Snapshot, now: Long): Prepared {
            require(expected.grantRevision != ULong.MAX_VALUE)
            val key = ByteArray(32).also { SecureRandom().nextBytes(it) }
            try {
                val response = request.encrypt(identity, key, now)
                val delivery = PcPairingDelivery(request.createdAtMs, request.expiresAtMs,
                    PcAuthorizationController.Target(hex(request.clientId()), request.capabilities, expected.grantRevision + 1u),
                    hex(request.transactionId()), identity.sha256, response)
                return Prepared(delivery, expected, key)
            } catch (error: Exception) { key.fill(0); throw error }
        }
        fun decode(input: ByteArray): PcPairingDelivery {
            try {
                require(input.size in 525..8716)
                val b = ByteBuffer.wrap(input)
                require(b.int == 0x53504431)
                val created = b.long; val expires = b.long; val revision = b.long.toULong(); val caps = b.int
                val client = ByteArray(16).also { b.get(it) }; val tx = ByteArray(16).also { b.get(it) }
                val pin = ByteArray(32).also { b.get(it) }; val size = b.int
                require(created >= 0 && created <= Long.MAX_VALUE - PcPairingExchange.TTL_MS &&
                    expires == created + PcPairingExchange.TTL_MS && revision > 0u && caps in 1..31 &&
                    client.any { it != 0.toByte() } && tx.any { it != 0.toByte() } && size == b.remaining())
                val response = ByteArray(size).also { b.get(it) }; val wire = ByteBuffer.wrap(response)
                require(wire.int == 0x53505231)
                val certSize = wire.getInt(36)
                require(certSize in 1..8192 && size == 40 + certSize + 384)
                require(MessageDigest.getInstance("SHA-256").digest(response.copyOfRange(40, 40 + certSize)).contentEquals(pin))
                return PcPairingDelivery(created, expires, PcAuthorizationController.Target(hex(client), caps, revision), hex(tx), hex(pin), response)
            } catch (_: Exception) { throw IllegalArgumentException("Invalid saved PC pairing delivery") }
        }
    }
}
