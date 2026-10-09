// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import com.shaniu.companion.provision.DeviceControlProtocol
import java.nio.ByteBuffer
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class OtaControlUploadTest {
    private data class Sent(val command: DeviceControlProtocol.Command, val payload: ByteArray)

    private fun record(size: Int) = ByteArray(size) { (it and 0xff).toByte() }
    private fun ack(error: Int = 0) = DeviceControlProtocol.Snapshot(
        error, false, false, null, null, null, null)

    @Test fun sendsBoundedChunksAndAcceptsOnlyStartAcknowledgement() {
        val sent = mutableListOf<Sent>()
        val borrowed = mutableListOf<ByteArray>()
        val original = record(70)
        val upload = OtaControlUpload(original) { command, payload ->
            borrowed += payload
            sent += Sent(command, payload.copyOf())
            true
        }

        assertTrue(upload.start())
        assertEquals(DeviceControlProtocol.Command.OTA_BEGIN, sent.last().command)
        assertEquals(70, ByteBuffer.wrap(sent.last().payload).int)
        assertTrue(borrowed.last().all { it == 0.toByte() })
        assertArrayEquals(record(70), original)

        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        upload.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        upload.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        upload.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        assertEquals(listOf(32, 32, 6), sent.drop(1).take(3).map { it.payload.size })
        assertEquals(70, upload.totalBytes)
        assertEquals(70, upload.uploadedBytes)
        assertEquals(3, upload.appendCount)
        assertEquals(DeviceControlProtocol.Command.OTA_START, sent.last().command)
        assertEquals(OtaControlUpload.State.WAITING, upload.state)

        upload.response(DeviceControlProtocol.Command.OTA_START, ack())
        assertEquals(OtaControlUpload.State.ACCEPTED, upload.state)
        assertNull(upload.error)
        assertTrue(borrowed.all { it.all { byte -> byte == 0.toByte() } })
        assertFalse(upload.start())
    }

    @Test fun rejectsUnavailableTransportWithoutReplayAndAllowsMaximumRecord() {
        val payload = record(3371)
        val sent = mutableListOf<Sent>()
        val upload = OtaControlUpload(payload) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            false
        }

        assertFalse(upload.start())
        assertEquals(OtaControlUpload.State.FAILED, upload.state)
        assertEquals(-11, upload.error)
        assertEquals(1, sent.size)
        assertFalse(upload.start())
        assertEquals(1, sent.size)
        assertArrayEquals(record(3371), payload)
    }

    @Test fun responseErrorsAndMismatchesFailAndDoNotReuseRecord() {
        val sent = mutableListOf<Sent>()
        val upload = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        upload.start()
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack(-16))
        assertEquals(OtaControlUpload.State.FAILED, upload.state)
        assertEquals(-16, upload.error)
        assertEquals(0, upload.totalBytes)
        assertEquals(0, upload.uploadedBytes)
        assertEquals(0, upload.appendCount)
        assertFalse(upload.start())

        val mismatch = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        mismatch.start()
        mismatch.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        assertEquals(OtaControlUpload.State.FAILED, mismatch.state)
        assertEquals(-71, mismatch.error)
    }

    @Test fun cancelWaitsForAcknowledgementAndHandlesStartRace() {
        val sent = mutableListOf<Sent>()
        val beforeStart = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        beforeStart.cancel()
        assertEquals(OtaControlUpload.State.CANCELED, beforeStart.state)
        assertTrue(sent.isEmpty())

        val duringAppend = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        duringAppend.start()
        duringAppend.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        duringAppend.cancel()
        duringAppend.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        assertEquals(DeviceControlProtocol.Command.OTA_CANCEL, sent.last().command)
        duringAppend.response(DeviceControlProtocol.Command.OTA_CANCEL, ack())
        assertEquals(OtaControlUpload.State.CANCELED, duringAppend.state)
        assertEquals(0, duringAppend.totalBytes)
        assertEquals(0, duringAppend.uploadedBytes)
        assertEquals(0, duringAppend.appendCount)

        val duringStart = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        duringStart.start()
        duringStart.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        duringStart.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        duringStart.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        assertEquals(DeviceControlProtocol.Command.OTA_START, sent.last().command)
        duringStart.cancel()
        duringStart.response(DeviceControlProtocol.Command.OTA_START, ack())
        assertEquals(DeviceControlProtocol.Command.OTA_CANCEL, sent.last().command)
        duringStart.response(DeviceControlProtocol.Command.OTA_CANCEL, ack())
        assertEquals(OtaControlUpload.State.CANCELED, duringStart.state)
    }

    @Test fun closeCancelsWithoutSending() {
        val sent = mutableListOf<Sent>()
        val upload = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf())
            true
        }
        upload.start()
        upload.close()
        // Local disposal is not a remote cancellation acknowledgement.
        assertFalse(upload.state == OtaControlUpload.State.CANCELED)
        assertTrue(sent.size == 1)
        assertNull(upload.error)
    }
    @Test fun lateAcknowledgementCannotOverwriteCanceledTerminalState() {
        val sent = mutableListOf<Sent>()
        val upload = OtaControlUpload(record(44)) { command, bytes ->
            sent += Sent(command, bytes.copyOf()); true
        }
        assertTrue(upload.start())
        upload.cancel()
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        assertEquals(DeviceControlProtocol.Command.OTA_CANCEL, sent.last().command)
        upload.response(DeviceControlProtocol.Command.OTA_CANCEL, ack())
        assertEquals(OtaControlUpload.State.CANCELED, upload.state)
        val count = sent.size
        // Duplicate or delayed transport delivery cannot change a terminal result.
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        assertEquals(OtaControlUpload.State.CANCELED, upload.state)
        assertEquals(count, sent.size)
        assertEquals(0, upload.totalBytes)
    }


    @Test fun cancellationRequestWaitsForRemoteConfirmation() {
        val sent = mutableListOf<DeviceControlProtocol.Command>()
        val upload = OtaControlUpload(record(44)) { command, _ -> sent += command; true }
        upload.start()
        upload.cancel()
        assertEquals(OtaControlUpload.State.WAITING, upload.state)
        assertEquals(1, sent.size)
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        assertEquals(OtaControlUpload.State.WAITING, upload.state)
        assertEquals(DeviceControlProtocol.Command.OTA_CANCEL, sent.last())
        upload.response(DeviceControlProtocol.Command.OTA_CANCEL, ack(-114))
        assertEquals(OtaControlUpload.State.FAILED, upload.state)
        assertEquals(-114, upload.error)
        val count = sent.size
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        assertEquals(-114, upload.error)
        assertEquals(count, sent.size)
    }

    @Test fun acceptedTerminalCannotBecomeCanceledFromLateAckOrClose() {
        val sent = mutableListOf<DeviceControlProtocol.Command>()
        val upload = OtaControlUpload(record(44)) { command, _ -> sent += command; true }
        upload.start()
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        upload.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        upload.response(DeviceControlProtocol.Command.OTA_APPEND, ack())
        upload.response(DeviceControlProtocol.Command.OTA_START, ack())
        assertEquals(OtaControlUpload.State.ACCEPTED, upload.state)
        val count = sent.size
        upload.cancel()
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        upload.close()
        assertEquals(OtaControlUpload.State.ACCEPTED, upload.state)
        assertEquals(count, sent.size)
    }

    @Test fun localCloseIgnoresLateAckWithoutClaimingRemoteCancellation() {
        val sent = mutableListOf<DeviceControlProtocol.Command>()
        val upload = OtaControlUpload(record(44)) { command, _ -> sent += command; true }
        upload.start()
        upload.close()
        val terminal = upload.state
        assertFalse(terminal == OtaControlUpload.State.CANCELED)
        upload.response(DeviceControlProtocol.Command.OTA_BEGIN, ack())
        assertEquals(terminal, upload.state)
        assertEquals(1, sent.size)
        assertEquals(0, upload.totalBytes)
        assertFalse(upload.start())
    }

}
