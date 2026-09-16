// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.nio.ByteBuffer
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLEngineResult.HandshakeStatus
import javax.net.ssl.SSLEngineResult.Status
import javax.net.ssl.SSLException

/** Serialized worker-owned TLS stream. GATT callbacks must enqueue input onto
 * that worker, never call this recursively. Output callbacks must synchronously
 * copy/queue their bytes or throw on backpressure; retained plaintext is cleared
 * after delivery. A channel is one connection generation and cannot reconnect.
 * The owner supplies connection/handshake deadlines and disconnects on failure.
 */
class ProvisionTlsChannel internal constructor(
    private val engine: SSLEngine,
    private val ciphertext: (ByteArray) -> Unit,
    private val plaintext: (ByteArray) -> Unit,
) : AutoCloseable {
    private val incoming = ByteBuffer.allocate(32768)
    private val outgoing = ByteBuffer.allocate(32768)
    private val decoded = ByteBuffer.allocate(32768)
    private val empty = ByteBuffer.allocate(0)
    private var started = false
    private var closed = false
    private var operating = false
    var established = false
        private set

    fun start() = operate {
        check(!started) { "TLS channel already started" }
        started = true
        engine.beginHandshake()
        pump()
    }

    fun receive(bytes: ByteArray) = operate {
        check(started) { "TLS channel not started" }
        // Fragmentation is a transport concern. No unbounded TLS record buffer.
        require(bytes.isNotEmpty() && bytes.size <= incoming.remaining()) {
            "TLS input capacity exceeded"
        }
        incoming.put(bytes)
        pump()
    }

    fun send(bytes: ByteArray) = operate {
        check(established) { "TLS authentication incomplete" }
        require(bytes.size in 1..16384) { "TLS application message size invalid" }
        val input = ByteBuffer.wrap(bytes)
        while (input.hasRemaining()) {
            val consumed = wrap(input)
            if (consumed == 0) throw SSLException("TLS write made no progress")
        }
    }

    private fun wrap(input: ByteBuffer): Int {
        outgoing.clear()
        val result = engine.wrap(input, outgoing)
        if (result.status != Status.OK) throw SSLException("TLS output failed")
        if (result.handshakeStatus == HandshakeStatus.FINISHED) established = true
        outgoing.flip()
        if (outgoing.hasRemaining()) {
            val bytes = ByteArray(outgoing.remaining())
            outgoing.get(bytes)
            try { ciphertext(bytes) } finally { bytes.fill(0) }
        }
        return result.bytesConsumed()
    }

    private fun pump() {
        // Every iteration consumes a record, emits a handshake flight, or runs
        // delegated tasks. Broken peers/providers cannot spin the product owner.
        repeat(256) {
            if (established && engine.handshakeStatus != HandshakeStatus.NOT_HANDSHAKING) {
                throw SSLException("TLS renegotiation is not supported")
            }
            when (engine.handshakeStatus) {
                HandshakeStatus.NEED_TASK -> {
                    var task = engine.delegatedTask
                    var count = 0
                    while (task != null) {
                        if (++count > 64) throw SSLException("TLS task limit")
                        task.run()
                        task = engine.delegatedTask
                    }
                    if (count == 0) throw SSLException("TLS task made no progress")
                }
                HandshakeStatus.NEED_WRAP -> wrap(empty)
                HandshakeStatus.NEED_UNWRAP, HandshakeStatus.NOT_HANDSHAKING -> {
                    if (incoming.position() == 0) return
                    incoming.flip()
                    decoded.clear()
                    val result = try { engine.unwrap(incoming, decoded) }
                                 finally { incoming.compact() }
                    if (result.handshakeStatus == HandshakeStatus.FINISHED) established = true
                    when (result.status) {
                        Status.BUFFER_UNDERFLOW -> return
                        Status.OK -> Unit
                        else -> throw SSLException("TLS input failed or closed")
                    }
                    decoded.flip()
                    if (decoded.hasRemaining()) {
                        if (!established) throw SSLException("Unauthenticated TLS application data")
                        val bytes = ByteArray(decoded.remaining())
                        decoded.get(bytes)
                        try { plaintext(bytes) } finally {
                            bytes.fill(0)
                            decoded.array().fill(0)
                        }
                    }
                    if (result.bytesConsumed() == 0 && result.bytesProduced() == 0 &&
                        result.handshakeStatus == HandshakeStatus.NOT_HANDSHAKING) return
                }
                else -> throw SSLException("Unsupported TLS handshake state")
            }
        }
        throw SSLException("TLS progress limit")
    }

    private fun operate(action: () -> Unit) {
        check(!closed && !operating) { "TLS channel unavailable or reentered" }
        operating = true
        try { action() } catch (error: Exception) {
            close()
            throw error
        } finally { operating = false }
    }

    /** Abort only; owner closes GATT as well. No claim of graceful TLS shutdown. */
    override fun close() {
        closed = true
        established = false
        incoming.array().fill(0)
        outgoing.array().fill(0)
        decoded.array().fill(0)
        engine.closeOutbound()
    }
}
