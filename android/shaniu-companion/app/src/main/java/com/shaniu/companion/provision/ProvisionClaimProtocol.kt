// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.SecureRandom

/** Serialized TLS-worker protocol. send must copy/enqueue synchronously.
 * Authentication starts only after the pinned TLS channel is established.
 * Credentials are not sent until the board authorizes this device over the
 * authenticated channel. LOCAL_CONFIRMATION names that device authorization;
 * it is not a request for a user PTT/physical-button press. Legacy recovery
 * may still have a separate physical confirmation path owned by the firmware.
 */
class ProvisionClaimProtocol(
    bootstrap: ProvisionBootstrap,
    bundle: ByteArray,
    private val send: (ByteArray) -> Unit,
    private val changed: (State) -> Unit,
    private val beforeApply: (ByteArray) -> Unit = {},
    recoveryTransaction: ByteArray? = null,
    private val beforeApplyConfiguration: ((ByteArray, ByteArray) -> Unit)? = null,
) : AutoCloseable {
    enum class State { NEW, AUTHENTICATING, LOCAL_CONFIRMATION, UPLOADING,
        VERIFYING, COMMITTED, NOT_COMMITTED, FAILED, UNCONFIRMED, CLOSED }
    var state = State.NEW
        private set
    /** Safe context for a terminal failure. It contains no response payload. */
    var failureStage: State? = null
        private set
    var failureCode: Int? = null
        private set
    private val candidate = bundle.takeIf { it.size in 1..16384 }?.copyOf()
        ?: throw IllegalArgumentException("Invalid configuration size")
    private val secret = try { bootstrap.usePossessionSecret { it.copyOf() } }
        catch (_: Exception) {
            candidate.fill(0)
            throw IllegalArgumentException("Provisioning bootstrap is unavailable")
        }
    private val recovery = recoveryTransaction != null
    private val transaction = recoveryTransaction?.also {
        require(it.size == 16 && it.any { b -> b != 0.toByte() })
    }?.copyOf() ?: ByteArray(16).also { SecureRandom().nextBytes(it) }
    private val input = ByteArray(40)
    private var used = 0
    private var sequence = 0
    private var offset = 0
    private var awaiting = 1

    fun start() {
        check(state == State.NEW)
        try {
            update(State.AUTHENTICATING)
            request(1, secret)
        } finally { secret.fill(0) }
    }

    fun receive(bytes: ByteArray) {
        if (state == State.COMMITTED || state == State.NOT_COMMITTED) return
        check(state !in setOf(State.NEW, State.CLOSED, State.FAILED, State.UNCONFIRMED))
        try {
            var pos = 0
            while (pos < bytes.size) {
                val count = minOf(input.size - used, bytes.size - pos)
                bytes.copyInto(input, used, pos, pos + count)
                used += count; pos += count
                if (used == input.size) {
                    response()
                    input.fill(0); used = 0
                }
            }
        } catch (e: Exception) {
            recordMalformedFailure()
            wipe()
            update(if (state == State.VERIFYING) State.UNCONFIRMED else State.FAILED)
            throw IllegalStateException("Invalid provisioning response")
        }
    }

    private fun response() {
        val frame = ByteBuffer.wrap(input).order(ByteOrder.BIG_ENDIAN)
        require(frame.int == 0x53505631 && frame.int == 0x80000000.toInt())
        require(frame.int == sequence)
        val tx = ByteArray(16).also { frame.get(it) }
        require(tx.contentEquals(transaction) && frame.int == 8)
        val remote = frame.int
        val result = frame.int
        require(remote in 2..9 && result <= 0)
        if (remote == 7 || remote == 8) {
            require(result < 0)
            // Only retain the code after the complete response has passed all
            // framing, transaction, sequence and remote-state validation.
            failureStage = state
            failureCode = result
            wipe()
            update(if (remote == 8) State.UNCONFIRMED else State.FAILED)
            return
        }
        require(result == 0)
        when (awaiting) {
            1 -> when (remote) {
                2 -> update(State.LOCAL_CONFIRMATION)
                3 -> {
                    sequence++
                    if (recovery) {
                        update(State.VERIFYING)
                        request(5, byteArrayOf())
                    } else {
                        update(State.UPLOADING)
                        request(2, ByteBuffer.allocate(4).putInt(candidate.size).array())
                    }
                }
                else -> error("Unexpected authentication state")
            }
            2, 3 -> {
                require(remote == 4)
                sequence++
                if (offset < candidate.size) {
                    val count = minOf(1020, candidate.size - offset)
                    val data = ByteBuffer.allocate(4 + count).putInt(offset)
                        .put(candidate, offset, count).array()
                    offset += count
                    try { request(3, data) } finally { data.fill(0) }
                } else {
                    // Persist the public receipt locator before any APPLY bytes
                    // can reach the board. Failure here leaves the trial unused.
                    val receipt = transaction.copyOf()
                    // The configuration callback receives the exact uploaded
                    // candidate. Its private copy cannot mutate protocol state
                    // and is wiped even if durable storage fails.
                    val configuration = if (beforeApplyConfiguration != null) candidate.copyOf() else null
                    try {
                        if (configuration != null) beforeApplyConfiguration!!.invoke(receipt, configuration)
                        else beforeApply(receipt)
                    } finally { receipt.fill(0); configuration?.fill(0) }
                    update(State.VERIFYING)
                    request(4, byteArrayOf())
                }
            }
            4 -> {
                require(remote == 5 || remote == 6)
                if (remote == 6) { wipe(); update(State.COMMITTED) }
            }
            5 -> {
                require(recovery && (remote == 5 || remote == 6 || remote == 9))
                if (remote != 5) {
                    wipe()
                    update(if (remote == 6) State.COMMITTED else State.NOT_COMMITTED)
                }
            }
            else -> error("Unexpected response")
        }
    }

    private fun request(type: Int, payload: ByteArray) {
        awaiting = type
        val frame = ByteBuffer.allocate(32 + payload.size).order(ByteOrder.BIG_ENDIAN)
            .putInt(0x53505631).put(type.toByte()).put(byteArrayOf(0, 0, 0))
            .putInt(sequence).put(transaction).putInt(payload.size).put(payload).array()
        try { send(frame) } finally { frame.fill(0) }
    }

    private fun update(value: State) { state = value; changed(value) }
    private fun recordMalformedFailure() {
        if (failureStage == null) failureStage = state
    }
    private fun wipe() { secret.fill(0); candidate.fill(0); input.fill(0) }

    /** A disconnect after APPLY cannot prove whether durable commit happened. */
    fun disconnected() {
        if (state == State.COMMITTED || state == State.NOT_COMMITTED || state == State.FAILED || state == State.CLOSED) return
        val outcome = if (state == State.VERIFYING || state == State.UNCONFIRMED)
            State.UNCONFIRMED else State.FAILED
        wipe(); update(outcome)
    }

    override fun close() {
        wipe(); transaction.fill(0)
        if (state !in setOf(State.COMMITTED, State.NOT_COMMITTED, State.UNCONFIRMED, State.FAILED)) update(State.CLOSED)
    }
    override fun toString() = "ProvisionClaimProtocol(redacted)"
}
