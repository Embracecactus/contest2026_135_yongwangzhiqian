// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import com.shaniu.companion.provision.DeviceControlProtocol

/**
 * Serializes one authenticated SDC OTA source-record upload. ACCEPTED means
 * only that OTA_START was accepted; firmware progress remains OTA_STATUS data.
 */
internal class OtaControlUpload(
    record: ByteArray,
    private val send: (DeviceControlProtocol.Command, ByteArray) -> Boolean
) : AutoCloseable {
    enum class State {
        READY,
        WAITING,
        ACCEPTED,
        FAILED,
        CANCELED,
        // 本地释放不是远端取消确认；远端结果仍需由 Session/状态查询取得。
        CLOSED
    }

    private var ownedRecord = record.copyOf().also {
        require(it.size in MIN_RECORD_SIZE..MAX_RECORD_SIZE)
    }
    private var pending: DeviceControlProtocol.Command? = null
    private var pendingCount = 0
    private var uploaded = 0
    private var appendedChunks = 0
    private var cancelRequested = false

    var state: State = State.READY
        private set
    var error: Int? = null
        private set
    /** Aggregate source-record progress only; no record bytes or endpoint data. */
    val totalBytes: Int get() = if (state in setOf(State.FAILED, State.CANCELED, State.CLOSED)) 0 else ownedRecord.size
    val uploadedBytes: Int get() = if (state in setOf(State.FAILED, State.CANCELED, State.CLOSED)) 0 else uploaded
    val appendCount: Int get() = if (state in setOf(State.FAILED, State.CANCELED, State.CLOSED)) 0 else appendedChunks

    fun start(): Boolean {
        if (state != State.READY) return false
        val payload = byteArrayOf(
            (ownedRecord.size ushr 24).toByte(),
            (ownedRecord.size ushr 16).toByte(),
            (ownedRecord.size ushr 8).toByte(),
            ownedRecord.size.toByte()
        )
        return transmit(DeviceControlProtocol.Command.OTA_BEGIN, payload)
    }

    fun response(command: DeviceControlProtocol.Command,
                 snapshot: DeviceControlProtocol.Snapshot) {
        // 已终结的对象不再接受旧回执，也不能重发或覆盖原始结果。
        if (state in setOf(State.ACCEPTED, State.FAILED, State.CANCELED, State.CLOSED)) return
        if (state != State.WAITING || command != pending) {
            fail(ERR_EPROTO)
            return
        }
        if (snapshot.error != 0) {
            fail(snapshot.error)
            return
        }

        when (command) {
            DeviceControlProtocol.Command.OTA_BEGIN -> afterBeginAck()
            DeviceControlProtocol.Command.OTA_APPEND -> afterAppendAck()
            DeviceControlProtocol.Command.OTA_START -> afterStartAck()
            DeviceControlProtocol.Command.OTA_CANCEL -> cancelAccepted()
            else -> fail(ERR_EPROTO)
        }
    }

    fun cancel() {
        when (state) {
            State.READY -> {
                state = State.CANCELED
                wipeRecord()
            }
            State.WAITING -> cancelRequested = true
            else -> Unit
        }
    }

    override fun close() {
        pending = null
        pendingCount = 0
        cancelRequested = false
        if (state == State.READY || state == State.WAITING) {
            state = State.CLOSED
            error = null
        }
        wipeRecord()
    }

    private fun afterBeginAck() {
        if (cancelRequested) {
            transmitCancel()
        } else {
            sendNextAppend()
        }
    }

    private fun afterAppendAck() {
        uploaded += pendingCount
        appendedChunks++
        pendingCount = 0
        if (cancelRequested) {
            transmitCancel()
        } else if (uploaded == ownedRecord.size) {
            transmit(DeviceControlProtocol.Command.OTA_START, ByteArray(0))
        } else {
            sendNextAppend()
        }
    }

    private fun afterStartAck() {
        if (cancelRequested) {
            transmitCancel()
        } else {
            pending = null
            state = State.ACCEPTED
            error = null
            wipeRecord()
        }
    }

    private fun sendNextAppend() {
        if (uploaded !in 0 until ownedRecord.size) {
            fail(ERR_EPROTO)
            return
        }
        pendingCount = minOf(MAX_APPEND_SIZE, ownedRecord.size - uploaded)
        val payload = ownedRecord.copyOfRange(uploaded, uploaded + pendingCount)
        transmit(DeviceControlProtocol.Command.OTA_APPEND, payload)
    }

    private fun transmitCancel() {
        transmit(DeviceControlProtocol.Command.OTA_CANCEL, ByteArray(0))
    }

    private fun transmit(command: DeviceControlProtocol.Command, payload: ByteArray): Boolean {
        pending = command
        pendingCount = if (command == DeviceControlProtocol.Command.OTA_APPEND) {
            payload.size
        } else {
            0
        }
        state = State.WAITING
        val accepted = try {
            send(command, payload)
        } catch (_: Exception) {
            false
        } finally {
            payload.fill(0)
        }
        if (!accepted) {
            fail(ERR_EAGAIN)
            return false
        }
        return true
    }

    private fun cancelAccepted() {
        pending = null
        pendingCount = 0
        state = State.CANCELED
        error = null
        wipeRecord()
    }

    private fun fail(code: Int) {
        pending = null
        pendingCount = 0
        state = State.FAILED
        error = code
        wipeRecord()
    }

    private fun wipeRecord() {
        ownedRecord.fill(0)
    }

    private companion object {
        const val MIN_RECORD_SIZE = 44
        const val MAX_RECORD_SIZE = 3371
        const val MAX_APPEND_SIZE = 32
        const val ERR_EAGAIN = -11
        const val ERR_EPROTO = -71
    }
}
